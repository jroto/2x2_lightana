//
// fft_notch.C
//
// Example macro demonstrating LoopFFT with notch filtering.
// Shows raw waveforms and FFT spectra in top rows,
// and filtered waveforms and filtered FFT spectra in bottom rows (4-row layout).
//
// Usage:
//   source ../Setup.sh
//   root -l -q fft_notch.C
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
    run.SelectChannel(1, 20, true);

    // --- Configure notch filter ---
    // Suppress known noise lines (e.g., clock harmonics or power-line noise)
    ndlar_light::NotchFilter notch;
    notch.frequencies_mhz = {10.0, 20.0,25.0,30.0};  // Example: clock harmonics at these frequencies
    notch.half_width_mhz  = 0.1;              // ±0.3 MHz around each centre

    // --- Create AnalysisFFT object with notch filtering ---
    // This overrides Loop() to call LoopFFT() internally with the notch filter.
    ndlar_light::AnalysisFFT ana(run, 16.0, &notch);

    // --- Launch interactive FFT display ---
    // The Loop() call here will invoke LoopFFT() with 4-row layout:
    // Row 1: raw waveform, Row 2: filtered waveform,
    // Row 3: raw FFT spectrum, Row 4: filtered FFT spectrum.
    ana.Loop(500);

    // --- Alternative: using the old-style API (equivalent) ---
    // ndlar_light::Analysis ana_old(run);
    // ana_old.LoopFFT(16.0, 50, &notch);
}



