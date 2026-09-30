#pragma once
//
// NotchFilter.hpp
//
// Plain struct and free function for frequency-domain notch filtering.
// No ROOT dependency, pure C++ stdlib. Designed to zero out specified
// frequency bands (e.g., power-line harmonics, clock noise) in FFT output.
//

#include <vector>
#include <cmath>

namespace ndlar_light {

/// Configuration for notch filtering: centre frequencies and half-width.
struct NotchFilter {
    /// Centre frequencies to suppress [MHz].
    std::vector<double> frequencies_mhz;

    /// Half-width of each notch around its centre [MHz].
    /// Default: 0.5 MHz (i.e., ±0.5 MHz around each centre).
    double half_width_mhz = 0.5;
};

/// Apply notch filter to a one-sided complex FFT output in-place.
///
/// Zeros out all FFT bins (both positive and negative frequencies) that
/// fall within the specified notch bands.
///
/// @param re              Real part of full two-sided FFT output (length N).
///                        Modified in-place.
/// @param im              Imaginary part of full two-sided FFT output (length N).
///                        Modified in-place.
/// @param N               FFT size (number of samples, e.g., 600).
/// @param samplePeriod_ns ADC sampling period in nanoseconds (e.g., 16.0 ns).
/// @param filter          NotchFilter configuration (centre frequencies and
///                        half-width).
///
/// Frequency-bin mapping:
///   Positive freqs: k = 0 to N/2     → freq[k] = k / (N * samplePeriod_ns * 1e-3) [MHz]
///   Negative freqs: k = N/2+1 to N-1 → freq[N-k] (mirror)
///
/// For each notch centre f_c in filter.frequencies_mhz:
///   - Zero all bins k where |freq[k] - f_c| <= half_width_mhz
///   - Also zero the mirror bin at N-k if it exists
inline void ApplyNotchFilter(std::vector<double>& re,
                             std::vector<double>& im,
                             int N,
                             double samplePeriod_ns,
                             const NotchFilter& filter)
{
    if (filter.frequencies_mhz.empty() || samplePeriod_ns <= 0.0) {
        return;  // No notches, or invalid sampling period
    }

    // For each bin k, compute the frequency in MHz
    for (int k = 0; k < N; ++k) {
        // Frequency of positive-side bin
        double freq_k = static_cast<double>(k) / (static_cast<double>(N) * samplePeriod_ns) * 1e3;

        // Frequency of negative-side bin (mirror)
        double freq_nk = static_cast<double>(N - k) / (static_cast<double>(N) * samplePeriod_ns) * 1e3;

        // Check if this bin is within any notch
        bool inNotch = false;
        for (double f_c : filter.frequencies_mhz) {
            if (std::fabs(freq_k - f_c) <= filter.half_width_mhz ||
                std::fabs(freq_nk - f_c) <= filter.half_width_mhz) {
                inNotch = true;
                break;
            }
        }

        // Zero the bin if it's within a notch
        if (inNotch) {
            re[k] = 0.0;
            im[k] = 0.0;
        }
    }
}

} // namespace ndlar_light

