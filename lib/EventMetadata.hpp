#pragma once
//
// EventMetadata.hpp
//
// Per-event metadata extracted from `/light/events/data` (everything
// except the waveform samples themselves, which live in `/light/wvfm/data`
// and are represented by Waveform/WaveformAna). Shared via composition by
// both Event (raw waveforms) and EventAna (analyzed waveforms) so the
// metadata fields and their accessors are defined exactly once.
//

#include <array>
#include <cstdint>
#include <iomanip>
#include <ostream>

namespace ndlar_light {

/// Number of ADCs in the NDLAr light readout system.
constexpr int kNumADCs = 8;

/// Number of channels per ADC in the NDLAr light readout system.
constexpr int kNumChannels = 64;

/// Metadata for one event, mirroring `/light/events/data` minus the
/// waveform samples (see Waveform.hpp / WaveformAna.hpp for those).
class EventMetadata {
public:
    EventMetadata() = default;

    uint64_t GetId() const { return fId; }
    int32_t GetEventNumber() const { return fEvent; }
    uint8_t GetTriggerType() const { return fTrigType; }

    /// Per-ADC serial number (sn[8]).
    int32_t GetSerialNumber(int adc) const { return fSn[adc]; }

    /// Per-ADC Unix time, milliseconds (utime_ms[8]).
    uint64_t GetUTimeMs(int adc) const { return fUTimeMs[adc]; }

    /// Per-ADC TAI timestamp, nanoseconds (tai_ns[8]).
    uint64_t GetTaiNs(int adc) const { return fTaiNs[adc]; }

    /// Whether (adc, channel) has meaningful waveform data for this event,
    /// per the `wvfm_valid[8][64]` flag in /light/events/data. This is a
    /// distinct concept from Waveform::IsClipped() / WaveformAna's clipped
    /// flag - a channel can be valid-but-clipped, or simply invalid.
    bool IsValid(int adc, int channel) const { return fWvfmValid[adc][channel]; }

    // --- Mutators used by SubRunReader while filling metadata in place. ---

    void SetId(uint64_t id) { fId = id; }
    void SetEventNumber(int32_t event) { fEvent = event; }
    void SetTriggerType(uint8_t t) { fTrigType = t; }
    void SetSerialNumber(int adc, int32_t sn) { fSn[adc] = sn; }
    void SetUTimeMs(int adc, uint64_t t) { fUTimeMs[adc] = t; }
    void SetTaiNs(int adc, uint64_t t) { fTaiNs[adc] = t; }
    void SetValid(int adc, int channel, bool valid) { fWvfmValid[adc][channel] = valid; }

    /// Tabular print of the event-level metadata (id, event number,
    /// trig_type, and a per-ADC table of sn/utime_ms/tai_ns).
    void Print(std::ostream& os = std::cout) const
    {
        os << "Event id=" << fId << " event=" << fEvent
           << " trig_type=" << static_cast<int>(fTrigType) << "\n";
        os << std::left
           << std::setw(6) << "ADC"
           << std::setw(14) << "sn"
           << std::setw(18) << "utime_ms"
           << std::setw(22) << "tai_ns" << "\n";
        for (int adc = 0; adc < kNumADCs; ++adc) {
            os << std::left
               << std::setw(6) << adc
               << std::setw(14) << fSn[adc]
               << std::setw(18) << fUTimeMs[adc]
               << std::setw(22) << fTaiNs[adc] << "\n";
        }
    }

    friend std::ostream& operator<<(std::ostream& os, const EventMetadata& m)
    {
        m.Print(os);
        return os;
    }

private:
    uint64_t fId = 0;
    int32_t fEvent = 0;
    uint8_t fTrigType = 0;

    std::array<int32_t, kNumADCs> fSn = {};
    std::array<uint64_t, kNumADCs> fUTimeMs = {};
    std::array<uint64_t, kNumADCs> fTaiNs = {};

    bool fWvfmValid[kNumADCs][kNumChannels] = {};
};

} // namespace ndlar_light
