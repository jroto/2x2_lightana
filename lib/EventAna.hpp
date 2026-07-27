#pragma once
//
// EventAna.hpp
//
// Analyzed counterpart to Event: shares EventMetadata (via composition,
// same pattern as Event) but holds an 8x64 matrix of polymorphic
// MetaWaveformAna instead of raw Waveform. Storage is
// std::unique_ptr<MetaWaveformAna> (not a concrete WaveformAna) so
// Analysis can plug in any MetaWaveformAna-derived analyzer - see
// Analysis.hpp's WaveformAnaFactory.
//

#include "EventMetadata.hpp"
#include "MetaWaveformAna.hpp"

#include <memory>
#include <ostream>
#include <stdexcept>

namespace ndlar_light {

class EventAna {
public:
    EventAna() = default;

    EventMetadata& Meta() { return fMeta; }
    const EventMetadata& Meta() const { return fMeta; }

    /// Throws std::runtime_error if (adc, channel) hasn't been set yet
    /// (i.e. still nullptr) - this indicates the EventAna wasn't fully
    /// populated by Analysis::process().
    const MetaWaveformAna& GetWaveformAna(int adc, int channel) const {
        const auto& ptr = fWaveformAnas[adc][channel];
        if (!ptr) {
            throw std::runtime_error(
                "EventAna::GetWaveformAna: no analysis set for ADC " +
                std::to_string(adc) + " CH " + std::to_string(channel));
        }
        return *ptr;
    }

    /// Sets the analysis result for (adc, channel), taking ownership of
    /// `ana`. Used by Analysis::process() with the configured factory.
    void SetWaveformAna(int adc, int channel, std::unique_ptr<MetaWaveformAna> ana) {
        fWaveformAnas[adc][channel] = std::move(ana);
    }

    /// Prints event metadata (always) and, if `printWaveforms` is true,
    /// each valid MetaWaveformAna (mirrors Event::Print). Skips (adc, ch)
    /// slots that are either invalid or not yet populated (nullptr).
    void Print(bool printWaveforms = false, std::ostream& os = std::cout) const
    {
        fMeta.Print(os);
        if (!printWaveforms) return;

        for (int adc = 0; adc < kNumADCs; ++adc) {
            for (int ch = 0; ch < kNumChannels; ++ch) {
                if (!fMeta.IsValid(adc, ch)) continue;
                const auto& ptr = fWaveformAnas[adc][ch];
                if (ptr) ptr->Print(os);
            }
        }
    }

    friend std::ostream& operator<<(std::ostream& os, const EventAna& e)
    {
        e.Print(false, os);
        return os;
    }

private:
    EventMetadata fMeta;
    std::unique_ptr<MetaWaveformAna> fWaveformAnas[kNumADCs][kNumChannels];
};

} // namespace ndlar_light
