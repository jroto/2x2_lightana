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
#include "ChannelMap.hpp"
#include "SubRunReader.hpp"

#include <algorithm>
#include <array>
#include <ctime>
#include <dirent.h>
#include <iomanip>
#include <iostream>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

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
        LoadDefaultChannelMap();
        Init();
    }

    /// Build a Run by scanning `directory` for files matching `pattern`
    /// (a regex with exactly one capture group giving the subrun index),
    /// and sorting the matches by that index. `runNumber` is accepted
    /// for clarity/logging purposes but is not itself used to filter -
    /// bake the run number into `pattern` (see example above) so the
    /// matching logic stays fully caller-controlled.
    Run(const std::string& directory, int runNumber)
        : fRunNumber(runNumber)
    {
        ////   std::regex pattern(R"(.*_001130_p(\d+)\.FLOW\.hdf5)");
        std::stringstream ss;
        ss << std::setw(6) << std::setfill('0') << runNumber;
        // NOTE: the subrun index (\d+) must stay inside a capture group -
        // ScanDirectory() uses that group to read the subrun index for
        // sorting; without parentheses here it silently matches nothing.
        std::string patternString =
            "mpd_run_data_.*_CST_" +  ss.str() +
            "_p(\\d{5})\\.FLOW\\.hdf5";

        std::regex pattern(patternString);

        fSubrunFiles = ScanDirectory(directory, pattern);
        if (fSubrunFiles.empty()) {
            throw std::runtime_error(
                "Run: no files matching pattern found in '" + directory + patternString +
                "' for run " + std::to_string(runNumber));
            
        }
        LoadDefaultChannelMap();
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
            fRandomAccessReader = std::make_unique<SubRunReader>(fSubrunFiles[subrun], &fChannelMap);
            fRandomAccessSubrun = subrun;
        }
        fRandomAccessReader->ReadRow(row, fRandomAccessEvent);
        return fRandomAccessEvent;
    }

    /// Reload the channel map from a CSV file.
    /// Takes effect from the next call to Reset() + process().
    void SetChannelMap(const std::string& csvPath) {
        fChannelMap = ChannelMap::LoadFromCSV(csvPath);
        fChannelMapPath = csvPath;
    }
    void ResetChannels(bool active = false) {
        fChannelMap.ResetAll(active);
    }
    /// Programmatically activate or deactivate a single channel.
    void SelectChannel(int adc, int ch, bool active) {
        fChannelMap.SetActive(adc, ch, active);
    }

    /// Read-only access to the current channel map.
    const ChannelMap& GetChannelMap() const { return fChannelMap; }

    /// UTC Unix timestamp of the first event in this run, in whole seconds.
    /// Derived from fStartTimeMs, which is populated in Init() by reading
    /// row 0 of the first subrun file. Use this as the `time` field for
    /// HistName to embed start-time provenance in histogram names.
    std::uint64_t StartTimeUnixSeconds() const { return fStartTimeMs / 1000; }

    /// UTC Unix timestamp of the first event in this run, in milliseconds.
    std::uint64_t StartTimeUnixMilliseconds() const { return fStartTimeMs; }

    /// Prints a human-readable summary of the run: run number, event/
    /// subrun counts, per-subrun file names and event counts, start/end
    /// times (UTC) and duration, first-event ADC serial numbers, and
    /// channel-map activity summary.
    void Print(std::ostream& os = std::cout) const
    {
        os << "============================================================\n";
        os << " Run summary\n";
        os << "============================================================\n";
        if (fRunNumber == -1) {
            os << " Run number      : N/A (constructed from file list)\n";
        } else {
            os << " Run number      : " << fRunNumber << "\n";
        }
        os << " Total events    : " << fTotalEvents << "\n";
        os << " Subruns         : " << fSubrunFiles.size() << "\n";
        os << "\n Subrun files:\n";
        for (size_t i = 0; i < fSubrunFiles.size(); ++i) {
            size_t nEvents = fSubrunStart[i + 1] - fSubrunStart[i];
            os << "   [" << i << "]  " << fSubrunFiles[i]
               << "   (" << nEvents << " events)\n";
        }

        auto formatUtc = [](uint64_t ms) {
            std::time_t t = static_cast<std::time_t>(ms / 1000);
            std::tm* tmPtr = std::gmtime(&t);
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tmPtr);
            return std::string(buf);
        };

        os << "\n Start time      : " << formatUtc(fStartTimeMs)
           << " UTC  (raw: " << fStartTimeMs << " ms)\n";
        os << " End time        : " << formatUtc(fEndTimeMs)
           << " UTC  (raw: " << fEndTimeMs << " ms)\n";

        uint64_t durationSec = (fEndTimeMs - fStartTimeMs) / 1000;
        uint64_t hh = durationSec / 3600;
        uint64_t mm = (durationSec % 3600) / 60;
        uint64_t sscount = durationSec % 60;
        os << " Run duration    : " << hh << "h "
           << std::setw(2) << std::setfill('0') << mm << "m "
           << std::setw(2) << std::setfill('0') << sscount << "s\n"
           << std::setfill(' ');

        os << "\n ADC serial numbers (first event):\n";
        for (int adc = 0; adc < kNumADCs; ++adc) {
            os << "   ADC " << adc << ": " << fStartSn[adc] << "  ";
            if (adc % 4 == 3) os << "\n";
        }
        if (kNumADCs % 4 != 0) os << "\n";

        os << "\n Channel map     : " << fChannelMapPath << "\n";
        int totalActive = 0;
        std::array<int, kNumADCs> activePerAdc{};
        for (int adc = 0; adc < kNumADCs; ++adc) {
            int n = 0;
            for (int ch = 0; ch < kNumChannels; ++ch)
                if (fChannelMap.IsActive(adc, ch)) ++n;
            activePerAdc[adc] = n;
            totalActive += n;
        }
        os << " Active channels : " << totalActive << " / " << (kNumADCs * kNumChannels) << "\n";
        for (int adc = 0; adc < kNumADCs; ++adc) {
            os << "   ADC " << adc << ": " << std::setw(3) << activePerAdc[adc] << " active  ";
            if (adc % 4 == 3) os << "\n";
        }
        os << "============================================================\n";
    }

    friend std::ostream& operator<<(std::ostream& os, const Run& r) {
        r.Print(os);
        return os;
    }
    /// Return a snapshot of all currently selected channels.
    /// A channel is selected when ChannelMap::IsActive(adc, ch) is true.
    /// The returned list is ordered by ADC and then channel number.
    std::vector<std::pair<int, int>> GetSelectedChannels() const
    {
        std::vector<std::pair<int, int>> selected;

        for (int adc = 0; adc < kNumADCs; ++adc) {
            for (int ch = 0; ch < kNumChannels; ++ch) {
                if (fChannelMap.IsActive(adc, ch)) {
                    selected.emplace_back(adc, ch);
                }
            }
        }

        return selected;
    }
