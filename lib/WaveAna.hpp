#pragma once
//
// WaveAna.hpp
//
// A second, independent concrete implementation of MetaWaveformAna
// (alongside WaveformAna). Where WaveformAna only computes the sample
// mean, WaveAna runs a baseline-finding + hit-finding analysis: it
// locates flat "baseline" windows in the waveform (see Baseline.hpp),
// derives an overall per-waveform baseline from them (falling back to a
// pre-calibrated ChannelBaseline if no baseline window is found in this
// particular waveform), then finds "hits" - contiguous excursions above
// threshold - via iterative peak removal.
//
// Registers "n_hits", "total_charge", "overall_baseline" in the shared
// MetaWaveformAna parameter registry, alongside whatever WaveformAna (or
// any other analyzer) has already registered.
//

#include "Baseline.hpp"
#include "BaselineCalibrator.hpp"
#include "MetaWaveformAna.hpp"
#include "Waveform.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ndlar_light {

/// One hit found by WaveAna's iterative peak-removal hit finder.
struct Hit {
    int    tick_start;   // first tick of hit (where signal crosses threshold going up)
    int    tick_peak;    // tick of maximum sample
    int    tick_end;     // last tick of hit (where signal crosses threshold going down)
    double amplitude;    // s[tick_peak] - overall_baseline
    double charge;       // sum(s[i] - overall_baseline) for i in [tick_start, tick_end]
};

/// Tunable parameters for WaveAna.
struct WaveAnaConfig {
    BaselineConfig baseline_cfg;         // passed to Baseline
    double         threshold_adc = 5.0;  // hit threshold above baseline (tunable)
};

/// Baseline + hit-finding analysis for a single waveform.
class WaveAna : public MetaWaveformAna {
public:
    // Registry indices
    static std::size_t NHitsIndex()
    {
        static const std::size_t idx = MetaWaveformAna::RegisterParam("n_hits");
        return idx;
    }
    static std::size_t TotalChargeIndex()
    {
        static const std::size_t idx = MetaWaveformAna::RegisterParam("total_charge");
        return idx;
    }
    static std::size_t OverallBaselineIndex()
    {
        static const std::size_t idx = MetaWaveformAna::RegisterParam("overall_baseline");
        return idx;
    }

    /// Default-constructed, unanalyzed placeholder - needed so WaveAna
    /// can live in a fixed-size array before being assigned a real
    /// analysis, mirroring WaveformAna's default constructor.
    WaveAna() = default;

    /// Main constructor. `fallback` may be nullptr if no calibration is
    /// available.
    WaveAna(const Waveform& wf, bool isValid, const WaveAnaConfig& cfg,
            const ChannelBaseline* fallback = nullptr)
        : fAdc(wf.GetADC())
        , fChannel(wf.GetChannel())
        , fClipped(wf.IsClipped())
        , fValid(isValid)
    {
        // Ensure all three parameters are registered before sizing fParams.
        const std::size_t nHitsIdx = NHitsIndex();
        const std::size_t totalChargeIdx = TotalChargeIndex();
        const std::size_t overallBaselineIdx = OverallBaselineIndex();

        fParams.resize(MetaWaveformAna::ParamNames().size(),
                       std::numeric_limits<double>::quiet_NaN());
        std::size_t maxIdx = std::max({nHitsIdx, totalChargeIdx, overallBaselineIdx});
        if (maxIdx >= fParams.size())
            fParams.resize(maxIdx + 1, std::numeric_limits<double>::quiet_NaN());

        RunAnalysis(wf, cfg, fallback);
    }

    // MetaWaveformAna interface
    int    GetADC()     const override { return fAdc; }
    int    GetChannel() const override { return fChannel; }
    bool   IsClipped()  const override { return fClipped; }
    bool   IsValid()    const override { return fValid; }

    bool HasParamIndex(std::size_t i) const override
    {
        if (i >= fParams.size()) return false;
        return !std::isnan(fParams[i]);
    }

    double GetParamByIndex(std::size_t i) const override
    {
        if (!HasParamIndex(i)) {
            throw std::out_of_range(
                "WaveAna::GetParamByIndex: missing index " + std::to_string(i) +
                " for ADC " + std::to_string(fAdc) + " CH " + std::to_string(fChannel));
        }
        return fParams[i];
    }

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

    friend std::ostream& operator<<(std::ostream& os, const WaveAna& wa)
    {
        wa.Print(os);
        return os;
    }

    // WaveAna-specific accessors
    const std::vector<Hit>&             Hits()      const { return fHits; }
    const std::vector<BaselineSegment>& Baselines() const { return fBaselineSegs; }
    double                              OverallBaseline() const { return fOverallBaseline; }

private:
    int    fAdc     = -1;
    int    fChannel = -1;
    bool   fClipped = false;
    bool   fValid   = false;

    std::vector<double>          fParams;
    std::vector<Hit>             fHits;
    std::vector<BaselineSegment> fBaselineSegs;
    double                       fOverallBaseline = 0.0;

    void RunAnalysis(const Waveform& wf, const WaveAnaConfig& cfg, const ChannelBaseline* fallback)
    {
        std::vector<double> working(wf.Size());
        for (std::size_t s = 0; s < wf.Size(); ++s) working[s] = wf.GetSample(s);

        Baseline baseline(cfg.baseline_cfg);
        fBaselineSegs = baseline.FindAll(working);

        if (!fBaselineSegs.empty()) {
            double sum = 0.0;
            for (const auto& seg : fBaselineSegs) sum += seg.mean;
            fOverallBaseline = sum / static_cast<double>(fBaselineSegs.size());
        } else if (fallback != nullptr && fallback->calibrated) {
            fOverallBaseline = fallback->mean;
        } else {
            fOverallBaseline = 0.0;
            std::cout << "WaveAna::RunAnalysis: warning: no baseline window found and "
                      << "no calibrated fallback for ADC " << fAdc << " CH " << fChannel
                      << " - defaulting overall_baseline to 0.\n";
        }

        FindHits(working, fOverallBaseline, cfg.threshold_adc);

        double totalCharge = 0.0;
        for (const auto& hit : fHits) totalCharge += hit.charge;

        fParams[NHitsIndex()]           = static_cast<double>(fHits.size());
        fParams[TotalChargeIndex()]     = totalCharge;
        fParams[OverallBaselineIndex()] = fOverallBaseline;
    }

    void FindHits(std::vector<double>& working, double baseline, double threshold_adc)
    {
        const int N = static_cast<int>(working.size());
        const double cut = baseline;
//        const double cut = baseline + threshold_adc;

        while (true) {
            int i_max = static_cast<int>(
                std::max_element(working.begin(), working.end()) - working.begin());

            if (working[i_max] <= cut) break; // no more hits

            int tick_start = i_max;
            while (tick_start > 0 && working[tick_start - 1] > cut) --tick_start;

            int tick_end = i_max;
            while (tick_end < N - 1 && working[tick_end + 1] > cut) ++tick_end;

            double amplitude = working[i_max] - baseline;
            double charge = 0.0;
            for (int i = tick_start; i <= tick_end; ++i) charge += (working[i] - baseline);

            Hit hit;
            hit.tick_start = tick_start;
            hit.tick_peak  = i_max;
            hit.tick_end   = tick_end;
            hit.amplitude  = amplitude;
            hit.charge     = charge;
            fHits.push_back(hit);

            // remove hit: set samples to baseline (flat line)
            for (int i = tick_start; i <= tick_end; ++i) working[i] = baseline;
        }
    }
};

} // namespace ndlar_light
