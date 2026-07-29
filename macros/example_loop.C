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
    std::string path="/pnfs/dune/scratch/users/jsoto/NDLAr_Run3/VBRscan_20260716/";
    int run_number = 1130;
    try {
        ndlar_light::Run run(path, run_number);
        run.Print();
        run.ResetChannels(false); // deactivate all channels
        run.SelectChannel(2, 4, true); // activate ADC 0, channel
        run.SelectChannel(2, 5, true); // activate ADC 0, channel
        run.Print();
        ndlar_light::Analysis analysis(run);
        analysis.Loop();
        std::cout << "Run has " << run.NumSubruns() << " subrun(s), "
                  << run.TotalEvents() << " total events.\n";

/*
        size_t max_events = 5000;
        size_t n_printed = 0;
        while (run.HasNext() && n_printed < max_events) {
            const ndlar_light::Event& event = run.NextEvent();

            std::cout << "Event #" << event.GetEventNumber()
                      << " (id=" << event.GetId() << "):\n"
                      << "  trig_type=" << static_cast<int>(event.GetTriggerType()) << "\n";
            n_printed++;
        }
*/
    } catch (const std::exception& e) {
        std::cerr << "example_loop error: " << e.what() << std::endl;
    }
}
