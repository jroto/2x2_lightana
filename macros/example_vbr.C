#include "../lib/NDLArLight.hpp"

#include <iostream>
void example_vbr()
{

        ndlar_light::VBRCalibrator vbr_calibrator("data/vbr_calibration1.csv");
//        vbr_calibrator.ResetChannels();
//        vbr_calibrator.SelectChannel(2,4);
//        vbr_calibrator.SelectChannel(2,5);
//        vbr_calibrator.SelectChannel(2,6);
//        vbr_calibrator.Process();
//            vbr_calibrator.ProcessSPEHist(); //loop over all runs.
cout << "HI"<< endl;
            vbr_calibrator.PerformGainFits(); //loop over all runs.
//            vbr_calibrator.FitGainVsVoltage(); //loop over all runs.
//            DumpVBR(outfile);
//        vbr_calibrator.PrintReport();
}