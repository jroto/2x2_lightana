You are working in the repository jroto/2x2_lightana. All source files are
header-only and live in lib/. The umbrella header is lib/NDLArLight.hpp.
The build system is a single g++ invocation in the file Compile at the
repository root. There is no CMake.

Read the following files before writing any code:
  lib/Analysis.hpp          — contains Analysis, WaveformAnaFactory, Loop, Loop2
  lib/MetaWaveformAna.hpp   — abstract base for all per-waveform analyzers
  lib/WaveformAna.hpp       — simple concrete analyzer (computes mean)
  lib/WaveAna.hpp           — full concrete analyzer (baseline + hits)
  lib/Waveform.hpp          — raw waveform: int16_t[600], GetSample(), Data()
  lib/Run.hpp               — Run, GetSelectedChannels(), HasNext(), NextEvent()
  lib/Utils.hpp             — PauseExecution(), MyColors palette
  lib/NDLArLight.hpp        — umbrella include

Do not invent any file, class, or method that is not in those files.

────────────────────────────────────────────────────────────────
TASK
────────────────────────────────────────────────────────────────

Add per-waveform FFT analysis to the framework, consisting of:

  1. A new concrete analyzer class  FFTWaveformAna  (new file lib/FFTWaveformAna.hpp)
  2. A new interactive display method  Analysis::LoopFFT  added to lib/Analysis.hpp
  3. An example macro  macros/fft_loop.cpp
  4. lib/NDLArLight.hpp updated to #include "FFTWaveformAna.hpp"

────────────────────────────────────────────────────────────────
1. FFTWaveformAna  (lib/FFTWaveformAna.hpp)
────────────────────────────────────────────────────────────────

Inherits from MetaWaveformAna exactly as WaveformAna and WaveAna do.

Constructor signature:
  FFTWaveformAna(const Waveform& wf, bool isValid, double samplePeriod_ns)

where samplePeriod_ns is the ADC hardware sampling period in nanoseconds
(16.0 ns for DAPHNE; passed by the caller, not hardcoded).

On construction, immediately compute:
  - DC subtraction: subtract the arithmetic mean of all kNumSamples = 600
    samples from each sample before the FFT.
  - Real-to-complex FFT of the 600 DC-subtracted samples using ROOT's
    TVirtualFFT::FFT(1, &N, "R2C M") interface.
  - One-sided magnitude spectrum: retain bins 0..300 (N/2 inclusive).
    Apply the standard factor-of-2 normalization to bins 1..299 (not DC,
    not Nyquist) so that the magnitude represents the true single-sided
    amplitude. Divide all bins by N to normalize by transform length.
  - Frequency axis: freq[k] = k / (N * samplePeriod_ns * 1e-3)  [MHz].
    Store both the magnitude vector and the frequency vector as
    std::vector<double> members, accessible via:
      const std::vector<double>& Magnitudes() const;
      const std::vector<double>& Frequencies() const;  // in MHz
      int    NumFFTBins()       const;  // = 301
      double SamplePeriod_ns()  const;

Register the following parameters in the MetaWaveformAna shared registry
(using the same function-local-static pattern as WaveformAna::MeanIndex()):
  "fft_peak_freq_mhz"   — frequency [MHz] of the bin with maximum magnitude
  "fft_peak_mag"        — magnitude at that bin
  "fft_dc_offset"       — the mean that was subtracted before the FFT

Implement all pure-virtual methods: GetADC, GetChannel, IsClipped, IsValid,
HasParamIndex, GetParamByIndex, Print.

Handle edge cases silently (store NaN / empty vectors):
  - wf.Size() < 2
  - samplePeriod_ns <= 0

────────────────────────────────────────────────────────────────
2. Analysis::LoopFFT  (added to lib/Analysis.hpp)
────────────────────────────────────────────────────────────────

Method signature:
  void LoopFFT(double samplePeriod_ns = 16.0, int maxEvents = -1)

