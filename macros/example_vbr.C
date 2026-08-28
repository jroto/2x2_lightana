#include "../lib/NDLArLight.hpp"

#include <iostream>
void example_vbr()
{

        ndlar_light::VBRCalibrator vbr_calibrator("data/vbr_calibration1.csv");
//          vbr_calibrator.ResetChannels();
//        vbr_calibrator.SelectChannel(0,4);
//        vbr_calibrator.SelectChannel(0,5);
//        vbr_calibrator.SelectChannel(2,4);
//        vbr_calibrator.SelectChannel(2,5);
//        vbr_calibrator.Process();
//          vbr_calibrator.SelectADC(0);
//            vbr_calibrator.ProcessSPEHist("UPDATE"); //loop over all runs.
//            vbr_calibrator.PerformGainFits("UPDATE"); //loop over all runs.
//            vbr_calibrator.FitGainVsVoltage(); //loop over all runs.
        vbr_calibrator.PrintReport();
}