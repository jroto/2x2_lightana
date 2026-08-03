Prompt: BaselineCalibrator::Draw(), retained histograms/fits, and PauseExecution()
Context

    Namespace: ndlar_light. Header-only, ROOT/ACLiC compatible.
    BaselineCalibrator lives in lib/BaselineCalibrator.hpp. It currently builds a TH1F and TF1 per (adc, ch) inside FitChannel(), extracts results, then deletes them.
    Analysis::Loop() lives in lib/Analysis.hpp. It currently has an inline pause block using std::getline(std::cin, line).
    Both files include ../lib/NDLArLight.hpp which is the umbrella header.

1. New file lib/Utils.hpp

Create a small utility header containing PauseExecution():

cpp

#pragma once
//
// Utils.hpp
//
// Shared utility functions for interactive ROOT macros.
//

#include <iostream>
#include <string>

namespace ndlar_light {

/// Prints `prompt` to stdout, then waits for the user to press Enter.
/// Returns true  → user pressed Enter (continue).
/// Returns false → user typed 'q' or 'Q' before Enter (quit).
/// Used by Analysis::Loop() and BaselineCalibrator::Draw() to pause
/// execution between canvas updates.
inline bool PauseExecution(const std::string& prompt = "[Enter] continue   [q] quit: ") {
    std::cout << prompt << std::flush;
    std::string line;
    std::getline(std::cin, line);
    return line.empty() || (line[0] != 'q' && line[0] != 'Q');
}

} // namespace ndlar_light

2. Update lib/Analysis.hpp
2a. Add include

cpp

#include "Utils.hpp"

2b. Replace the inline pause block in Loop()

Find and replace:

cpp

std::cout << "Event " << eventCount
          << " - [Enter] next, [q+Enter] quit: " << std::flush;
std::string line;
std::getline(std::cin, line);
if (!line.empty() && (line[0] == 'q' || line[0] == 'Q')) break;

With:

cpp

if (!PauseExecution("Event " + std::to_string(eventCount)
                    + " | [Enter] next   [q] quit: "))
    break;

No other changes to Analysis.hpp.
3. Update lib/BaselineCalibrator.hpp
3a. Add includes

cpp

#include "Utils.hpp"
#include "TCanvas.h"
#include "TPaveText.h"
#include "TStyle.h"

3b. Retain histograms and fits as members

Add private members to BaselineCalibrator:

cpp

private:
    // Retained per-channel histogram and Gaussian fit for Draw().
    // Indexed [adc][ch]. nullptr if channel was never calibrated
    // (no baseline samples accumulated).
    TH1F* fHist[kNumADCs][kNumChannels];
    TF1*  fFit [kNumADCs][kNumChannels];

Initialize all pointers to nullptr in the constructor:

cpp

BaselineCalibrator::BaselineCalibrator(const CalibratorConfig& cfg)
    : fCfg(cfg), fBaseline(cfg.baseline_cfg)
{
    for (int adc = 0; adc < kNumADCs; ++adc)
        for (int ch = 0; ch < kNumChannels; ++ch) {
            fHist[adc][ch] = nullptr;
            fFit [adc][ch] = nullptr;
        }
}

Add a destructor that cleans up owned ROOT objects:

cpp

~BaselineCalibrator() {
    for (int adc = 0; adc < kNumADCs; ++adc)
        for (int ch = 0; ch < kNumChannels; ++ch) {
            delete fHist[adc][ch];
            delete fFit [adc][ch];
        }
}

3c. Update FitChannel() to retain instead of delete

Replace the current FitChannel() logic with the following. The key change is:

    Do not delete h at the end — assign it to fHist[adc][ch].
    Do not destroy the TF1 — assign it to fFit[adc][ch].
    Give each histogram and function a unique name to avoid ROOT name collisions: Form("h_cal_adc%d_ch%d", adc, ch) and Form("f_cal_adc%d_ch%d", adc, ch).
    Use h->Fit(fFit[adc][ch], "Q0N", "", fit_min, fit_max) so ROOT does not draw or print during calibration.

cpp

