// macros/example_analysis.C
//
// Example ROOT macro demonstrating the ndlar_light Analysis layer:
//   - build a Run from an explicit list of subrun files (same file as
//     example_loop.C)
//   - construct Analysis(run) and call process()
//   - loop over Analysis::GetEvents(), printing each EventAna's metadata
//     and the mean of a couple of valid channels
//
// Run with:
//   source Setup.sh
//   root -l macros/example_analysis.C

#include "../lib/NDLArLight.hpp"

#include <iostream>

void example_analysis()
{
    std::vector<std::string> subrun_files = {
        "/pnfs/dune/scratch/users/jsoto/NDLAr_Run3/VBRscan_20260716/"
        "mpd_run_data_2026_07_16_14_05_34_CST_001130_p00003.FLOW.hdf5",
    };

    try {
        ndlar_light::Run run("/pnfs/dune/scratch/users/jsoto/NDLAr_Run3/VBRscan_20260716/",
                            1130);
//        "mpd_run_data_2026_07_16_14_05_34_CST_001130_p00003.FLOW.hdf5);
        std::cout << "Run has " << run.NumSubruns() << " subrun(s), "
                  << run.TotalEvents() << " total events.\n";

        ndlar_light::Analysis analysis(run);
        analysis.process();

        std::cout << "Analysis processed " << analysis.GetNEvents() << " events.\n";

        size_t max_events = 5;
        size_t n_printed = 0;

        for (const auto& eventAna : analysis.GetEvents()) {
            if (n_printed >= max_events) break;

            eventAna.Meta().Print();

            int n_channels_shown = 0;
            for (int adc = 0; adc < ndlar_light::kNumADCs && n_channels_shown < 2; ++adc) {
                for (int ch = 0; ch < ndlar_light::kNumChannels && n_channels_shown < 2; ++ch) {
                    if (!eventAna.Meta().IsValid(adc, ch)) continue;
                    const ndlar_light::WaveformAna& wa = eventAna.GetWaveformAna(adc, ch);
                    std::cout << "  ADC " << adc << " CH " << ch
                              << " mean=" << wa.GetMean() << "\n";
                    ++n_channels_shown;
                }
            }

            ++n_printed;
        }
    } catch (const std::exception& e) {
        std::cerr << "example_analysis error: " << e.what() << std::endl;
    }
}