Behaviour mirrors Loop2 exactly, with these differences:

Canvas layout:
  N columns × 2 rows, where N = number of selected channels
  (use fRun.GetSelectedChannels(), exactly as Loop2 does).
  Top row    pads 1..N      — waveform display (identical to Loop2 top row)
  Bottom row pads N+1..2N   — running-average FFT magnitude spectrum

Top row: copy the Loop2 top-row drawing code without modification.
  Use the same waveform TH1F, same title format, same baseline/hit overlays
  (dynamic_cast<WaveAna*>), same TPaveText parameter overlay, same
  gPad->Clear() / gPad->Update() pattern.
  Construct the per-waveform analyzer with:
    auto ptr = std::make_unique<FFTWaveformAna>(wf, true, samplePeriod_ns);
  (Do not use fFactory here — FFTWaveformAna is not the registered factory
  type. The factory is still used for the WaveAna overlays via a separate
  fFactory(wf, true) call, exactly as Loop2 does.)

Bottom row — running-average FFT using TProfile:
  Before the event loop, create one TProfile per selected channel:
    - name:   Form("fft_prof_adc%d_ch%d_loopfft%zu", adc, ch, instance)
    - title:  Form("ADC %d / CH %d  avg FFT  (N=0);Frequency [MHz];Magnitude [ADC/bin]", adc, ch)
    - bins:   301, xlow = freq[0], xhigh = freq[300] + bin_width
              where freq[k] and bin_width come from the first valid FFT
              computed for that channel; defer TProfile creation until the
              first valid waveform if the frequency axis is not yet known.
    - option: "" (default, stores mean and error)
    - SetDirectory(nullptr)

  For each event, for each selected channel with a valid waveform:
    - Construct FFTWaveformAna for that waveform.
    - Fill the TProfile: for each bin k in 0..300, call
        prof->Fill(freq[k], mag[k])
    - Update the title to show the current accumulated count N.
    - Redraw the TProfile in the bottom pad with Draw("HIST").

  Use a static instance counter (same pattern as Loop2's sLoop2Instance)
  to avoid ROOT name collisions across multiple LoopFFT calls.

  At the end of LoopFFT, delete all TProfile objects.

Navigation, canvas title, and PauseExecution call: identical to Loop2.

────────────────────────────────────────────────────────────────
3. Example macro  macros/fft_loop.cpp
────────────────────────────────────────────────────────────────

Minimal working example:

  #include "../lib/NDLArLight.hpp"
  int main() {
      ndlar_light::Run run("/path/to/data", 1130);
      run.ResetChannels(false);
      run.SelectChannel(0, 4, true);
      run.SelectChannel(0, 5, true);
      ndlar_light::Analysis ana(run);
      ana.LoopFFT(16.0, 50);   // 16 ns sampling, show 50 events
  }

────────────────────────────────────────────────────────────────
4. Constraints
────────────────────────────────────────────────────────────────

- Do not rename or modify Loop or Loop2.
- Do not modify any existing class interface.
- Do not introduce any dependency beyond ROOT and the existing HDF5/HighFive.
- All new code lives in lib/FFTWaveformAna.hpp and the additions to
  lib/Analysis.hpp and lib/NDLArLight.hpp.
- Follow the existing code style: namespace ndlar_light, doxygen-style //
  comments, function-local statics for registry indices, SetDirectory(nullptr)
  for persistent histograms, same ROOT object naming conventions.
- No unnecessary copies of the waveform sample array: use wf.Data() for
  bulk reads where possible.

────────────────────────────────────────────────────────────────
5. Deliverables
────────────────────────────────────────────────────────────────

1. Complete content of lib/FFTWaveformAna.hpp
2. The LoopFFT method to paste into lib/Analysis.hpp (with its doc comment)
3. The updated #include line for lib/NDLArLight.hpp
4. macros/fft_loop.cpp
5. A brief note on any ROOT TVirtualFFT ownership/cleanup subtlety to be
   aware of.