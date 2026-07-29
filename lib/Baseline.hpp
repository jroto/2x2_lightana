#pragma once
//
// Baseline.hpp
//
// Finds "baseline" windows in a waveform - contiguous stretches of
// samples that look flat/quiet (small peak-to-peak amplitude, roughly
// symmetric about the mean) - which can then be used to estimate the
// per-waveform or per-channel baseline level. Pure data/algorithm layer,
// no ROOT dependency.
//

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <vector>

namespace ndlar_light {

/// One accepted baseline window.
struct BaselineSegment {
    int    tick_start;  // first tick of this window (inclusive)
    int    tick_end;    // last tick of this window (inclusive)
    double mean;        // mean ADC value in this window
    double std;         // RMS noise in this window
};

/// Tunable parameters for baseline-window acceptance.
struct BaselineConfig {
    int    window_ticks       = 20;   // ticks per candidate window
    double amp_threshold_adc  = 5.0;  // max allowed (max-min) in window
    double asymmetry_factor   = 3.0;  // max allowed AmpBot/AmpTop ratio
};

/// Scans a waveform's samples in non-overlapping windows and reports
/// which windows look like "baseline" (flat, quiet, roughly symmetric).
class Baseline {
public:
    explicit Baseline(const BaselineConfig& cfg = BaselineConfig{}) : fCfg(cfg) {}

    /// Find ALL baseline segments in `samples`.
    /// Scans in non-overlapping windows of cfg.window_ticks.
    /// A window is accepted if:
    ///   Amp = max - min  <=  cfg.amp_threshold_adc
    ///   AmpBot = mean - min  <=  cfg.asymmetry_factor * AmpTop
    ///   where AmpTop = max - mean
    /// Returns all accepted windows as BaselineSegment.
    std::vector<BaselineSegment> FindAll(const std::vector<double>& samples) const
    {
        std::vector<BaselineSegment> result;
        const int N = static_cast<int>(samples.size());
        for (int start = 0; start + fCfg.window_ticks <= N; start += fCfg.window_ticks) {
            BaselineSegment seg;
            if (EvaluateWindow(samples, start, seg)) {
                result.push_back(seg);
            }
        }
        return result;
    }

    /// Find the FIRST accepted baseline window in `samples`,
    /// starting from tick `start_tick`.
    /// Returns true and fills `seg` if found; false otherwise.
    bool FindFirst(const std::vector<double>& samples, int start_tick, BaselineSegment& seg) const
    {
        const int N = static_cast<int>(samples.size());
        for (int start = start_tick; start + fCfg.window_ticks <= N; start += fCfg.window_ticks) {
            if (EvaluateWindow(samples, start, seg)) return true;
        }
        return false;
    }

    const BaselineConfig& Config() const { return fCfg; }

private:
    BaselineConfig fCfg;

    /// Evaluate one window [start, start + window_ticks).
    /// Returns true and fills seg if the window passes both criteria.
    bool EvaluateWindow(const std::vector<double>& samples, int start, BaselineSegment& seg) const
    {
        const int end = start + fCfg.window_ticks; // exclusive
        const std::size_t n = static_cast<std::size_t>(fCfg.window_ticks);

        double sum = std::accumulate(samples.begin() + start, samples.begin() + end, 0.0);
        double mean = sum / static_cast<double>(n);

        double sumSq = std::inner_product(samples.begin() + start, samples.begin() + end,
                                            samples.begin() + start, 0.0);
        double meanSq = sumSq / static_cast<double>(n);
        double variance = meanSq - mean * mean;
        double stddev = variance > 0.0 ? std::sqrt(variance) : 0.0;

        auto minmax = std::minmax_element(samples.begin() + start, samples.begin() + end);
        double minVal = *minmax.first;
        double maxVal = *minmax.second;

        double amp = maxVal - minVal;
        double ampTop = maxVal - mean;
        double ampBot = mean - minVal;

        bool ok = (amp <= fCfg.amp_threshold_adc) &&
                  (ampBot <= fCfg.asymmetry_factor * ampTop);

        if (!ok) return false;

        seg.tick_start = start;
        seg.tick_end   = start + fCfg.window_ticks - 1;
        seg.mean       = mean;
        seg.std        = stddev;
        return true;
    }
};

} // namespace ndlar_light
