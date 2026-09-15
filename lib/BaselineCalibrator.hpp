#pragma once
//
// BaselineCalibrator.hpp
//
// Runs over a Run's events, accumulating Baseline::FindAll() window
// means per (adc, channel), then fits a Gaussian to each channel's
// accumulated distribution to obtain a calibrated baseline mean/sigma.
// This is the "offline calibration" step whose result (ChannelBaseline)
// can be handed to WaveAna as a per-channel fallback baseline for events
// where no in-waveform baseline window is found.
//

#include "Baseline.hpp"
#include "EventMetadata.hpp" // kNumADCs / kNumChannels
#include "Event.hpp"
#include "Run.hpp"
#include "Utils.hpp"

#include "TF1.h"
#include "TFitResult.h"
#include "TFitResultPtr.h"
#include "TH1F.h"
#include "TCanvas.h"
#include "TPaveText.h"
#include "TStyle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <ostream>
#include <string>
#include <vector>
#include <utility>

using namespace std;

namespace ndlar_light {

/// Calibrated baseline (Gaussian fit result) for a single channel.
struct ChannelBaseline {
    double      mean       = 0.0;   // Gaussian mean = calibrated baseline
    double      sigma      = 0.0;   // Gaussian sigma = calibrated noise
    double      STD       = 0.0;   // Gaussian mean = calibrated baseline
    double      STD_sigma       = 0.0;   // Gaussian mean = calibrated baseline
    std::size_t n_windows  = 0;     // number of baseline windows accumulated
    bool        calibrated = false; // true if Gaussian fit succeeded
};

/// Tunable parameters for BaselineCalibrator.
struct CalibratorConfig {
    BaselineConfig baseline_cfg;          // passed to Baseline
    std::size_t    max_events      = 1000; // max events to process
    double         fit_range_sigma = 2.0;  // Gaussian fit range: mean +/- N*RMS
};

/// Accumulates per-channel baseline-window means across a Run and fits a
/// Gaussian per channel to obtain a calibrated baseline (mean, sigma).
class BaselineCalibrator {
public:
    explicit BaselineCalibrator(const CalibratorConfig& cfg = CalibratorConfig{})
        : fCfg(cfg), fBaseline(cfg.baseline_cfg)
    {
        for (int adc = 0; adc < kNumADCs; ++adc)
            for (int ch = 0; ch < kNumChannels; ++ch) {
                fHist[adc][ch] = nullptr;
                fFit [adc][ch] = nullptr;
                fHist_std[adc][ch] = nullptr;
                fFit_std [adc][ch] = nullptr;
            }
    }

    ~BaselineCalibrator()
    {
        ClearCalibration();
    }

    /// Process up to cfg.max_events from `run`.
    /// Only channels selected in the Run channel map are processed.
    /// Each call starts a fresh, independent calibration.
    void Calibrate(Run& run)
    {
        // Discard samples, results, histograms, and fits from any
        // previous calibration before processing the current selection.
        ClearCalibration();

        // Reset first so the Run recreates its reader using the current
        // channel-map selection.
        run.Reset();

        // Take one stable snapshot of the channels to calibrate.
        fSelectedChannels = run.GetSelectedChannels();

        std::size_t nProcessed = 0;

        while (run.HasNext() && nProcessed < fCfg.max_events) {
            const Event& event = run.NextEvent();
            ++nProcessed;

            // Iterate only over the selected channels, not all 8 x 64 slots.
            for (const auto& channel : fSelectedChannels) {
                const int adc = channel.first;
                const int ch  = channel.second;

                if (!event.IsValid(adc, ch)) continue;

                const Waveform& wf = event.GetWaveform(adc, ch);

                std::vector<double> samples(wf.Size());
                for (std::size_t s = 0; s < wf.Size(); ++s) {
                    samples[s] = wf.GetSample(s);
                }

                const std::vector<BaselineSegment> segs =
                    fBaseline.FindAll(samples);

                for (const auto& seg : segs) {
                    fSamples[adc][ch].push_back(seg.mean);
                    fSamples_std[adc][ch].push_back(seg.std);
                }
            }
        }

        // Fit only the channels selected for this calibration.
        for (const auto& channel : fSelectedChannels) {
            FitChannel(channel.first, channel.second,"mean");
            FitChannel(channel.first, channel.second,"std");
        }
    }
    /// Access the calibrated baseline for a channel.
    const ChannelBaseline& GetBaseline(int adc, int ch) const
    {
        return fResult[adc][ch];
    }

