// macros/example_loop.C
//
// Example ROOT macro demonstrating the ndlar_light library:
//   - build a Run from an explicit list of subrun files
//   - iterate events sequentially
//   - print event metadata and a few waveform samples per (adc, channel)
//
// Run with:
//   source Setup.sh
//   root -l macros/example_loop.C
//
// (No histogramming here for now - just prints event details to prove
// the library works end-to-end.)

#include "../lib/NDLArLight.hpp"

#include <iostream>

void example_loop() {

    std::string path = "/pnfs/dune/scratch/users/jsoto/NDLAr_Run3/VBRscan_20260716/";
    int run_number = 1130;

    try {
        std::cout << "Building run from path: " << path << " run number: " << run_number << "\n";
        // --- 1. Build run and select channels (unchanged) ---
        ndlar_light::Run run(path, run_number);
        run.Print();
        run.ResetChannels(false);
        run.SelectChannel(2, 4, true);
        run.SelectChannel(2, 5, true);

        // --- 2. Baseline calibration (NEW) ---
        // Configure the baseline estimator and calibrator
        ndlar_light::CalibratorConfig cal_cfg;
        cal_cfg.baseline_cfg.window_ticks      = 30;    // ticks per baseline window
        cal_cfg.baseline_cfg.amp_threshold_adc = 120.0;   // max Amp = max-min in window
        cal_cfg.baseline_cfg.asymmetry_factor  = 3.0;   // max AmpBot/AmpTop ratio
        cal_cfg.max_events                     = 3000;  // events to use for calibration
        cal_cfg.fit_range_sigma                = 2.0;   // Gaussian fit range: mean ± 2*RMS

        ndlar_light::BaselineCalibrator calibrator(cal_cfg);
        calibrator.Calibrate(run);   // reads up to 1000 events, fits Gaussians per channel
        calibrator.Print();          // print calibrated baseline table
        calibrator.Draw();

        // --- 3. WaveAna configuration (NEW) ---
        ndlar_light::WaveAnaConfig wana_cfg;
        wana_cfg.baseline_cfg   = cal_cfg.baseline_cfg; // reuse same baseline settings
        wana_cfg.threshold_adc  = 80.0;                  // hit threshold above baseline

        // --- 4. Build Analysis with a WaveAna factory (NEW) ---
        // The factory captures wana_cfg and the calibrator by reference.
        // Analysis::Loop() will detect WaveAna via dynamic_cast and draw
        // baseline segments (green lines) and hits (red bands + markers).
        ndlar_light::Analysis analysis(run,
            [&wana_cfg, &calibrator](const ndlar_light::Waveform& wf, bool isValid)
            -> std::unique_ptr<ndlar_light::MetaWaveformAna>
            {
                const ndlar_light::ChannelBaseline* fallback =
                    &calibrator.GetBaseline(wf.GetADC(), wf.GetChannel());
                return std::make_unique<ndlar_light::WaveAna>(
                    wf, isValid, wana_cfg, fallback);
            });

        // --- 5. Run the interactive loop (unchanged call) ---
        analysis.Loop();

        std::cout << "Run has " << run.NumSubruns() << " subrun(s), "
                  << run.TotalEvents() << " total events.\n";

    } catch (const std::exception& e) {
        std::cerr << "example_loop error: " << e.what() << std::endl;
    }
}