#pragma once
//
// FFTWaveformAna.hpp
//
// FFT-based per-waveform analysis. Inherits from MetaWaveformAna and
// computes the real-to-complex FFT of DC-subtracted waveforms, storing
// one-sided magnitude spectrum and frequency axis. Registers three
// analysis parameters: fft_peak_freq_mhz, fft_peak_mag, fft_dc_offset.
//
// Optional notch filtering in frequency domain: when a NotchFilter is
// provided, computes and stores filtered waveform and filtered magnitude
// spectrum via inverse FFT after frequency-domain notch application.
//

#include "MetaWaveformAna.hpp"
#include "Waveform.hpp"
#include "NotchFilter.hpp"

#include "TVirtualFFT.h"
#include "TComplex.h"

#include <cmath>
#include <iomanip>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ndlar_light {

class FFTWaveformAna : public MetaWaveformAna {
public:
    /// Registry names for the FFT-computed parameters.
    static constexpr const char* kPeakFreqName = "fft_peak_freq_mhz";
    static constexpr const char* kPeakMagName = "fft_peak_mag";
    static constexpr const char* kDCOffsetName = "fft_dc_offset";

    /// Function-local-static accessors for registry indices.
    /// These force registration on first call (safe for Cling/ACLiC).
    static std::size_t PeakFreqIndex()
    {
        static const std::size_t idx = MetaWaveformAna::RegisterParam(kPeakFreqName);
        return idx;
    }

    static std::size_t PeakMagIndex()
    {
        static const std::size_t idx = MetaWaveformAna::RegisterParam(kPeakMagName);
        return idx;
    }

    static std::size_t DCOffsetIndex()
    {
        static const std::size_t idx = MetaWaveformAna::RegisterParam(kDCOffsetName);
        return idx;
    }

    /// Default-constructed, unanalyzed placeholder.
    FFTWaveformAna() = default;

    /// Analyzes the waveform by computing its real-to-complex FFT.
    /// @param wf          The raw waveform (600 samples).
    /// @param isValid     Event-level validity flag for this (adc, channel).
    /// @param samplePeriod_ns  ADC hardware sampling period in nanoseconds
    ///                         (e.g. 16.0 ns for DAPHNE).
    ///
    /// On construction:
    ///   - Stores adc/channel/clipped/valid metadata.
    ///   - Computes DC offset (arithmetic mean of all 600 samples).
    ///   - Subtracts DC from each sample.
    ///   - Performs real-to-complex FFT using ROOT's TVirtualFFT.
    ///   - Builds one-sided magnitude spectrum (bins 0..300 = N/2).
    ///   - Applies standard normalization (factor of 2 for bins 1..299).
    ///   - Computes frequency axis [MHz].
    ///   - Finds peak bin (max magnitude) and registers its frequency/magnitude.
    ///
    /// Edge cases handled silently (store NaN):
    ///   - wf.Size() < 2
    ///   - samplePeriod_ns <= 0
    FFTWaveformAna(const Waveform& wf, bool isValid, double samplePeriod_ns)
        : fAdc(wf.GetADC())
        , fChannel(wf.GetChannel())
        , fClipped(wf.IsClipped())
        , fValid(isValid)
        , fSamplePeriod_ns(samplePeriod_ns)
    {
        DoFFTAnalysis(wf, nullptr);
    }