    double GetBaselineMean(int adc, int ch) const
    {
        return fResult[adc][ch].mean;
    }
    double GetBaselineSTD(int adc, int ch) const
    {
        return fResult[adc][ch].STD;
    }

    /// Print a summary table of calibrated baselines to os.
    /// Print calibrated baselines for channels selected in the most
    /// recent call to Calibrate().
    void Print(std::ostream& os = std::cout) const
    {
        os << std::left
           << std::setw(6)  << "ADC"
           << std::setw(6)  << "CH"
           << std::setw(12) << "mean"
           << std::setw(12) << "sigma"
           << std::setw(12) << "std"
           << std::setw(12) << "std_sigma"
           << std::setw(10) << "n_wins"
           << "calibrated\n";

        if (fSelectedChannels.empty()) {
            os << "No channels were selected for the most recent calibration.\n";
            return;
        }

        for (const auto& channel : fSelectedChannels) {
            const int adc = channel.first;
            const int ch  = channel.second;

            const ChannelBaseline& b = fResult[adc][ch];
            std::setprecision(1);
            os << std::left
               << std::setw(6)  << adc
               << std::setw(6)  << ch << std::fixed << std::setprecision(1)
               << std::setw(12) << b.mean
               << std::setw(12) << b.sigma
               << std::setw(12) << b.STD
               << std::setw(12) << b.STD_sigma
               << std::setw(10) << b.n_windows
               << b.calibrated
               << "\n";
        }
    }
    /// Draw calibrated histograms and fits on a TCanvas, one pad per channel.
    /// Pauses for user input via PauseExecution().
    void  Draw(string mode="mean", bool NotPause=false, string pdfFile="") const
    {

        const bool drawMean = (mode == "mean");
        const bool drawStd  = (mode == "std");

        if (!drawMean && !drawStd) {
            std::cerr << "BaselineCalibrator::Draw: mode must be \"mean\" or \"std\"; got \""
                    << mode << "\".\n";
            return;
        }
                std::vector<std::pair<int,int>> channels;
        for (int adc = 0; adc < kNumADCs; ++adc)
            for (int ch = 0; ch < kNumChannels; ++ch)
                if (fHist[adc][ch] != nullptr)
                    channels.emplace_back(adc, ch);

        if (channels.empty()) {
            std::cout << "BaselineCalibrator::Draw: no calibrated channels to draw.\n";
            return;
        }


        gStyle->SetOptStat(0);

        for (int adc = 0; adc < kNumADCs; ++adc)
        {
            const std::string canvasName  = "baseline_calibration_" + mode + "_"+ std::to_string(adc);
            const std::string canvasTitle = "Baseline calibration (ADD "+std::to_string(adc)+"): " + mode + " per channel.";
            TCanvas canvas(
                canvasName.c_str(), canvasTitle.c_str(), 200, 10, 1400, 900
            );
            canvas.Divide(6, 8);
            int padIdx=1;
            for (int ch = 0; ch < kNumChannels; ++ch)
            {
                canvas.cd(padIdx++);
                gPad->Clear();

                TH1F* h = drawMean ? fHist[adc][ch] : fHist_std[adc][ch];
                TF1*  f = drawMean ? fFit[adc][ch]  : fFit_std[adc][ch];

                const ChannelBaseline& res = fResult[adc][ch];

                // Draw histogram
                h->SetLineColor(kBlue + 1);
                h->SetLineWidth(1);
                if(mode=="mean") h->GetXaxis()->SetTitle("Baseline mean (ADC)");
                else h->GetXaxis()->SetTitle("Baseline std (ADC)");
                h->GetYaxis()->SetTitle("Windows");
                h->Draw("HIST");

                // Overlay fit if it exists
                if (f != nullptr) {
                    f->SetLineColor(res.calibrated ? kRed : kOrange + 1);
                    f->SetLineWidth(2);
                    f->Draw("SAME");
                }

                // TPaveText: channel identity + fit result
                TPaveText* pt = new TPaveText(0.55, 0.65, 0.98, 0.98, "NDC");
                pt->SetFillColor(0);
                pt->SetBorderSize(1);
                pt->SetTextSize(0.05);
                pt->AddText(Form("ADC %d / CH %d", adc, ch));
                pt->AddText(Form("N windows: %zu", res.n_windows));
                if (res.calibrated) {
                    drawMean ? pt->AddText(Form("#mu = %.2f ADC", res.mean)) :
                    pt->AddText(Form("#mu = %.2f ADC", res.STD));
                    drawMean ? pt->AddText(Form("#sigma = %.2f ADC", res.sigma)) :
                    pt->AddText(Form("#sigma = %.2f ADC", res.STD_sigma));
                } else {
                    pt->AddText("Fit FAILED");
                    pt->AddText(Form("histo mean = %.2f", res.mean));
                }
                pt->Draw();

                gPad->Update();
            }
            canvas.Update();

            if(!NotPause) PauseExecution("Baseline calibration drawn | [Enter] continue   [q] quit: ");
            if(!pdfFile.empty()) canvas.Print(pdfFile.c_str(), "pdf");
        }
    }

private:
    CalibratorConfig fCfg;
    Baseline         fBaseline; //Class that contains the baseline finding algorithm
    ChannelBaseline  fResult[kNumADCs][kNumChannels]; //class that contains the results (mean, std, etc)

