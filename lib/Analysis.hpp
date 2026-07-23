#pragma once
//
// Analysis.hpp
//
// Processes a full Run event by event and waveform by waveform,
// accumulating results as a vector of EventAna. Does not own the Run
// (non-owning reference) - the caller must keep the Run alive for as
// long as the Analysis is used.
//

#include "Event.hpp"
#include "EventAna.hpp"
#include "Run.hpp"

#include "TFile.h"
#include "TTree.h"

#include <cstddef>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace ndlar_light {

class Analysis {
public:
    /// `run` is stored as a reference; Analysis does not manage its
    /// lifetime.
    explicit Analysis(Run& run) : fRun(run) {}

    /// Iterates the referenced Run from the start (calls Run::Reset()
    /// first, so process() always covers the full run regardless of any
    /// prior external iteration), building one EventAna per raw Event.
    /// Reads one raw Event at a time via Run's existing streaming API -
    /// never loads the whole run into memory as raw Events.
    void process()
    {
        fEvents.clear();
        fRun.Reset();

        while (fRun.HasNext()) {
            const Event& event = fRun.NextEvent();

            EventAna ana;
            ana.Meta() = event.Meta();

            for (int adc = 0; adc < kNumADCs; ++adc) {
                for (int ch = 0; ch < kNumChannels; ++ch) {
                    bool valid = event.IsValid(adc, ch);
                    ana.MutableWaveformAna(adc, ch) = WaveformAna(event.GetWaveform(adc, ch), valid);
                }
            }

            fEvents.push_back(std::move(ana));
        }
    }

    /// Results accumulated by the most recent process() call.
    const std::vector<EventAna>& GetEvents() const { return fEvents; }

    size_t GetNEvents() const { return fEvents.size(); }

    /// Dumps the current analysis results (fEvents) into a ROOT file as a
    /// TTree with one entry per (event, adc, channel) waveform. Uses only
    /// the already-computed contents of fEvents - does NOT re-read the
    /// Run or call process(). Analysis-variable branches (one per key
    /// found in any WaveformAna::GetResults() map) are discovered
    /// dynamically, so new analysis quantities automatically appear here
    /// without touching this method.
    void Dump(const std::string& filename, const std::string& treename = "waveforms") const
    {
        if (fEvents.empty()) {
            TFile outfile(filename.c_str(), "RECREATE");
            if (outfile.IsZombie()) {
                throw std::runtime_error("Analysis::Dump: failed to create ROOT file '" + filename + "'");
            }
            TTree* tree = new TTree(treename.c_str(), "NDLAr light analysis per waveform");
            outfile.cd();
            tree->Write();
            outfile.Close();
            return;
        }

        // Union of all analysis parameter names present across every
        // WaveformAna in fEvents - determines the dynamic branch set.
        std::set<std::string> paramNames;
        for (const auto& eventAna : fEvents) {
            for (int adc = 0; adc < kNumADCs; ++adc) {
                for (int ch = 0; ch < kNumChannels; ++ch) {
                    const WaveformAna& wa = eventAna.GetWaveformAna(adc, ch);
                    for (const auto& kv : wa.GetResults()) {
                        paramNames.insert(kv.first);
                    }
                }
            }
        }

        TFile outfile(filename.c_str(), "RECREATE");
        if (outfile.IsZombie()) {
            throw std::runtime_error("Analysis::Dump: failed to create ROOT file '" + filename + "'");
        }

        TTree* tree = new TTree(treename.c_str(), "NDLAr light analysis per waveform");

        // Event-level
        ULong64_t event_id;
        Int_t event_number;
        UChar_t trig_type;
        Int_t sn;
        ULong64_t utime_ms;
        ULong64_t tai_ns;

        // Waveform-level
        Int_t adc;
        Int_t channel;
        Bool_t valid;
        Bool_t clipped;

        tree->Branch("event_id", &event_id, "event_id/l");
        tree->Branch("event_number", &event_number, "event_number/I");
        tree->Branch("trig_type", &trig_type, "trig_type/b");

        tree->Branch("sn", &sn, "sn/I");
        tree->Branch("utime_ms", &utime_ms, "utime_ms/l");
        tree->Branch("tai_ns", &tai_ns, "tai_ns/l");

        tree->Branch("adc", &adc, "adc/I");
        tree->Branch("channel", &channel, "channel/I");
        tree->Branch("valid", &valid, "valid/O");
        tree->Branch("clipped", &clipped, "clipped/O");

        // Dynamic analysis-variable branches, one Double_t per key name.
        struct ParamBranch {
            std::string name;
            Double_t value;
        };

        std::vector<ParamBranch> paramBranches;
        paramBranches.reserve(paramNames.size());

        for (const auto& pname : paramNames) {
            ParamBranch pb;
            pb.name = pname;
            pb.value = std::numeric_limits<double>::quiet_NaN();
            tree->Branch(pb.name.c_str(), &pb.value, (pname + "/D").c_str());
            paramBranches.push_back(std::move(pb));
        }

        for (const auto& eventAna : fEvents) {
            const EventMetadata& meta = eventAna.Meta();

            for (int adc_idx = 0; adc_idx < kNumADCs; ++adc_idx) {
                sn = meta.GetSerialNumber(adc_idx);
                utime_ms = meta.GetUTimeMs(adc_idx);
                tai_ns = meta.GetTaiNs(adc_idx);

                for (int ch_idx = 0; ch_idx < kNumChannels; ++ch_idx) {
                    const WaveformAna& wa = eventAna.GetWaveformAna(adc_idx, ch_idx);
                    const auto& results = wa.GetResults();

                    event_id = meta.GetId();
                    event_number = meta.GetEventNumber();
                    trig_type = meta.GetTriggerType();

                    adc = adc_idx;
                    channel = ch_idx;
                    valid = meta.IsValid(adc_idx, ch_idx);
                    clipped = wa.IsClipped();

                    for (auto& pb : paramBranches) {
                        auto it = results.find(pb.name);
                        pb.value = (it != results.end())
                                       ? it->second
                                       : std::numeric_limits<double>::quiet_NaN();
                    }

                    tree->Fill();
                }
            }
        }

        outfile.cd();
        tree->Write();
        outfile.Close();
    }

private:
    Run& fRun;
    std::vector<EventAna> fEvents;
};

} // namespace ndlar_light