private:
    /// Tries to load the default channel-map CSV; falls back silently to
    /// an all-channels-active map if it's missing/unreadable, so Run
    /// works out of the box with no CSV present.
    void LoadDefaultChannelMap()
    {
        try {
            fChannelMap = ChannelMap::LoadFromCSV(kDefaultChannelMapPath);
            fChannelMapPath = kDefaultChannelMapPath;
        } catch (...) {
            fChannelMap = ChannelMap(); // all channels active
            fChannelMapPath = "(none - all channels active)";
        }
    }

    void Init()
    {
        // Open each subrun just long enough to count its events, to build
        // the prefix-sum table for random access and TotalEvents(). This
        // keeps only one file open at a time even during setup.
        std::cout << "Run " << fRunNumber << ": scanning subruns..." << std::endl;
        fSubrunStart.reserve(fSubrunFiles.size() + 1);
        size_t running_total = 0;
        for (const auto& path : fSubrunFiles) {
            fSubrunStart.push_back(running_total);
            SubRunReader reader(path, &fChannelMap);
            running_total += reader.NumEvents();
        }
        fSubrunStart.push_back(running_total);
        fTotalEvents = running_total;

        // Cache first-event metadata (start time, serial numbers)
        {
            SubRunReader r(fSubrunFiles.front(), &fChannelMap);
            Event e;
            r.ReadRow(0, e);
            fStartTimeMs = e.Meta().GetUTimeMs(0);
            for (int adc = 0; adc < kNumADCs; ++adc)
                fStartSn[adc] = e.Meta().GetSerialNumber(adc);
        }

        // Cache last-event metadata (end time) via direct random access
        // ReadRow supports any row index - reading the last row is as
        // cheap as reading the first (HDF5 hyperslab selection).
        {
            const size_t lastSubrun = fSubrunFiles.size() - 1;
            const size_t lastRow    = fSubrunStart[lastSubrun + 1]
                                    - fSubrunStart[lastSubrun] - 1;
            SubRunReader r(fSubrunFiles.back(), &fChannelMap);
            Event e;
            r.ReadRow(lastRow, e);
            fEndTimeMs = e.Meta().GetUTimeMs(0);
        }
        std::cout << "Run: done. Total events = " << fTotalEvents << std::endl;
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
            fReader = std::make_unique<SubRunReader>(fSubrunFiles[fCurrentSubrun], &fChannelMap);
        }
    }

    void AdvanceToNextSubrun()
    {
        ++fCurrentSubrun;
        fRowInSubrun = 0;
        fReader.reset();
        if (fCurrentSubrun < fSubrunFiles.size()) {
            fReader = std::make_unique<SubRunReader>(fSubrunFiles[fCurrentSubrun], &fChannelMap);
        }
    }

    std::vector<std::string> fSubrunFiles;
    int fRunNumber = -1;
    ChannelMap fChannelMap;
    std::string fChannelMapPath = "(none - all channels active)";

    size_t fTotalEvents = 0;
    std::vector<size_t> fSubrunStart; // prefix sums, size = NumSubruns() + 1

    // Cached first/last-event metadata, filled in Init().
    uint64_t fStartTimeMs = 0;                  // utime_ms[0] of first event
    uint64_t fEndTimeMs   = 0;                  // utime_ms[0] of last event
    std::array<int32_t, kNumADCs> fStartSn = {}; // sn[8] of first event


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
