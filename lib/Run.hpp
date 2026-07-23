#pragma once
//
// Run.hpp
//
// Represents a full run as an ordered collection of subrun files, and
// iterates events across all of them transparently (the caller doesn't
// need to know where one subrun ends and the next begins).
//
// Two ways to build a Run:
//   1. Run(std::vector<std::string> subrunFiles)              - explicit, ordered list.
//   2. Run(directory, runNumber, filenamePattern)              - scan + sort by subrun index.
//
// For (2), the filename pattern is a std::regex supplied by the caller
// every time (not hardcoded/inferred), since naming conventions vary
// between productions. The regex must have exactly one capture group,
// which is parsed as the subrun index (an integer) used for sorting.
// Example, matching `mpd_run_data_2026_07_16_14_05_34_CST_001130_p00003.FLOW.hdf5`
// for run number 1130:
//
//   std::regex pattern(R"(.*_001130_p(\d+)\.FLOW\.hdf5)");
//   ndlar_light::Run run("/pnfs/dune/scratch/users/jsoto/NDLAr_Run3/VBRscan_20260716",
//                         1130, pattern);
//
// NOTE (simplification vs. the original design sketch): this first
// version assumes simple identity-index correspondence between
// /light/events/data and /light/wvfm/data (see SubRunReader) and does
// not cross-validate against the HDF5 ref/ref_region datasets.
//

#include "Event.hpp"
#include "SubRunReader.hpp"

#include <algorithm>
#include <dirent.h>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

namespace ndlar_light {

class Run {
public:
    /// Build a Run from an explicit, ordered list of subrun file paths.
    /// The order given is the iteration order (no re-sorting is done).
    explicit Run(std::vector<std::string> subrunFiles)
        : fSubrunFiles(std::move(subrunFiles))
    {
        if (fSubrunFiles.empty()) {
            throw std::runtime_error("Run: empty subrun file list");
        }
        Init();
    }

    /// Build a Run by scanning `directory` for files matching `pattern`
    /// (a regex with exactly one capture group giving the subrun index),
    /// and sorting the matches by that index. `runNumber` is accepted
    /// for clarity/logging purposes but is not itself used to filter -
    /// bake the run number into `pattern` (see example above) so the
    /// matching logic stays fully caller-controlled.
    Run(const std::string& directory, int runNumber, const std::regex& pattern)
        : fRunNumber(runNumber)
    {
        fSubrunFiles = ScanDirectory(directory, pattern);
        if (fSubrunFiles.empty()) {
            throw std::runtime_error(
                "Run: no files matching pattern found in '" + directory +
                "' for run " + std::to_string(runNumber));
        }
        Init();
    }

    /// Total number of events across all subruns.
    size_t TotalEvents() const { return fTotalEvents; }

    /// Number of subrun files in this run.
    size_t NumSubruns() const { return fSubrunFiles.size(); }

    /// Filename of the subrun currently being iterated (sequential API).
    const std::string& CurrentSubrunFile() const { return fSubrunFiles.at(fCurrentSubrun); }

    /// Index (0-based, into the sorted/given file list) of the subrun
    /// currently being iterated.
    size_t CurrentSubrunIndex() const { return fCurrentSubrun; }

    // --- Sequential/streaming iteration (primary intended use) ---

    /// Resets iteration to the first event of the first subrun.
    void Reset()
    {
        fCurrentSubrun = 0;
        fRowInSubrun = 0;
        fReader.reset();
    }

    /// Whether there is another event to read via NextEvent().
    bool HasNext()
    {
        EnsureReaderOpen();
        while (fCurrentSubrun < fSubrunFiles.size() &&
               fRowInSubrun >= fReader->NumEvents()) {
            AdvanceToNextSubrun();
            if (fCurrentSubrun >= fSubrunFiles.size()) return false;
        }
        return fCurrentSubrun < fSubrunFiles.size();
    }

    /// Reads and returns the next event, advancing the iteration
    /// position. The returned reference is to an internal, reused Event
    /// buffer - copy it if you need it to outlive the next NextEvent() call.
    const Event& NextEvent()
    {
        if (!HasNext()) {
            throw std::runtime_error("Run::NextEvent: no more events");
        }
        fReader->ReadRow(fRowInSubrun, fEvent);
        ++fRowInSubrun;
        return fEvent;
    }

