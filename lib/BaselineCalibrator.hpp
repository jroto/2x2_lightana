#pragma once
//
// BaselineCalibrator.hpp
//
// Runs over a Run's events, accumulating Baseline::FindAll() window
// means per (adc, channel), then fits a Gaussian to each channel's
// accumulated distribution to obtain a calibrated baseline mean/sigma.
// This is the "offline calibration" step whose result (ChannelBaseline)
// can be handed to WaveAna as a per-channel fallback baseline for events
// where no in-waveform baseline window is found.
//

#include "Baseline.hpp"
#include "EventMetadata.hpp" // kNumADCs / kNumChannels
#include "Event.hpp"
#include "Run.hpp"

#include "TF1.h"
#include "TFitResultPtr.h"
#include "TH1F.h"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <string>
#include <vector>

namespace ndlar_light {

/// Calibrated baseline (Gaussian fit result) for a single channel.
struct ChannelBaseline {
    double      mean       = 0.0;   // Gaussian mean = calibrated baseline
    double      sigma      = 0.0;   // Gaussian sigma = calibrated noise
    std::size_t n_windows  = 0;     // number of baseline windows accumulated
    bool        calibrated = false; // true if Gaussian fit succeeded
};

/// Tunable parameters for BaselineCalibrator.
struct CalibratorConfig {
    BaselineConfig baseline_cfg;          // passed to Baseline
    std::size_t    max_events      = 1000; // max events to process
    double         fit_range_sigma = 2.0;  // Gaussian fit range: mean +/- N*RMS
};

/// Accumulates per-channel baseline-window means across a Run and fits a
/// Gaussian per channel to obtain a calibrated baseline (mean, sigma).
class BaselineCalibrator {
public:
    explicit BaselineCalibrator(const CalibratorConfig& cfg = CalibratorConfig{})
        : fCfg(cfg), fBaseline(cfg.baseline_cfg) {}

    /// Process up to cfg.max_events from `run` (calls run.Reset() internally).
    /// For each event, for each (adc, ch), runs Baseline::FindAll() and
    /// accumulates all accepted window means into per-channel storage.
    /// After accumulation, fits a Gaussian per channel.
    void Calibrate(Run& run)
    {
        run.Reset();
        std::size_t nProcessed = 0;

        while (run.HasNext() && nProcessed < fCfg.max_events) {
            const Event& event = run.NextEvent();
            ++nProcessed;

            for (int adc = 0; adc < kNumADCs; ++adc) {
                for (int ch = 0; ch < kNumChannels; ++ch) {
                    if (!event.IsValid(adc, ch)) continue;

                    const Waveform& wf = event.GetWaveform(adc, ch);
                    std::vector<double> samples(wf.Size());
                    for (std::size_t s = 0; s < wf.Size(); ++s) samples[s] = wf.GetSample(s);

                    std::vector<BaselineSegment> segs = fBaseline.FindAll(samples);
                    for (const auto& seg : segs) {
                        fSamples[adc][ch].push_back(seg.mean);
                    }
                }
            }
        }

        for (int adc = 0; adc < kNumADCs; ++adc)
            for (int ch = 0; ch < kNumChannels; ++ch)
                FitChannel(adc, ch);
    }

    /// Access the calibrated baseline for a channel.
    const ChannelBaseline& GetBaseline(int adc, int ch) const
    {
        return fResult[adc][ch];
    }

    /// Print a summary table of calibrated baselines to os.
    void Print(std::ostream& os = std::cout) const
    {
        os << std::left
           << std::setw(6) << "ADC" << std::setw(6) << "CH"
           << std::setw(12) << "mean" << std::setw(12) << "sigma"
           << std::setw(10) << "n_wins" << "calibrated\n";
        for (int adc = 0; adc < kNumADCs; ++adc) {
            for (int ch = 0; ch < kNumChannels; ++ch) {
                const ChannelBaseline& b = fResult[adc][ch];
                os << std::left
                   << std::setw(6) << adc << std::setw(6) << ch
                   << std::setw(12) << b.mean << std::setw(12) << b.sigma
                   << std::setw(10) << b.n_windows << b.calibrated << "\n";
            }
        }
    }

private:
    CalibratorConfig fCfg;
    Baseline         fBaseline;
    ChannelBaseline  fResult[kNumADCs][kNumChannels];

    // Raw accumulated baseline means per channel, kept for histogram/fit.
    std::vector<double> fSamples[kNumADCs][kNumChannels];

    /// Fit a Gaussian to fSamples[adc][ch] and fill fResult[adc][ch].
    /// The histogram is built over all samples (outliers NOT removed).
    /// The Gaussian fit is restricted to [mean - N*rms, mean + N*rms]
    /// where mean/rms are computed from the histogram itself,
    /// and N = cfg.fit_range_sigma.
    void FitChannel(int adc, int ch)
    {
        std::vector<double>& samples = fSamples[adc][ch];
        ChannelBaseline& result = fResult[adc][ch];

        if (samples.empty()) {
            result.calibrated = false;
            return;
        }

        double minVal = *std::min_element(samples.begin(), samples.end());
        double maxVal = *std::max_element(samples.begin(), samples.end());

        double loEdge = minVal - 1.0;
        double hiEdge = maxVal + 1.0;
        int nBins = std::max(20, static_cast<int>((maxVal - minVal) * 4));

        std::string hname = "h_baseline_calib_" + std::to_string(adc) + "_" + std::to_string(ch);
        TH1F* h = new TH1F(hname.c_str(), "", nBins, loEdge, hiEdge);
        for (double v : samples) h->Fill(v);

        double h_mean = h->GetMean();
        double h_rms  = h->GetRMS();

        double fit_min = h_mean - fCfg.fit_range_sigma * h_rms;
        double fit_max = h_mean + fCfg.fit_range_sigma * h_rms;

        TFitResultPtr fitResult = h->Fit("gaus", "Q0S", "", fit_min, fit_max);

        TF1* f = h->GetFunction("gaus");
        if (f) {
            result.mean  = f->GetParameter(1);
            result.sigma = f->GetParameter(2);
        }
        result.n_windows  = samples.size();
        result.calibrated = (f != nullptr) && (static_cast<int>(fitResult) == 0);

        delete h;
    }
};

} // namespace ndlar_light