    // Snapshot of Run-selected channels used by the latest Calibrate() call.
    std::vector<std::pair<int, int>> fSelectedChannels;

    // Raw accumulated baseline means per channel, kept for histogram/fit.
    std::vector<double> fSamples[kNumADCs][kNumChannels];
    std::vector<double> fSamples_std[kNumADCs][kNumChannels];

    /// Retained per-channel histogram and Gaussian fit for Draw().
    /// Indexed [adc][ch]. nullptr if channel was never calibrated
    /// (no baseline samples accumulated).
    TH1F* fHist[kNumADCs][kNumChannels];
    TF1*  fFit [kNumADCs][kNumChannels];
    TH1F* fHist_std[kNumADCs][kNumChannels];
    TF1*  fFit_std [kNumADCs][kNumChannels];

    /// Delete ROOT objects and reset all state from a prior calibration.
    /// Safe to call when no calibration has been performed yet.
    void ClearCalibration()
    {
        for (int adc = 0; adc < kNumADCs; ++adc) {
            for (int ch = 0; ch < kNumChannels; ++ch) {
                delete fHist[adc][ch];
                delete fFit[adc][ch];
                delete fHist_std[adc][ch];
                delete fFit_std[adc][ch];

                fHist[adc][ch] = nullptr;
                fFit[adc][ch]  = nullptr;

                fHist_std[adc][ch] = nullptr;
                fFit_std[adc][ch]  = nullptr;

                fSamples[adc][ch].clear();
                fSamples_std[adc][ch].clear();
                fResult[adc][ch] = ChannelBaseline{};
            }
        }

        fSelectedChannels.clear();
    }
    /// Fit a Gaussian to fSamples[adc][ch] and fill fResult[adc][ch].
    /// The histogram is built over all samples (outliers NOT removed).
    /// The Gaussian fit is restricted to [mean - N*rms, mean + N*rms]
    /// where mean/rms are computed from the histogram itself,
    /// and N = cfg.fit_range_sigma. Retains histogram and fit for Draw().
    void FitChannel(int adc, int ch, string mode="mean")
    {
        const bool isMean = (mode == "mean");
        const bool isStd  = (mode == "std");
        if (!isMean && !isStd) {
            std::cerr << "BaselineCalibrator::FitChannel: invalid mode \""
                    << mode << "\".\n";
            return;
        }

        const std::vector<double>& samples =
            isMean ? fSamples[adc][ch] : fSamples_std[adc][ch];
        TH1F*& hist =
            isMean ? fHist[adc][ch] : fHist_std[adc][ch];
        TF1*& fit =
            isMean ? fFit[adc][ch] : fFit_std[adc][ch];
        ChannelBaseline& result = fResult[adc][ch];

        if (samples.empty()) {
            result.calibrated = false;
            return;
        }

        double minVal = *std::min_element(samples.begin(), samples.end());
        double maxVal = *std::max_element(samples.begin(), samples.end());

        double loEdge = minVal - 1.0;
        double hiEdge = maxVal + 1.0;
        int nBins = std::max(20, static_cast<int>((maxVal - minVal) * 3));



        const std::string fname =
            isMean
                ? "f_baseline_calib_" + std::to_string(adc) + "_" + std::to_string(ch)
                : "f_baselineSTD_calib_" + std::to_string(adc) + "_" + std::to_string(ch);
        hist = new TH1F(fname.c_str(), "", nBins, loEdge, hiEdge);
        for (double v : samples) hist->Fill(v);

        const double histMean = hist->GetMean();
        const double histStd  = hist->GetStdDev();

        // A zero-width distribution cannot support a meaningful Gaussian fit.
        if (histStd <= 0.0) {
            if (isMean) {
                result.mean  = histMean;
                result.sigma = 0.0;
            } else {
                result.STD       = histMean;
                result.STD_sigma = 0.0;
            }

            result.n_windows = samples.size();
            return;
        }

        const double fitMin = histMean - fCfg.fit_range_sigma * histStd;
        const double fitMax = histMean + fCfg.fit_range_sigma * histStd;


        const std::string hname =
            isMean
                ? "h_baseline_calib_" + std::to_string(adc) + "_" + std::to_string(ch)
                : "h_baselineSTD_calib_" + std::to_string(adc) + "_" + std::to_string(ch);

        fit = new TF1(fname.c_str(), "gaus", fitMin, fitMax);
        fit->SetParameters(hist->GetMaximum(), histMean, histStd);

        // "Q0NR": quiet, do not draw, do not store in histogram's list, use range
        TFitResultPtr fitResult = hist->Fit(fit, "NERSQ");
        const int fitStatus = fitResult;

        result.n_windows  = samples.size();
        if (fitResult.Get() && fitResult->IsValid()) {
            if (isMean) {
                result.mean  = fit->GetParameter(1);
                result.sigma = fit->GetParameter(2);
            } else {
                result.STD       = fit->GetParameter(1);
                result.STD_sigma = fit->GetParameter(2);
            }
            result.calibrated = true;
        } else {
            // Fit failed: store histogram mean as fallback, mark uncalibrated
                if (isMean) {
                    result.mean  = histMean;
                    result.sigma = histStd;
                } else {
                    result.STD       = histMean;
                    result.STD_sigma = histStd;
                }
            result.calibrated = false;
            std::cerr << "BaselineCalibrator: Gaussian fit rejected for ADC "
                    << adc << ", CH " << ch
                    << " | ROOT status = " << fitStatus
                    << " | result available = "
                    << (fitResult.Get() != nullptr)
                    << "\n";
        }

    }
    /// Write a one-page PDF report with eight vertically stacked plots:
    /// one baseline-versus-channel histogram for each ADC.
    ///
    /// Each TH1F has 64 bins, corresponding to channels 0--63.
    /// Non-selected channels remain empty. Bin contents are result.mean and
    /// bin errors are result.sigma.
    public:
    /*
    void PrintReport(
        const std::string& pdfFile = "baseline_report.pdf"
    ) const
    {
        if (fSelectedChannels.empty()) {
            std::cerr << "BaselineCalibrator::PrintReport: "
                    << "no channels were selected in the most recent calibration.\n";
            return;
        }

        // Must remain alive until canvas.Print(), because the canvas draws them.
        std::array<TH1F, kNumADCs> histograms;

        TCanvas canvas(
            "baseline_report_canvas",
            "Baseline calibration report",
            1400, 2200
        );

        canvas.Divide(1, kNumADCs, 0.0, 0.0);

        for (int adc = 0; adc < kNumADCs; ++adc) {
            TH1F& hist = histograms[adc];

            const std::string histName =
                "h_baseline_vs_channel_adc_" + std::to_string(adc);

            hist.SetName(histName.c_str());
            hist.SetTitle(
                Form("Baseline versus channel: ADC %d;Channel;Baseline (ADC counts)",
                    adc)
            );

            // One bin per integer channel: channel 0 -> bin 1, ..., channel 63 -> bin 64.
            hist.SetBins(kNumChannels, -0.5, kNumChannels - 0.5);
            hist.SetStats(false);

            hist.SetMarkerStyle(20);
            hist.SetMarkerSize(0.65);
            hist.SetMarkerColor(kBlue + 1);
            hist.SetLineColor(kBlue + 1);
            hist.SetLineWidth(2);

            // Fill only selected channels belonging to this ADC.
            for (const auto& channel : fSelectedChannels) {
                const int selectedAdc = channel.first;
                const int ch          = channel.second;

                if (selectedAdc != adc) continue;

                const ChannelBaseline& result = fResult[adc][ch];

                // Leave this selected bin empty if calibration found no windows.
                if (result.n_windows == 0) continue;

                const int bin = hist.FindBin(ch);

                hist.SetBinContent(bin, result.mean);
                hist.SetBinError(bin, result.sigma);
            }

            canvas.cd(adc + 1);

            gPad->SetGridx();
            gPad->SetGridy();
            gPad->SetLeftMargin(0.11);
            gPad->SetRightMargin(0.04);
            gPad->SetTopMargin(0.12);
            gPad->SetBottomMargin(0.18);

            // "E1" draws vertical error bars; "P" draws a marker at every
            // non-empty selected-channel bin.
            hist.Draw("E1 P");

            hist.GetXaxis()->SetNdivisions(kNumChannels / 4);
            hist.GetXaxis()->SetTitleSize(0.07);
            hist.GetXaxis()->SetLabelSize(0.055);

            hist.GetYaxis()->SetTitleSize(0.07);
            hist.GetYaxis()->SetLabelSize(0.055);
            hist.GetYaxis()->SetTitleOffset(0.60);
        }

        canvas.Modified();
        canvas.Update();

        canvas.Print((pdfFile+"(").c_str(), "pdf");


    }
        */
    void PrintReport(string pdfFile) const
    {
        DrawTGraph("mean",true,pdfFile+"(");
        DrawTGraph("std",true,pdfFile);
        Draw("mean",true,pdfFile);
        Draw("std",true,pdfFile+")");

        std::cout << "Baseline calibration report written to: "
                << pdfFile << "\n";
    }
    void DrawTGraph(string mode="mean", bool NotPause=false, string pdfFile="") const
    {
        const bool drawMean = (mode == "mean");
        const bool drawStd  = (mode == "std");

        if (!drawMean && !drawStd) {
            std::cerr << "BaselineCalibrator::DrawTGraph: mode must be \"mean\" or \"std\"; got \""
                    << mode << "\".\n";
            return;
        }

        TCanvas canvas(
            "baseline_report_canvas_tgraph",
            "Baseline calibration report (TGraph)",
            1400, 2200
        );
        canvas.Divide(1, kNumADCs, 0.0, 0.0);
        std::array<TH1F, kNumADCs> histograms;

        for (int adc = 0; adc < kNumADCs; ++adc) {
            TH1F& hist = histograms[adc];

            const std::string histName =
                "h_baseline_vs_channel_adc_" + std::to_string(adc);

            hist.SetName(histName.c_str());
            hist.SetTitle(
                Form("Baseline versus channel: ADC %d;Channel;Baseline (ADC counts)",
                    adc)
            );

            // One bin per integer channel: channel 0 -> bin 1, ..., channel 63 -> bin 64.
            hist.SetBins(kNumChannels, -0.5, kNumChannels - 0.5);
            hist.SetStats(false);

            hist.SetMarkerStyle(20);
            hist.SetMarkerSize(0.65);
            hist.SetMarkerColor(kBlue + 1);
            hist.SetLineColor(kBlue + 1);
            hist.SetLineWidth(2);

            // Fill only selected channels belonging to this ADC.
            for (const auto& channel : fSelectedChannels) {
                const int selectedAdc = channel.first;
                const int ch          = channel.second;

                if (selectedAdc != adc) continue;

                const ChannelBaseline& result = fResult[adc][ch];

                // Leave this selected bin empty if calibration found no windows.
                if (result.n_windows == 0) continue;

                const int bin = hist.FindBin(ch);

                hist.SetBinContent(bin, drawMean? result.mean : result.STD);
                hist.SetBinError(bin, drawMean ? result.sigma : result.STD_sigma);
            }

            canvas.cd(adc + 1);

            gPad->SetGridx();
            gPad->SetGridy();
            gPad->SetLeftMargin(0.11);
            gPad->SetRightMargin(0.04);
            gPad->SetTopMargin(0.12);
            gPad->SetBottomMargin(0.18);

            hist.Draw("E1 P");

            hist.GetXaxis()->SetNdivisions(kNumChannels / 4);
            hist.GetXaxis()->SetTitleSize(0.07);
            hist.GetXaxis()->SetLabelSize(0.055);

            hist.GetYaxis()->SetTitleSize(0.07);
            hist.GetYaxis()->SetLabelSize(0.055);
            hist.GetYaxis()->SetTitleOffset(0.60);

        }

        canvas.Modified();
        canvas.Update();
        canvas.Print((pdfFile+"(").c_str(), "pdf");
    }


};

} // namespace ndlar_light
