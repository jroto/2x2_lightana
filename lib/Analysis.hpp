#pragma once
//
// Analysis.hpp
//
// Processes a full Run event by event and waveform by waveform,
// accumulating results as a vector of EventAna. Does not own the Run
// (non-owning reference) - the caller must keep the Run alive for as
// long as the Analysis is used.
//

#include "Event.hpp"
#include "EventAna.hpp"
#include "MetaWaveformAna.hpp"
#include "Run.hpp"
#include "WaveformAna.hpp"
#include "WaveAna.hpp"
#include "Utils.hpp"
#include "HistCollection.hpp"

#include "TFile.h"
#include "TTree.h"
#include "TCanvas.h"
#include "TH1F.h"
#include "TPaveText.h"
#include "TText.h"
#include "TStyle.h"
#include "TSystem.h"
#include "TLine.h"
#include "TBox.h"
#include "TMarker.h"

#include <cmath>
#include <cstddef>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ndlar_light {

/// Factory signature for constructing a concrete MetaWaveformAna from a
/// raw Waveform plus its Event-level validity flag. Lets Analysis be
/// pluggable: pass a custom factory to run a different analysis
/// implementation without changing Analysis/EventAna/Dump.
using WaveformAnaFactory =
    std::function<std::unique_ptr<MetaWaveformAna>(const Waveform&, bool isValid)>;

/// Default factory, constructing the standard WaveformAna (mean-only,
/// for now).
inline WaveformAnaFactory DefaultWaveformAnaFactory()
{
    return [](const Waveform& wf, bool isValid) {
        return std::make_unique<WaveformAna>(wf, isValid);
    };
}

class Analysis {
public:
    /// `run` is stored as a reference; Analysis does not manage its
    /// lifetime. `factory` selects which concrete MetaWaveformAna
    /// implementation to construct per waveform - defaults to the
    /// standard WaveformAna, but callers can supply their own to plug in
    /// a different analysis without touching Analysis/EventAna/Dump.
    explicit Analysis(Run& run, WaveformAnaFactory factory = DefaultWaveformAnaFactory())
        : fRun(run), fFactory(std::move(factory)) {}

    /// Iterates the referenced Run from the start (calls Run::Reset()
    /// first, so process() always covers the full run regardless of any
    /// prior external iteration), building one EventAna per raw Event.
    /// Reads one raw Event at a time via Run's existing streaming API -
    /// never loads the whole run into memory as raw Events.
    void process(int maxEvents = -1)
    {
        fEvents.clear();
        fRun.Reset();

        int counter = 0;

        while (fRun.HasNext()) {
            if (maxEvents >= 0 && counter >= maxEvents) {
                break;
            }

            const Event& event = fRun.NextEvent();

            if (counter % 1000 == 0) {
                std::cout << "Processing event "
                        << event.Meta().GetId()
                        << " (event number "
                        << event.Meta().GetEventNumber()
                        << ") out of "
                        << fRun.TotalEvents()
                        << "\n";
            }

            fEvents.push_back(AnalyzeEvent(event));
            ++counter;
        }
    }
    EventAna AnalyzeEvent(const Event& event)
    {
        EventAna ana;
        ana.Meta() = event.Meta();

        for (int adc = 0; adc < kNumADCs; ++adc) {
            for (int ch = 0; ch < kNumChannels; ++ch) {
                const bool valid = event.IsValid(adc, ch);

                auto ptr = fFactory(
                    event.GetWaveform(adc, ch),
                    valid
                );

                ana.SetWaveformAna(adc, ch, std::move(ptr));
            }
        }

        return ana;
    }
    EventAna AnalyzeEventByIndex(std::size_t globalIndex)
    {
        return AnalyzeEvent(fRun.GetEvent(globalIndex));
    }
    /// Results accumulated by the most recent process() call.
    const std::vector<EventAna>& GetEvents() const { return fEvents; }

    size_t GetNEvents() const { return fEvents.size(); }

