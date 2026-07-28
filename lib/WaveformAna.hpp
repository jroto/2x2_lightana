#pragma once
//
// WaveformAna.hpp
//
// Analysis results for a single waveform. Constructed from a raw
// Waveform (plus the owning Event's validity flag, since Waveform itself
// doesn't know about validity - that's an Event-level concept); computes
// and stores its analysis results immediately in the constructor.
//
// Results are stored in a dense std::vector<double> indexed by the
// shared MetaWaveformAna parameter registry (see MetaWaveformAna.hpp),
// rather than a std::map<std::string,double> per instance - this avoids
// repeating the same key strings/map overhead for every one of the
// 8*64 waveforms per event. Known parameters get a
// `static const std::size_t k<Name>Index` (registered once via
// MetaWaveformAna::RegisterParam) plus a typed `Get<Name>()` accessor
// built on top of the index-based API.
//

#include "MetaWaveformAna.hpp"
#include "Waveform.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ndlar_light {

class WaveformAna : public MetaWaveformAna {
public:
    /// Name and registry index for the "mean" parameter. Add a new
    /// `kFooName`/`kFooIndex` pair here (and a matching `GetFoo()` below)
    /// whenever a new quantity is computed.
    static constexpr const char* kMeanName = "mean";

    /// Function-local-static accessor for the "mean" registry index.
    /// Cling/ACLiC don't guarantee inline static member initializers run
    /// before first use, so registration is instead forced on first call
    /// to this function (mirrors MetaWaveformAna::MutableRegistry()).
    static std::size_t MeanIndex()
    {
        static const std::size_t idx = MetaWaveformAna::RegisterParam(kMeanName);
        return idx;
    }

    /// Default-constructed, unanalyzed placeholder (adc/channel = -1, no
    /// params set). Needed so WaveformAna can live in a fixed-size 8x64
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
        const std::size_t meanIdx = MeanIndex(); // ensures "mean" is registered

        // Sized to the full registry (not just this class's own params)
        // so indices from any other registered analyzer are always
        // valid to query (HasParamIndex() just returns false for them).
        fParams.resize(MetaWaveformAna::ParamNames().size(),
                        std::numeric_limits<double>::quiet_NaN());

        double sum = 0.0;
        for (std::size_t s = 0; s < wf.Size(); ++s) sum += wf.GetSample(s);
        double mean = sum / static_cast<double>(wf.Size());

        if (meanIdx >= fParams.size()) fParams.resize(meanIdx + 1, std::numeric_limits<double>::quiet_NaN());
        fParams[meanIdx] = mean;
    }

    int GetADC() const override { return fAdc; }
    int GetChannel() const override { return fChannel; }
    bool IsClipped() const override { return fClipped; }
    bool IsValid() const override { return fValid; }

    bool HasParamIndex(std::size_t index) const override
    {
        if (index >= fParams.size()) return false;
        return !std::isnan(fParams[index]);
    }

    /// Value stored for parameter `index`. Throws std::out_of_range (with
    /// a message including adc/channel/index) if not present - a missing
    /// value here indicates a real bug (this class always computes the
    /// parameters it advertises), so failing loudly is preferred over
    /// silently returning a sentinel value.
    double GetParamByIndex(std::size_t index) const override
    {
        if (!HasParamIndex(index)) {
            throw std::out_of_range(
                "WaveformAna::GetParamByIndex: missing index " + std::to_string(index) +
                " for ADC " + std::to_string(fAdc) + " CH " + std::to_string(fChannel));
        }
        return fParams[index];
    }

    /// Typed convenience getter for the arithmetic mean of the 600 samples.
    double GetMean() const { return GetParamByIndex(MeanIndex()); }

    /// Tabular print: adc/channel, clipped, valid, and all currently-set
    /// parameters (by name, via the shared registry).
    void Print(std::ostream& os = std::cout) const override
    {
        os << std::left
           << std::setw(6) << "ADC" << std::setw(6) << "CH"
           << std::setw(10) << "clipped" << std::setw(8) << "valid"
           << "results\n";
        os << std::left
           << std::setw(6) << fAdc << std::setw(6) << fChannel
           << std::setw(10) << fClipped << std::setw(8) << fValid;
        const auto& names = MetaWaveformAna::ParamNames();
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (HasParamIndex(i)) os << names[i] << "=" << fParams[i] << " ";
        }
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
    std::vector<double> fParams;
};

} // namespace ndlar_light
