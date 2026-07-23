#pragma once
//
// EventAna.hpp
//
// Analyzed counterpart to Event: shares EventMetadata (via composition,
// same pattern as Event) but holds an 8x64 matrix of WaveformAna instead
// of raw Waveform. Structurally parallel to Event:
//   Event    = EventMetadata + raw Waveform matrix
//   EventAna = EventMetadata + analyzed WaveformAna matrix
//

#include "EventMetadata.hpp"
#include "WaveformAna.hpp"

#include <ostream>

namespace ndlar_light {

class EventAna {
public:
    EventAna() = default;

    EventMetadata& Meta() { return fMeta; }
    const EventMetadata& Meta() const { return fMeta; }

    const WaveformAna& GetWaveformAna(int adc, int channel) const {
        return fWaveformAnas[adc][channel];
    }
    WaveformAna& MutableWaveformAna(int adc, int channel) {
        return fWaveformAnas[adc][channel];
    }

    /// Prints event metadata (always) and, if `printWaveforms` is true,
    /// each valid WaveformAna (mirrors Event::Print).
    void Print(bool printWaveforms = false, std::ostream& os = std::cout) const
    {
        fMeta.Print(os);
        if (!printWaveforms) return;

        for (int adc = 0; adc < kNumADCs; ++adc) {
            for (int ch = 0; ch < kNumChannels; ++ch) {
                if (!fMeta.IsValid(adc, ch)) continue;
                fWaveformAnas[adc][ch].Print(os);
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
    WaveformAna fWaveformAnas[kNumADCs][kNumChannels];
};

} // namespace ndlar_light