    /// Dumps the current analysis results (fEvents) into a ROOT file as a
    /// TTree with one entry per (event, adc, channel) waveform. Uses only
    /// the already-computed contents of fEvents - does NOT re-read the
    /// Run or call process(). Analysis-variable branches are created for
    /// every parameter name registered in the shared
    /// MetaWaveformAna registry (MetaWaveformAna::ParamNames()) - since
    /// every concrete analyzer registers its keys once via
    /// RegisterParam(), this remains dynamic: new analysis quantities
    /// automatically appear here without touching this method.
    void Dump(const std::string& filename, const std::string& treename = "waveforms") const
    {
        // Full set of registered analysis parameter names - assumed to be
        // the complete union across all analyzer implementations, since
        // each registers its keys once via MetaWaveformAna::RegisterParam().
        const auto& allNames = MetaWaveformAna::ParamNames();

        TFile outfile(filename.c_str(), "RECREATE");
        if (outfile.IsZombie()) {
            throw std::runtime_error("Analysis::Dump: failed to create ROOT file '" + filename + "'");
        }

        TTree* tree = new TTree(treename.c_str(), "NDLAr light analysis per waveform");

        // Event-level
        ULong64_t event_id;
        Int_t event_number;
        UChar_t trig_type;
        Int_t sn;
        ULong64_t utime_ms;
        ULong64_t tai_ns;

        // Waveform-level
        Int_t adc;
        Int_t channel;
        Bool_t valid;
        Bool_t clipped;

        tree->Branch("event_id", &event_id, "event_id/l");
        tree->Branch("event_number", &event_number, "event_number/I");
        tree->Branch("trig_type", &trig_type, "trig_type/b");

        tree->Branch("sn", &sn, "sn/I");
        tree->Branch("utime_ms", &utime_ms, "utime_ms/l");
        tree->Branch("tai_ns", &tai_ns, "tai_ns/l");

        tree->Branch("adc", &adc, "adc/I");
        tree->Branch("channel", &channel, "channel/I");
        tree->Branch("valid", &valid, "valid/O");
        tree->Branch("clipped", &clipped, "clipped/O");

        // Dynamic analysis-variable branches, one Double_t per registered
        // parameter name, addressed by registry index at fill time.
        struct ParamBranch {
            std::string name;
            std::size_t index;
            Double_t value;
        };

        // Elements are constructed in-place in their final vector slots
        // first (via resize()), and only then does each tree->Branch()
        // take the address of that (now-stable) vector element - binding
        // the branch to the address of a temporary local variable that
        // then gets copied into the vector (e.g. via push_back) would
        // leave the branch pointing at stack memory that's reused/
        // invalidated on the next loop iteration.
        std::vector<ParamBranch> paramBranches(allNames.size());
        for (std::size_t i = 0; i < allNames.size(); ++i) {
            ParamBranch& pb = paramBranches[i];
            pb.name = allNames[i];
            pb.index = MetaWaveformAna::ParamIndex(pb.name);
            pb.value = std::numeric_limits<double>::quiet_NaN();
            tree->Branch(pb.name.c_str(), &pb.value, (pb.name + "/D").c_str());
        }

        for (const auto& eventAna : fEvents) {
            const EventMetadata& meta = eventAna.Meta();

            for (int adc_idx = 0; adc_idx < kNumADCs; ++adc_idx) {
                sn = meta.GetSerialNumber(adc_idx);
                utime_ms = meta.GetUTimeMs(adc_idx);
                tai_ns = meta.GetTaiNs(adc_idx);

                for (int ch_idx = 0; ch_idx < kNumChannels; ++ch_idx) {
                    const MetaWaveformAna& wa = eventAna.GetWaveformAna(adc_idx, ch_idx);

                    event_id = meta.GetId();
                    event_number = meta.GetEventNumber();
                    trig_type = meta.GetTriggerType();

                    adc = adc_idx;
                    channel = ch_idx;
                    valid = meta.IsValid(adc_idx, ch_idx);
                    clipped = wa.IsClipped();

                    for (auto& pb : paramBranches) {
                        pb.value = wa.HasParamIndex(pb.index)
                                       ? wa.GetParamByIndex(pb.index)
                                       : std::numeric_limits<double>::quiet_NaN();
                    }

                    tree->Fill();
                }
            }
        }

        outfile.cd();
        tree->Write();
        outfile.Close();
        std::cout << "Analysis::Dump: wrote " << tree->GetEntries()
                  << " entries to '" << filename << "'\n";
    }

