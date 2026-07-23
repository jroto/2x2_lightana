#pragma once
//
// WaveformAna.hpp
//
// Analysis results for a single waveform. Constructed from a raw
// Waveform (plus the owning Event's validity flag, since Waveform itself
// doesn't know about validity - that's an Event-level concept); computes
// and stores its analysis results immediately in the constructor.
//
// Results are stored in a generic std::map<std::string, double> rather
// than fixed named fields, so future quantities (RMS, integral, peak
// amplitude, peak time, baseline, ...) can be added without an API
// change. Known keys get a `k<Name>` constant and a typed `Get<Name>()`
// accessor on top of the map.
//

#include "Waveform.hpp"

#include <iomanip>
#include <map>
#include <ostream>
#include <stdexcept>
#include <string>

namespace ndlar_light {

class WaveformAna {
public:
    /// Known analysis parameter keys, to avoid magic strings scattered
    /// across the codebase. Add a new `k<Name>` here (and a matching
    /// `Get<Name>()` below) whenever a new quantity is computed.
    static constexpr const char* kMean = "mean";

    /// Default-constructed, unanalyzed placeholder (adc/channel = -1, no
    /// results). Needed so WaveformAna can live in a fixed-size 8x64
    /// array (see EventAna) before being assigned a real analysis via the
    /// parameterized constructor below.
    WaveformAna() = default;

    /// Analyzes `wf` immediately; `isValid` is Event::IsValid(adc, channel)
    /// for the (adc, channel) this waveform belongs to, passed explicitly
    /// since Waveform has no notion of validity itself.
    WaveformAna(const Waveform& wf, bool isValid)
        : fAdc(wf.GetADC())
        , fChannel(wf.GetChannel())
        , fClipped(wf.IsClipped())
        , fValid(isValid)
    {
        double sum = 0.0;
        for (std::size_t s = 0; s < wf.Size(); ++s) sum += wf.GetSample(s);
        fResults[kMean] = sum / static_cast<double>(wf.Size());
    }

    int GetADC() const { return fAdc; }
    int GetChannel() const { return fChannel; }
    bool IsClipped() const { return fClipped; }
    bool IsValid() const { return fValid; }

    /// Generic access to all computed parameters.
    const std::map<std::string, double>& GetResults() const { return fResults; }

    /// Looks up `key` in the results map. Throws std::out_of_range (with
    /// a message including adc/channel/key) if not present - a missing
    /// key here indicates a real bug (this class always computes the
    /// parameters it advertises), so failing loudly is preferred over
    /// silently returning a sentinel value.
    double Get(const std::string& key) const
    {
        auto it = fResults.find(key);
        if (it == fResults.end()) {
            throw std::out_of_range(
                "WaveformAna::Get: key '" + key + "' not found for ADC " +
                std::to_string(fAdc) + " CH " + std::to_string(fChannel));
        }
        return it->second;
    }

    /// Typed convenience getter for the arithmetic mean of the 600 samples.
    double GetMean() const { return Get(kMean); }

    /// Tabular print: adc/channel, clipped, valid, and all entries
    /// currently in the results map.
    void Print(std::ostream& os = std::cout) const
    {
        os << std::left
           << std::setw(6) << "ADC" << std::setw(6) << "CH"
           << std::setw(10) << "clipped" << std::setw(8) << "valid"
           << "results\n";
        os << std::left
           << std::setw(6) << fAdc << std::setw(6) << fChannel
           << std::setw(10) << fClipped << std::setw(8) << fValid;
        for (const auto& kv : fResults) os << kv.first << "=" << kv.second << " ";
        os << "\n";
    }

    friend std::ostream& operator<<(std::ostream& os, const WaveformAna& wa)
    {
        wa.Print(os);
        return os;
    }

private:
    int fAdc = -1;
    int fChannel = -1;
    bool fClipped = false;
    bool fValid = false;
    std::map<std::string, double> fResults;
};

} // namespace ndlar_light
