#include "../lib/NDLArLight.hpp"

#include <iostream>
using namespace std;
int main()
{

        ndlar_light::VBRCalibrator vbr_calibrator("data/vbr_calibration1.csv");
          vbr_calibrator.ResetChannels();
        vbr_calibrator.SelectChannel(0,4);
//            vbr_calibrator.ProcessSPEHist("UPDATE"); //loop over all runs.
            vbr_calibrator.PerformGainFits("UPDATE"); //loop over all runs.
            vbr_calibrator.FitGainVsVoltage(); //loop over all runs.
        vbr_calibrator.PrintReport();
}