    /// Interactive, on-the-fly event display: iterates the referenced Run
    /// (calling fRun.Reset() internally - process() need NOT have been
    /// called first, and if it *has* been called, Loop() still re-reads
    /// the run from the beginning, independent of fEvents) and, for each
    /// event, draws the waveforms of every active+valid (adc, channel)
    /// slot in a dynamically-laid-out TCanvas grid, overlaying computed
    /// analysis parameters, then waits for the user to press Enter (next
    /// event) or 'q' + Enter (quit). Stops after `maxEvents` events (if
    /// positive), when the run is exhausted, or when the user quits.
    void Loop(int maxEvents = -1)
    {
        const ChannelMap& chmap = fRun.GetChannelMap();

        // --- Collect active channels from the ChannelMap ---
        std::vector<std::pair<int, int>> activeChannels; // (adc, ch)
        for (int adc = 0; adc < kNumADCs; ++adc)
            for (int ch = 0; ch < kNumChannels; ++ch)
                if (chmap.IsActive(adc, ch))
                    activeChannels.emplace_back(adc, ch);

        if (activeChannels.empty()) {
            std::cout << "Analysis::Loop: no active channels in ChannelMap. "
                      << "Use Run::SelectChannel() to activate channels.\n";
            return;
        }

        // --- Compute canvas grid dimensions ---
        const int nPads = static_cast<int>(activeChannels.size());
        const int nCols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(nPads))));
        const int nRows = static_cast<int>(std::ceil(static_cast<double>(nPads) / nCols));

        // --- Create canvas ---
        TCanvas* canvas = new TCanvas("Loop_canvas", "Analysis::Loop", 200, 10, 800, 700);
        canvas->Divide(nCols, nRows);
        gStyle->SetOptStat(0);

        // --- Reset run and iterate ---
        fRun.Reset();
        int eventCount = 0;

        while (fRun.HasNext()) {
            if (maxEvents > 0 && eventCount >= maxEvents) break;

            const Event& event = fRun.NextEvent();
            ++eventCount;

            // Collect valid waveforms for active channels (a channel may
            // be active in the map but flagged invalid in this event).
            std::vector<std::pair<int, int>> validChannels;
            for (std::size_t i = 0; i < activeChannels.size(); ++i) {
                int adc = activeChannels[i].first;
                int ch = activeChannels[i].second;
                if (event.IsValid(adc, ch))
                    validChannels.emplace_back(adc, ch);
            }

            if (validChannels.empty()) {
                std::cout << "Analysis::Loop: event " << eventCount
                          << " has no valid active channels - skipping.\n";
                continue;
            }

            // --- Draw each waveform ---
            const auto& paramNames = MetaWaveformAna::ParamNames();

            int padIdx = 1;
            for (std::size_t i = 0; i < activeChannels.size(); ++i) {
                int adc = activeChannels[i].first;
                int ch = activeChannels[i].second;

                canvas->cd(padIdx++);
                gPad->Clear();

                if (!event.IsValid(adc, ch)) {
                    // Draw a blank pad with a label for inactive/invalid slots
                    TPaveText* msg = new TPaveText(0.1, 0.4, 0.9, 0.6, "NDC");
                    msg->AddText(Form("ADC %d / CH %d", adc, ch));
                    msg->AddText("(inactive / invalid)");
                    msg->SetFillColor(0);
                    msg->SetTextColor(kGray + 1);
                    msg->Draw();
                    continue;
                }

                const Waveform& wf = event.GetWaveform(adc, ch);
                std::unique_ptr<MetaWaveformAna> ptr = fFactory(wf, event.IsValid(adc, ch));
                WaveAna* wa = dynamic_cast<WaveAna*>(ptr.get());

                // Build TH1F for this waveform
                std::string hname = Form("h_adc%d_ch%d_ev%d", adc, ch, eventCount);
                TH1F* h = new TH1F(hname.c_str(), "", static_cast<int>(kNumSamples), 0, static_cast<double>(kNumSamples));
                for (int s = 0; s < static_cast<int>(kNumSamples); ++s)
                    h->SetBinContent(s + 1, wf.GetSample(s));

                // Title: channel identity + physical info
                const Channel& info = chmap.GetChannel(adc, ch);
                std::string title = Form(
                    "ADC %d / CH %d | TPC %d | trap: %s;Ticks;ADC counts",
                    adc, ch, info.tpc, info.trap_type.c_str());
                h->SetTitle(title.c_str());
                h->SetLineColor(kBlue + 1);
                h->Draw("HIST");

                if (wa != nullptr) {
                    // --- WaveAna-specific overlays: baselines and hits ---
                    for (const auto& seg : wa->Baselines()) {
                        TLine* line = new TLine(seg.tick_start, seg.mean, seg.tick_end, seg.mean);
                        line->SetLineColor(kGreen + 2);
                        line->SetLineWidth(2);
                        line->Draw();
                    }

                    double baseline = wa->OverallBaseline();
                    for (const auto& hit : wa->Hits()) {
                        TBox* box = new TBox(hit.tick_start, baseline,
                                              hit.tick_end, baseline + hit.amplitude);
                        box->SetFillColor(kRed);
                        box->SetFillStyle(3003);
                        box->Draw();

                        TMarker* marker = new TMarker(hit.tick_peak, wf.GetSample(hit.tick_peak), 20);
                        marker->SetMarkerColor(kRed);
                        marker->Draw();
                    }
                }

                // Overlay parameters as TPaveText
                if (!paramNames.empty()) {
                    TPaveText* pt = new TPaveText(0.55, 0.72, 0.98, 0.98, "NDC");
                    pt->SetFillColor(0);
                    pt->SetFillStyle(1001);
                    pt->SetBorderSize(1);
                    pt->SetTextSize(0.04);
                    for (std::size_t p = 0; p < paramNames.size(); ++p) {
                        if (ptr->HasParamIndex(p)) {
                            std::string line = Form("%s = %.4g",
                                paramNames[p].c_str(),
                                ptr->GetParamByIndex(p));
                            pt->AddText(line.c_str());
                        }
                    }
                    pt->Draw();
                }

                gPad->Update();
            }

            // Event-level title on the canvas
            canvas->cd(0);
            std::string canvasTitle = Form(
                "Event %d  |  ID %llu  |  [Enter] next   [q] quit",
                eventCount,
                static_cast<unsigned long long>(event.Meta().GetId()));
            canvas->SetTitle(canvasTitle.c_str());
            canvas->Update();

            // --- Wait for user input ---
            if (!PauseExecution("Event " + std::to_string(eventCount)
                                + " | [Enter] next   [q] quit: "))
                break;

            // Clean up histograms before next event to avoid ROOT memory buildup
            canvas->Clear();
            canvas->Divide(nCols, nRows);
        }

        std::cout << "Analysis::Loop: finished after " << eventCount << " events.\n";
    }

    /// Interactive two-row event display with cumulative per-channel charge spectra.
    ///
    /// Layout: the canvas is divided into N columns × 2 rows, where N is the
    /// number of selected channels.
    ///
    ///   Top row    (pads 1 .. N):     per-event waveform display, identical to
    ///                                  Loop() — waveform, baseline segments,
    ///                                  hit boxes/markers, analysis parameters.
    ///   Bottom row (pads N+1 .. 2N):  cumulative histogram of individual Hit::charge
    ///                                  values accumulated from the start of this
    ///                                  Loop2() invocation through the current event.
    ///                                  The histogram is NOT reset between events.
    ///
    /// Does not require process() to have been called first; resets the Run
    /// internally. If the factory does not produce WaveAna the top row still
    /// works normally and the bottom histograms simply remain empty.
    ///
    /// Navigation: [Enter] = next event, [q+Enter] = quit.
    void Loop2(int maxEvents = -1)
    {
        // --- Snapshot selected channels once (ADC-major, channel-major) ---
        const auto selectedChannels = fRun.GetSelectedChannels();

        const int nSelected = static_cast<int>(selectedChannels.size());

        if (nSelected == 0) {
            std::cout << "Analysis::Loop2: no active channels in ChannelMap. "
                      << "Use Run::SelectChannel() to activate channels.\n";
            return;
        }

        // --- Create canvas: N columns × 2 rows ---
        const int canvasWidth  = std::max(1200, 450 * nSelected);
        const int canvasHeight = 900;

        // Use a unique instance counter to avoid ROOT name clashes across calls.
        static std::size_t sLoop2Instance = 0;
        const std::size_t instance = sLoop2Instance++;

        std::string cname = Form("Loop2_canvas_%zu", instance);
        TCanvas* canvas = new TCanvas(cname.c_str(), "Analysis::Loop2",
                                      200, 10, canvasWidth, canvasHeight);
        canvas->Divide(nSelected, 2);
        gStyle->SetOptStat(0);

        // --- Create persistent per-channel charge histograms (one per selected ch) ---
        constexpr int    kChargeHistBins = 200;
        constexpr double kChargeHistMin  = 0.0;
        constexpr double kChargeHistMax  = 30000.0;

        std::vector<TH1F*> chargeHists(static_cast<std::size_t>(nSelected), nullptr);
        for (int i = 0; i < nSelected; ++i) {
            int adc = selectedChannels[static_cast<std::size_t>(i)].first;
            int ch  = selectedChannels[static_cast<std::size_t>(i)].second;
            std::string hname = Form("h_hit_charge_adc%d_ch%d_loop2%zu", adc, ch, instance);
            TH1F* hc = new TH1F(hname.c_str(),
                                 Form("ADC %d / CH %d cumulative hit charge", adc, ch),
                                 kChargeHistBins, kChargeHistMin, kChargeHistMax);
            hc->SetDirectory(nullptr); // decouple from ROOT directory
            hc->GetXaxis()->SetTitle("Hit charge (ADC counts #times ticks)");
            hc->GetYaxis()->SetTitle("Hits");
            hc->SetLineColor(kBlue + 1);
            hc->SetFillColor(kAzure + 7);
            hc->SetFillStyle(1001);
            chargeHists[static_cast<std::size_t>(i)] = hc;
        }

        // --- Reset run and iterate ---
        fRun.Reset();
        int eventCount = 0;
        const ChannelMap& chmap   = fRun.GetChannelMap();
        const auto& paramNames    = MetaWaveformAna::ParamNames();

        while (fRun.HasNext()) {
            if (maxEvents > 0 && eventCount >= maxEvents) break;

            const Event& event = fRun.NextEvent();
            ++eventCount;

            // --- Top row: clear & redraw waveform pads ---
            for (int i = 0; i < nSelected; ++i) {
                int adc = selectedChannels[static_cast<std::size_t>(i)].first;
                int ch  = selectedChannels[static_cast<std::size_t>(i)].second;

                canvas->cd(i + 1);  // top row: pads 1..N
                gPad->Clear();

                if (!event.IsValid(adc, ch)) {
                    TPaveText* msg = new TPaveText(0.1, 0.4, 0.9, 0.6, "NDC");
                    msg->AddText(Form("ADC %d / CH %d", adc, ch));
                    msg->AddText("(inactive / invalid)");
                    msg->SetFillColor(0);
                    msg->SetTextColor(kGray + 1);
                    msg->Draw();
                    gPad->Update();
                    continue;
                }

                const Waveform& wf = event.GetWaveform(adc, ch);
                std::unique_ptr<MetaWaveformAna> ptr = fFactory(wf, true);
                WaveAna* wa = dynamic_cast<WaveAna*>(ptr.get());

                // Waveform histogram
                std::string hname = Form("h2_adc%d_ch%d_ev%d_inst%zu",
                                         adc, ch, eventCount, instance);
                TH1F* h = new TH1F(hname.c_str(), "",
                                    static_cast<int>(kNumSamples), 0,
                                    static_cast<double>(kNumSamples));
                for (int s = 0; s < static_cast<int>(kNumSamples); ++s)
                    h->SetBinContent(s + 1, wf.GetSample(s));

                const Channel& info = chmap.GetChannel(adc, ch);
                std::string title = Form(
                    "ADC %d / CH %d | TPC %d | trap: %s;Ticks;ADC counts",
                    adc, ch, info.tpc, info.trap_type.c_str());
                h->SetTitle(title.c_str());
                h->SetLineColor(kBlue + 1);
                h->Draw("HIST");

                if (wa != nullptr) {
                    // Baseline segments
                    for (const auto& seg : wa->Baselines()) {
                        TLine* line = new TLine(seg.tick_start, seg.mean,
                                                seg.tick_end,   seg.mean);
                        line->SetLineColor(kGreen + 2);
                        line->SetLineWidth(2);
                        line->Draw();
                    }

                    // Hit boxes and peak markers
                    double baseline = wa->OverallBaseline();
                    for (const auto& hit : wa->Hits()) {
                        TBox* box = new TBox(hit.tick_start, baseline,
                                              hit.tick_end,   baseline + hit.amplitude);
                        box->SetFillColor(kRed);
                        box->SetFillStyle(3003);
                        box->Draw();

                        TMarker* marker = new TMarker(
                            hit.tick_peak,
                            static_cast<double>(wf.GetSample(
                                static_cast<std::size_t>(hit.tick_peak))),
                            20);
                        marker->SetMarkerColor(kRed);
                        marker->Draw();

                        // Accumulate individual hit charge into persistent histogram
                        chargeHists[static_cast<std::size_t>(i)]->Fill(hit.charge);
                    }
                }

                // Analysis-parameter overlay
                if (!paramNames.empty()) {
                    TPaveText* pt = new TPaveText(0.55, 0.72, 0.98, 0.98, "NDC");
                    pt->SetFillColor(0);
                    pt->SetFillStyle(1001);
                    pt->SetBorderSize(1);
                    pt->SetTextSize(0.04);
                    for (std::size_t p = 0; p < paramNames.size(); ++p) {
                        if (ptr->HasParamIndex(p)) {
                            std::string pline = Form("%s = %.4g",
                                paramNames[p].c_str(),
                                ptr->GetParamByIndex(p));
                            pt->AddText(pline.c_str());
                        }
                    }
                    pt->Draw();
                }

                gPad->Update();
            }

            // --- Bottom row: redraw cumulative charge histograms ---
            for (int i = 0; i < nSelected; ++i) {
                canvas->cd(nSelected + i + 1);  // bottom row: pads N+1..2N
                gPad->Clear();
                chargeHists[static_cast<std::size_t>(i)]->Draw("HIST");
                gPad->Update();
            }

            // Canvas title
            canvas->cd(0);
            std::string canvasTitle = Form(
                "Loop2 | Event %d | ID %llu | [Enter] next | [q] quit",
                eventCount,
                static_cast<unsigned long long>(event.Meta().GetId()));
            canvas->SetTitle(canvasTitle.c_str());
            canvas->Update();

            // Pause for user input (processes ROOT GUI events while waiting)
            if (!PauseExecution("Loop2 Event " + std::to_string(eventCount)
                                + " | [Enter] next   [q] quit: "))
                break;
        }

        std::cout << "Analysis::Loop2: finished after " << eventCount << " events.\n";

        // Clean up charge histograms when done
        for (int i = 0; i < nSelected; ++i) {
            delete chargeHists[static_cast<std::size_t>(i)];
        }
    }

//private:
    Run& fRun;
    WaveformAnaFactory fFactory;
    std::vector<EventAna> fEvents;
};

} // namespace ndlar_light
