//
// fft_notch.cpp
//
// Example macro demonstrating LoopFFT with notch filtering.
// Shows raw waveforms and FFT spectra in top two rows,
// and filtered waveforms and filtered FFT spectra in bottom two rows.
//
// Usage:
//   source ../Setup.sh
//   root -l -q fft_notch.cpp
//

#include "../lib/NDLArLight.hpp"

void fft_notch() {
    // --- Configure data path and run number ---
    const char* dataPath = "/global/cfs/cdirs/dune/www/data/2x2/nearline_run3/flowed_light/cold_commission/V_br/20260924_49V5_20dB_mod0_ACL/";
    int runNumber = 1551;

    // --- Open the run ---
    ndlar_light::Run run(dataPath, runNumber);

    // --- Select a subset of channels to display ---
    run.ResetChannels(false);
    run.SelectChannel(0, 4, true);
    run.SelectChannel(0, 5, true);

    // --- Configure notch filter ---
    // Suppress known noise lines (e.g., clock harmonics or power-line noise)
    ndlar_light::NotchFilter notch;
    notch.frequencies_mhz = {1.5625, 3.125};  // Example: clock harmonics at these frequencies
    notch.half_width_mhz  = 0.3;              // ±0.3 MHz around each centre

    // --- Create analysis object ---
    ndlar_light::Analysis ana(run);

    // --- Launch interactive LoopFFT display with notch filtering ---
    // Arguments:
    //   samplePeriod_ns = 16.0 ns (ADC sampling period for DAPHNE)
    //   maxEvents = 50 (show at most 50 events)
    //   &notch = notch filter configuration (pointer)
    ana.LoopFFT(16.0, 50, &notch);
}

