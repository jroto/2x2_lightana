// macros/example_dump.C
//
// Example ROOT macro demonstrating ndlar_light::Analysis::Dump:
//   - build a Run from an explicit list of subrun files (same as
//     example_analysis.C)
//   - construct Analysis(run), call process()
//   - call Dump("analysis.root") to write a TTree with one entry per
//     (event, adc, channel) waveform, including dynamically-discovered
//     analysis-variable branches (currently just "mean")
//   - reopen the file and print the tree structure to show the result
//
// Run with:
//   source Setup.sh
//   root -l macros/example_dump.C

#include "../lib/NDLArLight.hpp"

#include "TFile.h"
#include "TTree.h"

#include <iostream>

void example_dump()
{
    std::vector<std::string> subrun_files = {
        "/pnfs/dune/scratch/users/jsoto/NDLAr_Run3/VBRscan_20260716/"
        "mpd_run_data_2026_07_16_14_05_34_CST_001130_p00003.FLOW.hdf5",
    };

    try {
        ndlar_light::Run run(subrun_files);
        std::cout << "Run has " << run.NumSubruns() << " subrun(s), "
                  << run.TotalEvents() << " total events.\n";

        ndlar_light::Analysis analysis(run);
        analysis.process();
        std::cout << "Analysis processed " << analysis.GetNEvents() << " events.\n";

        const std::string outname = "analysis.root";
        analysis.Dump(outname);
        std::cout << "Wrote " << outname << "\n";

        TFile f(outname.c_str(), "READ");
        if (f.IsZombie()) {
            std::cerr << "Failed to reopen " << outname << "\n";
            return;
        }
        f.ls();

        TTree* tree = dynamic_cast<TTree*>(f.Get("waveforms"));
        if (tree) {
            std::cout << "Entries: " << tree->GetEntries() << "\n";
            tree->Print();
        }
    } catch (const std::exception& e) {
        std::cerr << "example_dump error: " << e.what() << std::endl;
    }
}
