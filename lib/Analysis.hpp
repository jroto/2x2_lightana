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
#include "MetaWaveformAna.hpp"
#include "Run.hpp"
#include "WaveformAna.hpp"

#include "TFile.h"
#include "TTree.h"

#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ndlar_light {

/// Factory signature for constructing a concrete MetaWaveformAna from a
/// raw Waveform plus its Event-level validity flag. Lets Analysis be
/// pluggable: pass a custom factory to run a different analysis
/// implementation without changing Analysis/EventAna/Dump.
using WaveformAnaFactory =
    std::function<std::unique_ptr<MetaWaveformAna>(const Waveform&, bool isValid)>;

/// Default factory, constructing the standard WaveformAna (mean-only,
/// for now).
inline WaveformAnaFactory DefaultWaveformAnaFactory()
{
    return [](const Waveform& wf, bool isValid) {
        return std::make_unique<WaveformAna>(wf, isValid);
    };
}

class Analysis {
public:
    /// `run` is stored as a reference; Analysis does not manage its
    /// lifetime. `factory` selects which concrete MetaWaveformAna
    /// implementation to construct per waveform - defaults to the
    /// standard WaveformAna, but callers can supply their own to plug in
    /// a different analysis without touching Analysis/EventAna/Dump.
    explicit Analysis(Run& run, WaveformAnaFactory factory = DefaultWaveformAnaFactory())
        : fRun(run), fFactory(std::move(factory)) {}

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
                    auto ptr = fFactory(event.GetWaveform(adc, ch), valid);
                    ana.SetWaveformAna(adc, ch, std::move(ptr));
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
    /// Run or call process(). Analysis-variable branches are created for
    /// every parameter name registered in the shared
    /// MetaWaveformAna registry (MetaWaveformAna::ParamNames()) - since
    /// every concrete analyzer registers its keys once via
    /// RegisterParam(), this remains dynamic: new analysis quantities
    /// automatically appear here without touching this method.
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

        // Full set of registered analysis parameter names - assumed to be
        // the complete union across all analyzer implementations, since
        // each registers its keys once via MetaWaveformAna::RegisterParam().
        const auto& allNames = MetaWaveformAna::ParamNames();

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

        // Dynamic analysis-variable branches, one Double_t per registered
        // parameter name, addressed by registry index at fill time.
        struct ParamBranch {
            std::string name;
            std::size_t index;
            Double_t value;
        };

        // Elements are constructed in-place in their final vector slots
        // first (via resize()), and only then does each tree->Branch()
        // take the address of that (now-stable) vector element - binding
        // the branch to the address of a temporary local variable that
        // then gets copied into the vector (e.g. via push_back) would
        // leave the branch pointing at stack memory that's reused/
        // invalidated on the next loop iteration.
        std::vector<ParamBranch> paramBranches(allNames.size());
        for (std::size_t i = 0; i < allNames.size(); ++i) {
            ParamBranch& pb = paramBranches[i];
            pb.name = allNames[i];
            pb.index = MetaWaveformAna::ParamIndex(pb.name);
            pb.value = std::numeric_limits<double>::quiet_NaN();
            tree->Branch(pb.name.c_str(), &pb.value, (pb.name + "/D").c_str());
        }

        for (const auto& eventAna : fEvents) {
            const EventMetadata& meta = eventAna.Meta();

            for (int adc_idx = 0; adc_idx < kNumADCs; ++adc_idx) {
                sn = meta.GetSerialNumber(adc_idx);
                utime_ms = meta.GetUTimeMs(adc_idx);
                tai_ns = meta.GetTaiNs(adc_idx);

                for (int ch_idx = 0; ch_idx < kNumChannels; ++ch_idx) {
                    const MetaWaveformAna& wa = eventAna.GetWaveformAna(adc_idx, ch_idx);

                    event_id = meta.GetId();
                    event_number = meta.GetEventNumber();
                    trig_type = meta.GetTriggerType();

                    adc = adc_idx;
                    channel = ch_idx;
                    valid = meta.IsValid(adc_idx, ch_idx);
                    clipped = wa.IsClipped();

                    for (auto& pb : paramBranches) {
                        pb.value = wa.HasParamIndex(pb.index)
                                       ? wa.GetParamByIndex(pb.index)
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
    WaveformAnaFactory fFactory;
    std::vector<EventAna> fEvents;
};

} // namespace ndlar_light
