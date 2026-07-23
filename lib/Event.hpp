#pragma once
//
// Event.hpp
//
// Represents one row merged from `/light/events/data` and
// `/light/wvfm/data`: per-event metadata (EventMetadata, held by
// composition) plus an 8x64 matrix of Waveform objects (one per
// ADC/channel).
//
// Event instances are meant to be reused (filled in place) by Run /
// SubRunReader as iteration proceeds, rather than reallocated per event,
// since each event is ~600 KB (8 * 64 * 600 * 2 bytes of samples alone).
//

#include "EventMetadata.hpp"
#include "Waveform.hpp"

#include <ostream>

namespace ndlar_light {

/// One event (one row of `/light/events/data` merged with the
/// corresponding row of `/light/wvfm/data`).
class Event {
public:
    Event() = default;

    /// Direct access to the metadata sub-object.
    EventMetadata& Meta() { return fMeta; }
    const EventMetadata& Meta() const { return fMeta; }

    // --- Metadata accessors, delegating to fMeta (kept for existing
    // callers - SubRunReader, Run, example_loop.C - so this refactor
    // doesn't break anything). ---

    uint64_t GetId() const { return fMeta.GetId(); }
    int32_t GetEventNumber() const { return fMeta.GetEventNumber(); }
    uint8_t GetTriggerType() const { return fMeta.GetTriggerType(); }
    int32_t GetSerialNumber(int adc) const { return fMeta.GetSerialNumber(adc); }
    uint64_t GetUTimeMs(int adc) const { return fMeta.GetUTimeMs(adc); }
    uint64_t GetTaiNs(int adc) const { return fMeta.GetTaiNs(adc); }
    bool IsValid(int adc, int channel) const { return fMeta.IsValid(adc, channel); }

    // --- Waveform data from /light/wvfm/data ---

    const Waveform& GetWaveform(int adc, int channel) const {
        return fWaveforms[adc][channel];
    }

    // --- Mutators used by SubRunReader while filling this Event in place. ---

    void SetId(uint64_t id) { fMeta.SetId(id); }
    void SetEventNumber(int32_t event) { fMeta.SetEventNumber(event); }
    void SetTriggerType(uint8_t t) { fMeta.SetTriggerType(t); }
    void SetSerialNumber(int adc, int32_t sn) { fMeta.SetSerialNumber(adc, sn); }
    void SetUTimeMs(int adc, uint64_t t) { fMeta.SetUTimeMs(adc, t); }
    void SetTaiNs(int adc, uint64_t t) { fMeta.SetTaiNs(adc, t); }
    void SetValid(int adc, int channel, bool valid) { fMeta.SetValid(adc, channel, valid); }
    Waveform& MutableWaveform(int adc, int channel) { return fWaveforms[adc][channel]; }

    /// Prints event metadata (always) and, if `printWaveforms` is true,
    /// each *valid* waveform (invalid (adc, channel) slots are skipped
    /// rather than printing mostly-empty entries).
    void Print(bool printWaveforms = false, int maxSamplesPerWaveform = 5,
               std::ostream& os = std::cout) const
    {
        fMeta.Print(os);
        if (!printWaveforms) return;

        for (int adc = 0; adc < kNumADCs; ++adc) {
            for (int ch = 0; ch < kNumChannels; ++ch) {
                if (!fMeta.IsValid(adc, ch)) continue;
                fWaveforms[adc][ch].Print(os, maxSamplesPerWaveform);
            }
        }
    }

    friend std::ostream& operator<<(std::ostream& os, const Event& e)
    {
        e.Print(false, 5, os);
        return os;
    }

private:
    EventMetadata fMeta;
    Waveform fWaveforms[kNumADCs][kNumChannels];
};

} // namespace ndlar_light
