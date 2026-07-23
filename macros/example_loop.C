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

void example_loop()
{
    std::vector<std::string> subrun_files = {
        "/pnfs/dune/scratch/users/jsoto/NDLAr_Run3/VBRscan_20260716/"
        "mpd_run_data_2026_07_16_14_05_34_CST_001130_p00003.FLOW.hdf5",
    };

    try {
        ndlar_light::Run run(subrun_files);

        std::cout << "Run has " << run.NumSubruns() << " subrun(s), "
                  << run.TotalEvents() << " total events.\n";

        size_t max_events = 5000;
        size_t n_printed = 0;

        while (run.HasNext() && n_printed < max_events) {
            const ndlar_light::Event& event = run.NextEvent();

            std::cout << "Event #" << event.GetEventNumber()
                      << " (id=" << event.GetId() << "):\n"
                      << "  trig_type=" << static_cast<int>(event.GetTriggerType()) << "\n";
/*
            for (int adc = 0; adc < ndlar_light::kNumADCs; ++adc) {
                std::cout << "  ADC " << adc
                          << " sn=" << event.GetSerialNumber(adc)
                          << " utime_ms=" << event.GetUTimeMs(adc)
                          << " tai_ns=" << event.GetTaiNs(adc) << "\n";

                for (int ch = 0; ch < ndlar_light::kNumChannels; ++ch) {
                    if (!event.IsValid(adc, ch)) continue;

                    const ndlar_light::Waveform& wf = event.GetWaveform(adc, ch);
                    std::cout << "    CH " << ch
                              << " clipped=" << wf.IsClipped()
                              << " samples[0:5]=";
                    for (size_t s = 0; s < 5; ++s) std::cout << wf.GetSample(s) << " ";
                    std::cout << "...\n";
                }
            }
*/
            ++n_printed;
        }
    } catch (const std::exception& e) {
        std::cerr << "example_loop error: " << e.what() << std::endl;
    }
}
