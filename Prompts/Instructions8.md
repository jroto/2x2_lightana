Prompt: Add Analysis::Loop() — interactive per-event waveform display
Context

    Namespace: ndlar_light. Header-only, ROOT/ACLiC/Cling compatible.
    Analysis class lives in lib/Analysis.hpp. It holds a reference to a Run object (fRun) and a vector of processed EventAna objects (fEvents).
    Run exposes Reset(), HasNext(), NextEvent(), and GetChannelMap().
    ChannelMap exposes IsActive(adc, ch) and GetChannel(adc, ch) (returns a Channel with tpc, x, y, z, trap_type).
    WaveformAna is constructed as WaveformAna(const Waveform& wf, bool isValid) and exposes GetMean(), HasParamIndex(idx), GetParamByIndex(idx), and the static registry MetaWaveformAna::ParamNames().
    Event exposes IsValid(adc, ch), GetWaveform(adc, ch) (returns const Waveform&), and Meta().
    Waveform exposes GetSample(i) and Size() (= kNumSamples = 600).
    Navigation: [Enter] = next event, [q + Enter] = quit .
    kNumADCs = 8, kNumChannels = 64.

Goal

Add a Loop(int maxEvents = -1) method to Analysis that:

    Runs on the fly — calls fRun.Reset() internally, does not require process() to have been called first.
    For each event, constructs WaveformAna only for active+valid channels, draws their waveforms in a dynamically-laid-out TCanvas, overlays all computed parameters, and waits for user input.
    Uses getchar() in the terminal for navigation: Enter = next, q = quit .
    If no active+valid channels exist for an event, prints a warning to std::cout and advances automatically.
    Stops after maxEvents events (if maxEvents > 0), or when the run is exhausted, or when the user quits.

Implementation

Add the following method to the Analysis class in lib/Analysis.hpp. Add the required ROOT includes at the top of the file if not already present:

cpp

#include "TCanvas.h"
#include "TH1F.h"
#include "TPaveText.h"
#include "TText.h"
#include "TStyle.h"
#include "TSystem.h"

Method signature

cpp

void Loop(int maxEvents = -1);

Full implementation

cpp

void Loop(int maxEvents = -1) {

    const ChannelMap& chmap = fRun.GetChannelMap();

    // --- Collect active channels from the ChannelMap ---
    std::vector<std::pair<int,int>> activeChannels; // (adc, ch)
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
    TCanvas* canvas = new TCanvas("Loop_canvas", "Analysis::Loop", 200, 10, 1400, 900);
    canvas->Divide(nCols, nRows);
    gStyle->SetOptStat(0);

    // --- Reset run and iterate ---
    fRun.Reset();
    int eventCount = 0;

    while (fRun.HasNext()) {
        if (maxEvents > 0 && eventCount >= maxEvents) break;

        const Event& event = fRun.NextEvent();
        ++eventCount;

        // Collect valid waveforms for active channels
        // (a channel may be active in the map but flagged invalid in this event)
        std::vector<std::pair<int,int>> validChannels;
        for (const auto& [adc, ch] : activeChannels)
            if (event.IsValid(adc, ch))
                validChannels.emplace_back(adc, ch);

        if (validChannels.empty()) {
            std::cout << "Analysis::Loop: event " << eventCount
                      << " has no valid active channels — skipping.\n";
            continue;
        }

        // --- Draw each waveform ---
        const auto& paramNames = MetaWaveformAna::ParamNames();

        int padIdx = 1;
        for (const auto& [adc, ch] : activeChannels) {
            canvas->cd(padIdx++);
            gPad->Clear();

            if (!event.IsValid(adc, ch)) {
                // Draw a blank pad with a label for inactive/invalid slots
                TPaveText* msg = new TPaveText(0.1, 0.4, 0.9, 0.6, "NDC");
                msg->AddText(Form("ADC %d / CH %d", adc, ch));
                msg->AddText("(inactive / invalid)");
                msg->SetFillColor(0);
                msg->SetTextColor(kGray+1);
                msg->Draw();
                continue;
            }

            const Waveform& wf = event.GetWaveform(adc, ch);
            WaveformAna wa(wf, true);

            // Build TH1F for this waveform
            std::string hname = Form("h_adc%d_ch%d_ev%d", adc, ch, eventCount);
            TH1F* h = new TH1F(hname.c_str(), "", kNumSamples, 0, kNumSamples);
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

            // Overlay parameters as TPaveText
            if (!paramNames.empty()) {
                TPaveText* pt = new TPaveText(0.55, 0.72, 0.98, 0.98, "NDC");
                pt->SetFillColor(0);
                pt->SetFillStyle(1001);
                pt->SetBorderSize(1);
                pt->SetTextSize(0.04);
                for (std::size_t i = 0; i < paramNames.size(); ++i) {
                    if (wa.HasParamIndex(i)) {
                        std::string line = Form("%s = %.4g",
                            paramNames[i].c_str(),
                            wa.GetParamByIndex(i));
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
        std::cout << "Event " << eventCount
                  << " — [Enter] next, [q+Enter] quit: " << std::flush;
        std::string line;
        std::getline(std::cin, line);
        if (!line.empty() && (line[0] == 'q' || line[0] == 'Q')) break;

        // Clean up histograms before next event to avoid ROOT memory buildup
        canvas->Clear();
        canvas->Divide(nCols, nRows);
    }

    std::cout << "Analysis::Loop: finished after " << eventCount << " events.\n";
}

Notes for the agent

    Includes: add the ROOT includes listed above to Analysis.hpp if not already present. Also add #include <cmath> and #include <string> if missing.

    Memory: TH1F and TPaveText objects are created per event inside ROOT's current directory. canvas->Clear() at the end of each iteration cleans up owned pad contents. This is sufficient for an interactive tool — no explicit delete needed per iteration.

    fRun access: Loop() calls fRun.Reset() which resets the sequential iteration state. If the user has already called process(), calling Loop() afterwards will re-read from the beginning — this is expected and should be noted in a comment.

    No changes to Run, Event, WaveformAna, ChannelMap, or any other file. All changes are confined to lib/Analysis.hpp.

    std::cin / getchar(): use std::getline(std::cin, line) rather than getchar() to correctly consume the full line including any buffered newline from previous input.

    Cling compatibility: avoid auto structured bindings (const auto& [adc, ch]) if the target ROOT version uses a C++14 Cling — replace with explicit int adc = p.first; int ch = p.second; inside the loops if needed.
