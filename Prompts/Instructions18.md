You are working in jroto/2x2_lightana. All source is header-only in lib/.
Read lib/Analysis.hpp and lib/FFTWaveformAna.hpp in full before writing
any code. Do not invent any class or method not present in those files.

────────────────────────────────────────────────────────────────
GOAL
────────────────────────────────────────────────────────────────

Extend LoopFFT to support optional notch filtering in the frequency domain.
When notch frequencies are provided, the display shows four rows per channel:
  Row 1 (top):    raw waveform          (existing top-row drawing, unchanged)
  Row 2:          filtered waveform     (inverse FFT after notch)
  Row 3:          raw avg FFT spectrum  (existing TProfile, unchanged)
  Row 4 (bottom): filtered avg FFT spectrum (new TProfile)

When no notch frequencies are provided (empty vector), LoopFFT behaves
exactly as it does today (2 rows, no change).

────────────────────────────────────────────────────────────────
1. NotchFilter  (new file lib/NotchFilter.hpp)
────────────────────────────────────────────────────────────────

A plain struct + function, no class hierarchy, no ROOT dependency.

struct NotchFilter {
    std::vector<double> frequencies_mhz;  // centre frequencies to suppress
    double half_width_mhz = 0.5;          // half-width of each notch [MHz]
};

Free function:

/// Apply notch filter to a one-sided complex FFT output in-place.
/// `re` and `im` are the real and imaginary parts of the full two-sided
/// FFT output as returned by TVirtualFFT (length N).
/// For each notch centre f_c, all bins k where
///   |freq[k] - f_c| <= half_width  OR  |freq[N-k] - f_c| <= half_width
/// are zeroed (both the positive and negative frequency mirror).
/// freq[k] = k / (N * samplePeriod_ns * 1e-3)  [MHz].
void ApplyNotchFilter(std::vector<double>& re,
                      std::vector<double>& im,
                      int N,
                      double samplePeriod_ns,
                      const NotchFilter& filter);

────────────────────────────────────────────────────────────────
2. FFTWaveformAna  — add filtered waveform output
────────────────────────────────────────────────────────────────

Add an optional second constructor (or extend the existing one) that
accepts a NotchFilter:

  FFTWaveformAna(const Waveform& wf, bool isValid,
                 double samplePeriod_ns,
                 const NotchFilter* notch = nullptr)

When notch != nullptr:
  - After computing the forward FFT, retrieve the full two-sided complex
    output (re[], im[], length N=600) from TVirtualFFT.
  - Call ApplyNotchFilter(re, im, N, samplePeriod_ns, *notch).
  - Compute the filtered one-sided magnitude spectrum from the notched
    re/im (same normalization as the unfiltered spectrum).
  - Perform the inverse FFT (use TVirtualFFT::FFT(1, &N, "C2R M") on the
    notched re/im) to recover the filtered waveform in the time domain.
    Divide each output sample by N (standard FFTW normalization).
  - Store the filtered waveform as std::vector<double> fFilteredSamples
    (length N=600).
  - Store the filtered one-sided magnitude spectrum as
    std::vector<double> fFilteredMagnitudes (length 301).

Add accessors:
  bool HasFilteredWaveform() const;
  const std::vector<double>& FilteredSamples()    const;  // length 600
  const std::vector<double>& FilteredMagnitudes() const;  // length 301

When notch == nullptr, HasFilteredWaveform() returns false and the
filtered vectors are empty.

The existing constructor FFTWaveformAna(wf, isValid, samplePeriod_ns)
must remain unchanged and must continue to compile and work as before.

────────────────────────────────────────────────────────────────
3. Analysis::LoopFFT  — extend signature and canvas layout
────────────────────────────────────────────────────────────────

Change the signature to:

  void LoopFFT(double samplePeriod_ns = 16.0,
               int maxEvents = -1,
               const NotchFilter* notch = nullptr)

When notch == nullptr: behaviour is identical to the current implementation.
Do not change a single line of the existing 2-row logic.

When notch != nullptr:

Canvas layout: N columns × 4 rows (N = number of selected channels).
  Row 1 pads  1..N:    raw waveform          (same drawing as current top row)
  Row 2 pads  N+1..2N: filtered waveform     (new)
  Row 3 pads 2N+1..3N: raw avg FFT TProfile  (same as current bottom row)
  Row 4 pads 3N+1..4N: filtered avg FFT TProfile (new)

For row 2 (filtered waveform pad, per channel i):
  - Construct FFTWaveformAna(wf, true, samplePeriod_ns, notch).
  - If HasFilteredWaveform():
      Build a TH1F from FilteredSamples() (600 bins, x: 0..600).
      Title: "ADC %d / CH %d | filtered;Ticks;ADC counts"
      Line color: kOrange+7 (to distinguish from raw, which is kBlue+1).
      Draw("HIST").
  - Else: draw the same inactive/invalid label as row 1.

For row 4 (filtered avg FFT TProfile, per channel i):
  - Create and manage a second set of TProfile* (fftProfilesFiltered[]),
    using the same lazy-creation pattern as the existing fftProfiles[].
  - Profile name: Form("fft_prof_filt_adc%d_ch%d_loopfft%zu", adc, ch, instance)
  - Fill from FilteredMagnitudes() and Frequencies() (same freq axis).
  - Title: Form("ADC %d / CH %d  avg FFT filtered (N=%d);...", ...)
  - Line color: kOrange+7.
  - Draw("HIST") in row 4 pad.
  - Delete at end of LoopFFT, same as fftProfiles[].

Important: when notch != nullptr, the FFTWaveformAna for the filtered
waveform (row 2) and the one for the raw FFT (row 3) are the SAME object
— construct it once per (event, channel) and reuse it for both rows.
Do not construct FFTWaveformAna twice for the same waveform.

────────────────────────────────────────────────────────────────
4. NDLArLight.hpp
────────────────────────────────────────────────────────────────

Add  #include "NotchFilter.hpp"  before  #include "FFTWaveformAna.hpp".

────────────────────────────────────────────────────────────────
5. Example macro  macros/fft_notch.cpp
────────────────────────────────────────────────────────────────

  #include "../lib/NDLArLight.hpp"
  int main() {
      ndlar_light::Run run("/path/to/data", 1130);
      run.ResetChannels(false);
      run.SelectChannel(0, 4, true);
      run.SelectChannel(0, 5, true);

      // Suppress two known noise lines
      ndlar_light::NotchFilter notch;
      notch.frequencies_mhz = {1.5625, 3.125};  // example: clock harmonics
      notch.half_width_mhz  = 0.3;

      ndlar_light::Analysis ana(run);
      ana.LoopFFT(16.0, 50, &notch);
  }

────────────────────────────────────────────────────────────────
6. Constraints
────────────────────────────────────────────────────────────────

- NotchFilter.hpp has zero ROOT dependency (pure C++ stdlib).
- The existing LoopFFT(samplePeriod_ns, maxEvents) call with no notch
  argument must compile and behave identically to today.
- Follow the existing code style: namespace ndlar_light, function-local
  statics for instance counters, SetDirectory(nullptr) for TProfiles,
  same ROOT naming conventions.
- No new external dependencies.
- List every modified and added file.