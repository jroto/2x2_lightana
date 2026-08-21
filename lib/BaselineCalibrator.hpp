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
                }
            }
        }

        // Fit only the channels selected for this calibration.
        for (const auto& channel : fSelectedChannels) {
            FitChannel(channel.first, channel.second);
        }
    }
    /// Access the calibrated baseline for a channel.
    const ChannelBaseline& GetBaseline(int adc, int ch) const
    {
        return fResult[adc][ch];
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

            os << std::left
               << std::setw(6)  << adc
               << std::setw(6)  << ch
               << std::setw(12) << b.mean
               << std::setw(12) << b.sigma
               << std::setw(10) << b.n_windows
               << b.calibrated
               << "\n";
        }
    }
    /// Draw calibrated histograms and fits on a TCanvas, one pad per channel.
    /// Pauses for user input via PauseExecution().
    void Draw()
    {
        // Collect selected channels that have a histogram
        // (the fit itself may have failed).
        std::vector<std::pair<int,int>> channels;
        for (int adc = 0; adc < kNumADCs; ++adc)
            for (int ch = 0; ch < kNumChannels; ++ch)
                if (fHist[adc][ch] != nullptr)
                    channels.emplace_back(adc, ch);

        if (channels.empty()) {
            std::cout << "BaselineCalibrator::Draw: no calibrated channels to draw.\n";
            return;
        }

        // Dynamic grid layout
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
        canvas->SetTitle("Baseline Calibration — selected channels");
        canvas->Update();

        // Pause execution
        PauseExecution("Baseline calibration drawn | [Enter] continue   [q] quit: ");
    }

private:
    CalibratorConfig fCfg;
    Baseline         fBaseline;
    ChannelBaseline  fResult[kNumADCs][kNumChannels];

    // Snapshot of Run-selected channels used by the latest Calibrate() call.
    std::vector<std::pair<int, int>> fSelectedChannels;

    // Raw accumulated baseline means per channel, kept for histogram/fit.
    std::vector<double> fSamples[kNumADCs][kNumChannels];

    /// Retained per-channel histogram and Gaussian fit for Draw().
    /// Indexed [adc][ch]. nullptr if channel was never calibrated
    /// (no baseline samples accumulated).
    TH1F* fHist[kNumADCs][kNumChannels];
    TF1*  fFit [kNumADCs][kNumChannels];

    /// Delete ROOT objects and reset all state from a prior calibration.
    /// Safe to call when no calibration has been performed yet.
    void ClearCalibration()
    {
        for (int adc = 0; adc < kNumADCs; ++adc) {
            for (int ch = 0; ch < kNumChannels; ++ch) {
                delete fHist[adc][ch];
                delete fFit[adc][ch];

                fHist[adc][ch] = nullptr;
                fFit[adc][ch]  = nullptr;

                fSamples[adc][ch].clear();
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
    void FitChannel(int adc, int ch)
    {
        std::vector<double>& samples = fSamples[adc][ch];
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

        std::string hname = "h_baseline_calib_" + std::to_string(adc) + "_" + std::to_string(ch);
        fHist[adc][ch] = new TH1F(hname.c_str(), "", nBins, loEdge, hiEdge);
        for (double v : samples) fHist[adc][ch]->Fill(v);

        double h_mean = fHist[adc][ch]->GetMean();
        double h_rms  = fHist[adc][ch]->GetRMS();

        double fit_min = h_mean - fCfg.fit_range_sigma * h_rms;
        double fit_max = h_mean + fCfg.fit_range_sigma * h_rms;

        std::string fname = "f_baseline_calib_" + std::to_string(adc) + "_" + std::to_string(ch);
        fFit[adc][ch] = new TF1(fname.c_str(), "gaus", fit_min, fit_max);
        fFit[adc][ch]->SetParameters(fHist[adc][ch]->GetMaximum(), h_mean, h_rms);

        // "Q0NR": quiet, do not draw, do not store in histogram's list, use range
        TFitResultPtr fitResult = fHist[adc][ch]->Fit(fFit[adc][ch], "NERSQ");
        const int fitStatus = fitResult;

        result.n_windows  = samples.size();
        if (fitResult.Get() && fitResult->IsValid()) {
            result.mean       = fFit[adc][ch]->GetParameter(1);
            result.sigma      = fFit[adc][ch]->GetParameter(2);
            result.calibrated = true;
        } else {
            // Fit failed: store histogram mean as fallback, mark uncalibrated
            result.mean       = h_mean;
            result.sigma      = h_rms;
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

        // A normal .pdf filename writes one PDF page.
        canvas.Print(pdfFile.c_str());

        std::cout << "Baseline calibration report written to: "
                << pdfFile << "\n";
    }
};

} // namespace ndlar_light