    // --- Random access by global event index (nice-to-have) ---

    /// Reads event `globalIndex` (0-based across the whole run) into the
    /// internal reused Event buffer and returns it. Internally translated
    /// to (subrun index, local row index) via a prefix-sum table built at
    /// open time. This does not disturb the sequential HasNext()/NextEvent()
    /// iteration position.
    const Event& GetEvent(size_t globalIndex)
    {
        if (globalIndex >= fTotalEvents) {
            throw std::runtime_error(
                "Run::GetEvent: index " + std::to_string(globalIndex) +
                " out of range (" + std::to_string(fTotalEvents) + " events)");
        }

        // Find the subrun whose [start, start + n_events) range contains globalIndex.
        // fSubrunStart[i] is the global index of the first event of subrun i.
        size_t subrun = static_cast<size_t>(
            std::upper_bound(fSubrunStart.begin(), fSubrunStart.end(), globalIndex) -
            fSubrunStart.begin()) - 1;
        size_t row = globalIndex - fSubrunStart[subrun];

        if (!fRandomAccessReader || fRandomAccessSubrun != subrun) {
            fRandomAccessReader = std::make_unique<SubRunReader>(fSubrunFiles[subrun]);
            fRandomAccessSubrun = subrun;
        }
        fRandomAccessReader->ReadRow(row, fRandomAccessEvent);
        return fRandomAccessEvent;
    }

private:
    void Init()
    {
        // Open each subrun just long enough to count its events, to build
        // the prefix-sum table for random access and TotalEvents(). This
        // keeps only one file open at a time even during setup.
        fSubrunStart.reserve(fSubrunFiles.size() + 1);
        size_t running_total = 0;
        for (const auto& path : fSubrunFiles) {
            fSubrunStart.push_back(running_total);
            SubRunReader reader(path);
            running_total += reader.NumEvents();
        }
        fSubrunStart.push_back(running_total);
        fTotalEvents = running_total;

        Reset();
    }

    static std::vector<std::string> ScanDirectory(const std::string& directory,
                                                   const std::regex& pattern)
    {
        std::vector<std::pair<long, std::string>> matches; // (subrun index, full path)

        DIR* dir = opendir(directory.c_str());
        if (!dir) {
            throw std::runtime_error("Run: could not open directory '" + directory + "'");
        }

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            std::smatch m;
            if (std::regex_match(name, m, pattern) && m.size() >= 2) {
                long subrun_index = std::stol(m[1].str());
                std::string full_path = directory;
                if (!full_path.empty() && full_path.back() != '/') full_path += '/';
                full_path += name;
                matches.emplace_back(subrun_index, full_path);
            }
        }
        closedir(dir);

        std::sort(matches.begin(), matches.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        std::vector<std::string> result;
        result.reserve(matches.size());
        for (auto& m : matches) result.push_back(std::move(m.second));
        return result;
    }

    void EnsureReaderOpen()
    {
        if (!fReader && fCurrentSubrun < fSubrunFiles.size()) {
            fReader = std::make_unique<SubRunReader>(fSubrunFiles[fCurrentSubrun]);
        }
    }

    void AdvanceToNextSubrun()
    {
        ++fCurrentSubrun;
        fRowInSubrun = 0;
        fReader.reset();
        if (fCurrentSubrun < fSubrunFiles.size()) {
            fReader = std::make_unique<SubRunReader>(fSubrunFiles[fCurrentSubrun]);
        }
    }

    std::vector<std::string> fSubrunFiles;
    int fRunNumber = -1;

    size_t fTotalEvents = 0;
    std::vector<size_t> fSubrunStart; // prefix sums, size = NumSubruns() + 1

    // Sequential iteration state.
    size_t fCurrentSubrun = 0;
    size_t fRowInSubrun = 0;
    std::unique_ptr<SubRunReader> fReader;
    Event fEvent;

    // Random-access state (independent of sequential iteration state).
    std::unique_ptr<SubRunReader> fRandomAccessReader;
    size_t fRandomAccessSubrun = static_cast<size_t>(-1);
    Event fRandomAccessEvent;
};

} // namespace ndlar_light