void FitChannel(int adc, int ch) {
    auto& samples = fSamples[adc][ch];
    if (samples.empty()) return;

    // Build histogram over all samples (outliers NOT removed) [K1]
    double smin = *std::min_element(samples.begin(), samples.end());
    double smax = *std::max_element(samples.begin(), samples.end());
    int nbins = std::max(20, static_cast<int>((smax - smin) * 4));
    std::string hname = Form("h_cal_adc%d_ch%d", adc, ch);
    fHist[adc][ch] = new TH1F(hname.c_str(),
                               Form("ADC %d / CH %d", adc, ch),
                               nbins, smin - 1.0, smax + 1.0);
    for (double v : samples)
        fHist[adc][ch]->Fill(v);

    // Restrict Gaussian fit to central bulk [K1]
    double h_mean = fHist[adc][ch]->GetMean();
    double h_rms  = fHist[adc][ch]->GetRMS();
    double fit_min = h_mean - fCfg.fit_range_sigma * h_rms;
    double fit_max = h_mean + fCfg.fit_range_sigma * h_rms;

    std::string fname = Form("f_cal_adc%d_ch%d", adc, ch);
    fFit[adc][ch] = new TF1(fname.c_str(), "gaus", fit_min, fit_max);
    fFit[adc][ch]->SetParameters(fHist[adc][ch]->GetMaximum(), h_mean, h_rms);

    // "Q0N": quiet, do not draw, do not store in histogram's list
    TFitResultPtr r = fHist[adc][ch]->Fit(fFit[adc][ch], "Q0NR");

    fResult[adc][ch].n_windows = samples.size();
    if (r.Get() && r->IsValid()) {
        fResult[adc][ch].mean       = fFit[adc][ch]->GetParameter(1);
        fResult[adc][ch].sigma      = fFit[adc][ch]->GetParameter(2);
        fResult[adc][ch].calibrated = true;
    } else {
        // Fit failed: store histogram mean as fallback, mark uncalibrated
        fResult[adc][ch].mean       = h_mean;
        fResult[adc][ch].sigma      = h_rms;
        fResult[adc][ch].calibrated = false;
    }
}

3d. Add BaselineCalibrator::Draw()

cpp

void Draw() {
    // Collect all channels that have a histogram (fit may have failed) [K3]
    std::vector<std::pair<int,int>> channels;
    for (int adc = 0; adc < kNumADCs; ++adc)
        for (int ch = 0; ch < kNumChannels; ++ch)
            if (fHist[adc][ch] != nullptr)
                channels.emplace_back(adc, ch);

    if (channels.empty()) {
        std::cout << "BaselineCalibrator::Draw: no calibrated channels to draw.\n";
        return;
    }

    // Dynamic grid layout [K2]
    const int nPads = static_cast<int>(channels.size());
    const int nCols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(nPads))));
    const int nRows = static_cast<int>(std::ceil(static_cast<double>(nPads) / nCols));

    TCanvas* canvas = new TCanvas("cal_canvas", "Baseline Calibration",
                                  200, 10, 1400, 900);
    canvas->Divide(nCols, nRows);
    gStyle->SetOptStat(0);

    int padIdx = 1;
    for (const auto& p : channels) {
        int adc = p.first;
        int ch  = p.second;
        canvas->cd(padIdx++);
        gPad->Clear();

        TH1F* h = fHist[adc][ch];
        TF1*  f = fFit [adc][ch];
        const ChannelBaseline& res = fResult[adc][ch];

        // Draw histogram
        h->SetLineColor(kBlue + 1);
        h->SetLineWidth(1);
        h->GetXaxis()->SetTitle("Baseline mean (ADC)");
        h->GetYaxis()->SetTitle("Windows");
        h->Draw("HIST");

        // Overlay fit if it exists [K8]
        if (f != nullptr) {
            f->SetLineColor(res.calibrated ? kRed : kOrange + 1);
            f->SetLineWidth(2);
            f->Draw("SAME");
        }

        // TPaveText: channel identity + fit result [K8]
        TPaveText* pt = new TPaveText(0.55, 0.65, 0.98, 0.98, "NDC");
        pt->SetFillColor(0);
        pt->SetBorderSize(1);
        pt->SetTextSize(0.05);
        pt->AddText(Form("ADC %d / CH %d", adc, ch));
        pt->AddText(Form("N windows: %zu", res.n_windows));
        if (res.calibrated) {
            pt->AddText(Form("#mu = %.2f ADC", res.mean));
            pt->AddText(Form("#sigma = %.2f ADC", res.sigma));
        } else {
            pt->AddText("Fit FAILED");
            pt->AddText(Form("histo mean = %.2f", res.mean));
        }
        pt->Draw();

        gPad->Update();
    }

    canvas->cd(0);
    canvas->SetTitle("Baseline Calibration — all channels");
    canvas->Update();

    // Pause execution [K2]
    PauseExecution("Baseline calibration drawn | [Enter] continue   [q] quit: ");
}

4. Update lib/NDLArLight.hpp

Add #include "Utils.hpp" before the other lib includes, so it is always available:

cpp

#include "Utils.hpp"
#include "Waveform.hpp"
// ... rest unchanged

Summary of changes
File	Change
lib/Utils.hpp	New — PauseExecution() shared utility
lib/BaselineCalibrator.hpp	Retain TH1F*/TF1* per channel ; update FitChannel(); add Draw() ; add destructor
lib/Analysis.hpp	Replace inline pause with PauseExecution() call; add #include "Utils.hpp"
lib/NDLArLight.hpp	Add #include "Utils.hpp"
Everything else	No change
