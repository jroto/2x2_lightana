//
// fft_loop.cpp
//
// Example macro demonstrating the LoopFFT interactive display method.
// Shows waveforms in the top row and running-average FFT magnitude spectra
// in the bottom row for selected channels.
//
// Usage:
//   source ../Setup.sh
//   root -l -q fft_loop.cpp
//

#include "../lib/NDLArLight.hpp"

void fft_loop() {
    // --- Configure data path and run number ---
    // Update this path and run number as needed
    std::string path = "/global/cfs/cdirs/dune/www/data/2x2/nearline_run3/flowed_light/cold_commission/V_br/20260924_49V5_20dB_mod0_ACL/";
    int run_number = 1551;

    const char* dataPath = "/global/cfs/cdirs/dune/www/data/2x2/nearline_run3/flowed_light/cold_commission/V_br/20260924_49V5_20dB_mod0_ACL/";
    int runNumber = 1551;

    // --- Open the run ---
    ndlar_light::Run run(dataPath, runNumber);

    // --- Select a subset of channels to display ---
    // First, reset all channels to inactive
    run.ResetChannels(false);

    // Then select specific channels of interest
    // Example: ADC 0, channels 4 and 5
    run.SelectChannel(0, 4, true);
    run.SelectChannel(0, 5, true);

    // --- Create analysis object ---
    ndlar_light::Analysis ana(run);

    // --- Launch interactive LoopFFT display ---
    // Arguments:
    //   samplePeriod_ns = 16.0 ns (ADC sampling period for DAPHNE)
    //   maxEvents = 50 (show at most 50 events, -1 for all)
    ana.LoopFFT(16.0, 5000);

}

