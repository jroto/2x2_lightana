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

#include <cstddef>
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

private:
    Run& fRun;
    std::vector<EventAna> fEvents;
};

} // namespace ndlar_light