    /// Extended constructor with optional notch filtering.
    /// @param wf          The raw waveform (600 samples).
    /// @param isValid     Event-level validity flag for this (adc, channel).
    /// @param samplePeriod_ns  ADC hardware sampling period in nanoseconds.
    /// @param notch       Optional notch filter configuration. If provided,
    ///                    computes filtered waveform and filtered FFT spectrum.
    ///                    If nullptr, behaves identically to the 3-arg constructor.
    FFTWaveformAna(const Waveform& wf, bool isValid, double samplePeriod_ns,
                   const NotchFilter* notch)
        : fAdc(wf.GetADC())
        , fChannel(wf.GetChannel())
        , fClipped(wf.IsClipped())
        , fValid(isValid)
        , fSamplePeriod_ns(samplePeriod_ns)
    {
        DoFFTAnalysis(wf, notch);
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

    double GetParamByIndex(std::size_t index) const override
    {
        if (!HasParamIndex(index)) {
            throw std::out_of_range(
                "FFTWaveformAna::GetParamByIndex: no param at index " +
                std::to_string(index) + " for ADC " + std::to_string(fAdc) +
                " channel " + std::to_string(fChannel));
        }
        return fParams[index];
    }

    void Print(std::ostream& os = std::cout) const override
    {
        os << std::left
           << std::setw(6) << "ADC" << std::setw(6) << "CH" << std::setw(10) << "clipped"
           << std::setw(12) << "valid" << std::setw(16) << "dc_offset"
           << std::setw(16) << "peak_freq_mhz" << std::setw(16) << "peak_mag\n";
        os << std::left
           << std::setw(6) << fAdc << std::setw(6) << fChannel
           << std::setw(10) << fClipped << std::setw(12) << fValid
           << std::setw(16) << (HasParamIndex(DCOffsetIndex()) ? fParams[DCOffsetIndex()] : std::numeric_limits<double>::quiet_NaN())
           << std::setw(16) << (HasParamIndex(PeakFreqIndex()) ? fParams[PeakFreqIndex()] : std::numeric_limits<double>::quiet_NaN())
           << std::setw(16) << (HasParamIndex(PeakMagIndex()) ? fParams[PeakMagIndex()] : std::numeric_limits<double>::quiet_NaN()) << "\n";
    }

    /// Access the computed one-sided magnitude spectrum (raw, unfiltered).
    const std::vector<double>& Magnitudes() const { return fMagnitudes; }

    /// Access the frequency axis (in MHz).
    const std::vector<double>& Frequencies() const { return fFrequencies; }

    /// Number of FFT bins in the one-sided spectrum.
    int NumFFTBins() const { return static_cast<int>(fMagnitudes.size()); }

    /// Sampling period in nanoseconds.
    double SamplePeriod_ns() const { return fSamplePeriod_ns; }

    /// Whether a filtered waveform was computed (true if notch filter was applied).
    bool HasFilteredWaveform() const { return !fFilteredSamples.empty(); }

    /// Filtered waveform time-domain samples (length 600).
    /// Only valid if HasFilteredWaveform() is true.
    const std::vector<double>& FilteredSamples() const { return fFilteredSamples; }

    /// Filtered waveform one-sided magnitude spectrum (length 301).
    /// Only valid if HasFilteredWaveform() is true.
    const std::vector<double>& FilteredMagnitudes() const { return fFilteredMagnitudes; }

private:
    int fAdc = -1;
    int fChannel = -1;
    bool fClipped = false;
    bool fValid = false;
    double fSamplePeriod_ns = 0.0;

    std::vector<double> fParams;              // Indexed by registry
    std::vector<double> fMagnitudes;          // One-sided magnitude spectrum (raw)
    std::vector<double> fFrequencies;         // Frequency axis (MHz)
    std::vector<double> fFilteredSamples;     // Time-domain filtered waveform (if notch applied)
    std::vector<double> fFilteredMagnitudes;  // One-sided filtered FFT spectrum

    /// Helper function to compute FFT and optionally apply notch filtering.
    void DoFFTAnalysis(const Waveform& wf, const NotchFilter* notch)
    {
        // Initialize parameter storage
        fParams.resize(MetaWaveformAna::ParamNames().size(),
                        std::numeric_limits<double>::quiet_NaN());

        // Ensure all three registry indices exist
        const std::size_t peakFreqIdx = PeakFreqIndex();
        const std::size_t peakMagIdx = PeakMagIndex();
        const std::size_t dcOffsetIdx = DCOffsetIndex();

        // Resize if needed (in case registry grew)
        if (dcOffsetIdx >= fParams.size())
            fParams.resize(dcOffsetIdx + 1, std::numeric_limits<double>::quiet_NaN());

        // Handle edge cases
        if (wf.Size() < 2 || fSamplePeriod_ns <= 0.0) {
            return;  // Leave all params as NaN
        }

        // Compute DC offset (arithmetic mean)
        double sum = 0.0;
        for (std::size_t s = 0; s < wf.Size(); ++s) {
            sum += static_cast<double>(wf.GetSample(s));
        }
        double dcOffset = sum / static_cast<double>(wf.Size());
        fParams[dcOffsetIdx] = dcOffset;

        // Prepare DC-subtracted samples for FFT
        int N = static_cast<int>(kNumSamples);
        std::vector<double> fftInput(N);
        for (int i = 0; i < N; ++i) {
            fftInput[i] = static_cast<double>(wf.GetSample(i)) - dcOffset;
        }

        // Perform real-to-complex FFT
        TVirtualFFT* fft = TVirtualFFT::FFT(1, &N, "R2C M");
        fft->SetPoints(fftInput.data());
        fft->Transform();

        // Extract raw complex FFT output (full two-sided, length N)
        std::vector<double> fftRe(N), fftIm(N);
        for (int k = 0; k < N; ++k) {
            fft->GetPointComplex(k, fftRe[k], fftIm[k]);
        }

        // Compute raw one-sided magnitude spectrum
        ComputeOneSidedSpectrum(fftRe, fftIm, N, fMagnitudes, fFrequencies);

        // Find peak bin (maximum magnitude)
        const int numBins = N / 2 + 1;
        int peakBin = 0;
        double peakMag = 0.0;
        for (int k = 0; k < numBins; ++k) {
            if (fMagnitudes[k] > peakMag) {
                peakMag = fMagnitudes[k];
                peakBin = k;
            }
        }

        if (peakFreqIdx >= fParams.size())
            fParams.resize(peakFreqIdx + 1, std::numeric_limits<double>::quiet_NaN());
        if (peakMagIdx >= fParams.size())
            fParams.resize(peakMagIdx + 1, std::numeric_limits<double>::quiet_NaN());

        fParams[peakFreqIdx] = fFrequencies[peakBin];
        fParams[peakMagIdx] = peakMag;

        // Apply notch filter if provided
        if (notch != nullptr && !notch->frequencies_mhz.empty()) {
            // Copy FFT output for filtering (we'll modify these copies)
            std::vector<double> filteredRe = fftRe;
            std::vector<double> filteredIm = fftIm;

            // Apply notch to frequency domain
            ApplyNotchFilter(filteredRe, filteredIm, N, fSamplePeriod_ns, *notch);

            // Compute filtered one-sided magnitude spectrum
            ComputeOneSidedSpectrum(filteredRe, filteredIm, N, fFilteredMagnitudes, fFrequencies);

            // Inverse transform via a direct C++ inverse DFT (N = 600, cheap).
            // Avoids a second TVirtualFFT instance, which conflicts with the
            // forward plan. x[n] = (1/N) * sum_k X[k] exp(+2*pi*i*k*n/N).
            // The notch is symmetric, so the result is real.
            std::vector<double> cosT(N), sinT(N);
            const double twoPiOverN = 2.0 * 3.14159265358979323846 / static_cast<double>(N);
            for (int m = 0; m < N; ++m) {
                cosT[m] = std::cos(twoPiOverN * m);
                sinT[m] = std::sin(twoPiOverN * m);
            }
            fFilteredSamples.assign(N, 0.0);
            for (int n = 0; n < N; ++n) {
                double acc = 0.0;
                for (int k = 0; k < N; ++k) {
                    const int m = static_cast<int>((static_cast<long long>(k) * n) % N);
                    acc += filteredRe[k] * cosT[m] - filteredIm[k] * sinT[m];
                }
                fFilteredSamples[n] = acc / static_cast<double>(N) + dcOffset;
            }
        }

        delete fft;
    }

    /// Compute one-sided magnitude spectrum from full two-sided complex FFT.
    /// @param re, im      Real and imaginary parts of two-sided FFT (length N)
    /// @param N           FFT size
    /// @param mags        Output: one-sided magnitudes (length N/2+1)
    /// @param freqs       Output: frequency axis in MHz (length N/2+1)
    void ComputeOneSidedSpectrum(const std::vector<double>& re,
                                 const std::vector<double>& im,
                                 int N,
                                 std::vector<double>& mags,
                                 std::vector<double>& freqs)
    {
        const int numBins = N / 2 + 1;
        mags.resize(numBins);
        freqs.resize(numBins);

        for (int k = 0; k < numBins; ++k) {
            double mag = std::sqrt(re[k] * re[k] + im[k] * im[k]);

            // Standard one-sided normalization
            mag /= static_cast<double>(N);
            if (k > 0 && k < N / 2) {
                mag *= 2.0;
            }

            mags[k] = mag;
            freqs[k] = static_cast<double>(k) / (static_cast<double>(N) * fSamplePeriod_ns) * 1e3;
        }
    }
};

} // namespace ndlar_light

