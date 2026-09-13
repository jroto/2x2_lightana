Prompt: Add Baseline, BaselineCalibrator, and WaveAna classes
Context

    Namespace: ndlar_light. Header-only, ROOT/ACLiC compatible (#pragma once).
    kNumADCs = 8, kNumChannels = 64, kNumSamples = 600.
    Waveform exposes GetSample(i), Size(), GetADC(), GetChannel(), IsClipped().
    MetaWaveformAna is the abstract base for per-waveform analysis results, with a shared parameter registry (RegisterParam, ParamNames, HasParamIndex, GetParamByIndex).
    WaveformAna is the existing concrete implementation (computes only mean). The new WaveAna is a separate, independent concrete implementation of MetaWaveformAna — it does not replace or modify WaveformAna.
    Run exposes Reset(), HasNext(), NextEvent().
    Analysis uses a WaveformAnaFactory — the user can plug in WaveAna by passing a custom factory.
    Analysis::Loop() already uses fFactory to build analyzers. It should detect WaveAna via dynamic_cast and draw baselines and hits on top of the waveform.
    NDLArLight.hpp is the umbrella header — add #include "Baseline.hpp", #include "BaselineCalibrator.hpp", #include "WaveAna.hpp" there.

1. New file lib/Baseline.hpp
1a. Data structures

cpp

struct BaselineSegment {
    int    tick_start;  // first tick of this window (inclusive)
    int    tick_end;    // last tick of this window (inclusive)
    double mean;        // mean ADC value in this window
    double std;         // RMS noise in this window
};

1b. Config struct

cpp

struct BaselineConfig {
    int    window_ticks       = 20;   // ticks per candidate window
    double amp_threshold_adc  = 5.0;  // max allowed (max-min) in window
    double asymmetry_factor   = 3.0;  // max allowed AmpBot/AmpTop ratio
};

1c. Baseline class

cpp

class Baseline {
public:
    explicit Baseline(const BaselineConfig& cfg = BaselineConfig{});

    /// Find ALL baseline segments in `samples`.
    /// Scans in non-overlapping windows of cfg.window_ticks.
    /// A window is accepted if:
    ///   Amp = max - min  <=  cfg.amp_threshold_adc
    ///   AmpBot = mean - min  <=  cfg.asymmetry_factor * AmpTop
    ///   where AmpTop = max - mean
    /// Returns all accepted windows as BaselineSegment.
    std::vector<BaselineSegment>
    FindAll(const std::vector<double>& samples) const;

    /// Find the FIRST accepted baseline window in `samples`,
    /// starting from tick `start_tick`.
    /// Returns true and fills `seg` if found; false otherwise.
    bool FindFirst(const std::vector<double>& samples,
                   int start_tick,
                   BaselineSegment& seg) const;

    const BaselineConfig& Config() const { return fCfg; }

private:
    BaselineConfig fCfg;

    /// Evaluate one window [start, start + window_ticks).
    /// Returns true and fills seg if the window passes both criteria.
    bool EvaluateWindow(const std::vector<double>& samples,
                        int start,
                        BaselineSegment& seg) const;
};

1d. Implementation notes for EvaluateWindow

For window [start, start + window_ticks):

    Compute mean using std::accumulate.
    Compute std (RMS) using std::inner_product:
    std=1N∑si2−mean2
    std=N1​∑si2​−mean2
    ​
    Find min and max using std::minmax_element.
    Compute:
        Amp = max - min
        AmpTop = max - mean
        AmpBot = mean - min
    Accept if:
        Amp <= cfg.amp_threshold_adc
        AmpBot <= cfg.asymmetry_factor * AmpTop
    If accepted, fill seg:
        tick_start = start
        tick_end = start + window_ticks - 1
        mean, std as computed.

2. New file lib/BaselineCalibrator.hpp
2a. Data structures

cpp

struct ChannelBaseline {
    double      mean       = 0.0;   // Gaussian mean = calibrated baseline
    double      sigma      = 0.0;   // Gaussian sigma = calibrated noise
    std::size_t n_windows  = 0;     // number of baseline windows accumulated
    bool        calibrated = false; // true if Gaussian fit succeeded
};

2b. Config struct

cpp

struct CalibratorConfig {
    BaselineConfig baseline_cfg;         // passed to Baseline
    std::size_t    max_events    = 1000; // max events to process
    double         fit_range_sigma = 2.0;// Gaussian fit range: mean ± N*RMS
};

2c. BaselineCalibrator class

cpp

class BaselineCalibrator {
public:
    explicit BaselineCalibrator(const CalibratorConfig& cfg = CalibratorConfig{});

    /// Process up to cfg.max_events from `run` (calls run.Reset() internally).
    /// For each event, for each (adc, ch), runs Baseline::FindAll() and
    /// accumulates all accepted window means into per-channel storage.
    /// After accumulation, fits a Gaussian per channel.
    void Calibrate(Run& run);

    /// Access the calibrated baseline for a channel.
    const ChannelBaseline& GetBaseline(int adc, int ch) const;

    /// Print a summary table of calibrated baselines to os.
    void Print(std::ostream& os = std::cout) const;

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
    void FitChannel(int adc, int ch);
};

2d. Implementation notes for FitChannel

    If fSamples[adc][ch] is empty, leave fResult[adc][ch].calibrated = false and return.
    Build a TH1F histogram from fSamples[adc][ch]:
        Range: [min_sample - 1, max_sample + 1].
        Number of bins: reasonable resolution, e.g. max(20, (max-min)*4).
        Fill all samples (no exclusion).
    Compute initial estimates from the histogram:
        h_mean = h->GetMean()
        h_rms = h->GetRMS()
    Restrict the Gaussian fit range:
        fit_min = h_mean - cfg.fit_range_sigma * h_rms
        fit_max = h_mean + cfg.fit_range_sigma * h_rms
    Fit a Gaussian (TF1("gaus")) in [fit_min, fit_max] using h->Fit("gaus", "Q0", "", fit_min, fit_max).
    Extract:
        fResult[adc][ch].mean = f->GetParameter(1) (Gaussian mean).
        fResult[adc][ch].sigma = f->GetParameter(2) (Gaussian sigma).
        fResult[adc][ch].n_windows = fSamples[adc][ch].size().
        fResult[adc][ch].calibrated = true if fit converged (check TFitResultPtr status).
    Do not delete the histogram until after extracting results. Clean up with delete h.

3. New file lib/WaveAna.hpp
3a. Data structures

cpp

struct Hit {
    int    tick_start;   // first tick of hit (where signal crosses threshold going up)
    int    tick_peak;    // tick of maximum sample
    int    tick_end;     // last tick of hit (where signal crosses threshold going down)
    double amplitude;    // s[tick_peak] - overall_baseline
    double charge;       // sum(s[i] - overall_baseline) for i in [tick_start, tick_end]
};

3b. Config struct

cpp

struct WaveAnaConfig {
    BaselineConfig baseline_cfg;       // passed to Baseline
    double         threshold_adc = 5.0; // hit threshold above baseline (tunable)
};

3c. WaveAna class

Inherits MetaWaveformAna. Registers the following parameters in the shared registry:

    "n_hits" — number of hits found.
    "total_charge" — sum of all hit charges.
    "overall_baseline" — baseline value used for hit finding.

cpp

class WaveAna : public MetaWaveformAna {
public:
    // Registry indices
    static std::size_t NHitsIndex() {
        static const std::size_t idx = MetaWaveformAna::RegisterParam("n_hits");
        return idx;
    }
    static std::size_t TotalChargeIndex() {
        static const std::size_t idx = MetaWaveformAna::RegisterParam("total_charge");
        return idx;
    }
    static std::size_t OverallBaselineIndex() {
        static const std::size_t idx = MetaWaveformAna::RegisterParam("overall_baseline");
        return idx;
    }

    WaveAna() = default;

    /// Main constructor.
    /// `fallback` may be nullptr if no calibration is available.
    WaveAna(const Waveform& wf,
            bool isValid,
            const WaveAnaConfig& cfg,
            const ChannelBaseline* fallback = nullptr);

    // MetaWaveformAna interface
    int    GetADC()     const override { return fAdc; }
    int    GetChannel() const override { return fChannel; }
    bool   IsClipped()  const override { return fClipped; }
    bool   IsValid()    const override { return fValid; }
    bool   HasParamIndex(std::size_t i) const override;
    double GetParamByIndex(std::size_t i) const override;
    void   Print(std::ostream& os = std::cout) const override;

    // WaveAna-specific accessors
    const std::vector<Hit>&              Hits()      const { return fHits; }
    const std::vector<BaselineSegment>&  Baselines() const { return fBaselineSegs; }
    double                               OverallBaseline() const { return fOverallBaseline; }

private:
    int    fAdc     = -1;
    int    fChannel = -1;
    bool   fClipped = false;
    bool   fValid   = false;

    std::vector<double>          fParams;
    std::vector<Hit>             fHits;
    std::vector<BaselineSegment> fBaselineSegs;
    double                       fOverallBaseline = 0.0;

    void RunAnalysis(const Waveform& wf,
                     const WaveAnaConfig& cfg,
                     const ChannelBaseline* fallback);

    void FindHits(std::vector<double>& working,
                  double baseline,
                  double threshold_adc);
};

3d. Implementation notes for RunAnalysis

    Convert waveform to std::vector<double> working (copy of samples).
    Run Baseline::FindAll(working) → fBaselineSegs.
    Compute fOverallBaseline:
        If fBaselineSegs is non-empty:
        fOverallBaseline=1Nsegs∑ksegk.mean
        fOverallBaseline=Nsegs​1​k∑​segk​.mean
        Else if fallback != nullptr && fallback->calibrated:
            fOverallBaseline = fallback->mean
        Else:
            fOverallBaseline = 0.0 (last resort, log a warning).
    Call FindHits(working, fOverallBaseline, cfg.threshold_adc).
    Fill fParams:
        fParams[NHitsIndex()] = fHits.size()
        fParams[TotalChargeIndex()] = sum of all hit charges
        fParams[OverallBaselineIndex()] = fOverallBaseline

3e. Implementation notes for FindHits

Iterative peak-removal algorithm:

text

loop:
    find i_max = argmax(working[i])
    if working[i_max] <= baseline + threshold_adc: break  // no more hits

    // walk backwards to find tick_start
    tick_start = i_max
    while tick_start > 0 and working[tick_start - 1] > baseline + threshold_adc:
        tick_start--

    // walk forwards to find tick_end
    tick_end = i_max
    while tick_end < N-1 and working[tick_end + 1] > baseline + threshold_adc:
        tick_end++

    // compute hit properties
    amplitude = working[i_max] - baseline
    charge    = sum(working[i] - baseline for i in [tick_start, tick_end])

    fHits.push_back({tick_start, i_max, tick_end, amplitude, charge})

    // remove hit: set samples to baseline (flat line)
    for i in [tick_start, tick_end]:
        working[i] = baseline

end loop

4. Modify Analysis::Loop() in lib/Analysis.hpp

Inside the per-channel drawing loop, replace the hardcoded WaveformAna wa(wf, true) with fFactory:

cpp

auto ptr = fFactory(wf, event.IsValid(adc, ch));
WaveAna* wa = dynamic_cast<WaveAna*>(ptr.get());

If wa != nullptr (factory is producing WaveAna):

    Draw waveform TH1F as before.
    For each BaselineSegment in wa->Baselines():
        Draw a TLine from (seg.tick_start, seg.mean) to (seg.tick_end, seg.mean) in green (kGreen+2), line width 2.
    For each Hit in wa->Hits():
        Draw a TBox from (hit.tick_start, baseline) to (hit.tick_end, baseline + hit.amplitude) in semi-transparent red (kRed, fill style 3003).
        Draw a TMarker at (hit.tick_peak, wf.GetSample(hit.tick_peak)) in red.
    Overlay parameters TPaveText as before (will now include n_hits, total_charge, overall_baseline).

If wa == nullptr (other factory): fall back to current behavior unchanged.
5. Update lib/NDLArLight.hpp

Add:

cpp

#include "Baseline.hpp"
#include "BaselineCalibrator.hpp"
#include "WaveAna.hpp"

Summary of files
File	Change
lib/Baseline.hpp	New — BaselineSegment, BaselineConfig, Baseline
lib/BaselineCalibrator.hpp	New — ChannelBaseline, CalibratorConfig, BaselineCalibrator
lib/WaveAna.hpp	New — Hit, WaveAnaConfig, WaveAna
lib/Analysis.hpp	Modify Loop() to detect WaveAna and draw baselines + hits
lib/NDLArLight.hpp	Add three new includes
Everything else	No change