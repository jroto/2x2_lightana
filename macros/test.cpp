#include "../lib/NDLArLight.hpp"

#include <iostream>
using namespace std;
int main()
{

        ndlar_light::VBRCalibrator vbr_calibrator("data/vbr_calibration1.csv");
        vbr_calibrator.ResetChannels();
        vbr_calibrator.SelectChannel(2,4);
        vbr_calibrator.Process();
        return 0;
}