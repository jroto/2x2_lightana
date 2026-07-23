#pragma once
//
// Waveform.hpp
//
// Plain data holder for a single (ADC, channel) waveform read from the
// NDLAr light-system `/light/wvfm/data` dataset. No analysis logic here
// (no pedestal subtraction, no peak finding, etc.) - that belongs in a
// separate processing layer built on top of this.
//

#include <cstdint>
#include <cstddef>
#include <iomanip>
#include <ostream>
#include <algorithm>

namespace ndlar_light {

/// Number of ADC time samples per waveform, per the NDLAr light readout
/// schema (`/light/wvfm/data` : samples[8][64][600]).
constexpr std::size_t kNumSamples = 600;

/// Raw storage for one waveform: the ADC samples for a single (ADC index,
/// channel index) pair, plus the `clipped` flag from the HDF5 file and the
/// indices identifying which ADC/channel this waveform belongs to.
///
/// `clipped` and "valid" are different concepts in this data format:
///   - `clipped` (this class) says the waveform saturated the ADC range.
///   - "valid" (see Event::IsValid) says whether this (adc, channel) slot
///     actually has meaningful data for this event at all.
/// A channel can be valid but clipped, or invalid (and clipped/unclipped
/// is meaningless in that case).
class Waveform {
public:
    Waveform() = default;

    /// ADC index this waveform belongs to (0..7).
    int GetADC() const { return fAdc; }

    /// Channel index this waveform belongs to (0..63).
    int GetChannel() const { return fChannel; }

    /// Whether the HDF5 `clipped` flag was set (ADC saturation) for this
    /// waveform.
    bool IsClipped() const { return fClipped; }

    /// Number of ADC samples in this waveform (always kNumSamples).
    std::size_t Size() const { return kNumSamples; }

    /// Raw sample access, no bounds checking (hot path).
    int16_t GetSample(std::size_t i) const { return fSamples[i]; }

    /// Direct pointer to the underlying sample array (read-only), useful
    /// for bulk operations (e.g. filling a TH1D from a range of samples).
    const int16_t* Data() const { return fSamples; }

    // --- Filled by SubRunReader; not intended to be set by users directly. ---
    void SetADC(int adc) { fAdc = adc; }
    void SetChannel(int channel) { fChannel = channel; }
    void SetClipped(bool clipped) { fClipped = clipped; }
    int16_t* MutableData() { return fSamples; }

    /// Tabular print: adc/channel, clipped flag, and the first
    /// `maxSamples` samples (600 by default is unreadable, so this caps
    /// output for interactive/console use).
    void Print(std::ostream& os = std::cout, int maxSamples = 10) const
    {
        int n = std::min<int>(maxSamples, static_cast<int>(kNumSamples));
        os << std::left
           << std::setw(6) << "ADC" << std::setw(6) << "CH" << std::setw(10) << "clipped"
           << "samples[0:" << n << "]\n";
        os << std::left
           << std::setw(6) << fAdc << std::setw(6) << fChannel
           << std::setw(10) << fClipped;
        for (int s = 0; s < n; ++s) os << fSamples[s] << " ";
        os << "...\n";
    }

    friend std::ostream& operator<<(std::ostream& os, const Waveform& wf)
    {
        wf.Print(os);
        return os;
    }

private:
    int16_t fSamples[kNumSamples] = {};
    bool fClipped = false;
    int fAdc = -1;
    int fChannel = -1;
};

} // namespace ndlar_light
