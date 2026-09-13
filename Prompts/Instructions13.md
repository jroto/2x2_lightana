Use this prompt for your agent:

text

Modify the current `main` branch of https://github.com/jroto/2x2_lightana.

## Goal

Add a new public method to `ndlar_light::Analysis`:

```cpp
void Loop2(int maxEvents = -1);

Loop2() is a second interactive event-display mode.

It must iterate through the Run event by event, stop after every event, and show a canvas with exactly:

    N columns;
    2 rows;

where N is the number of currently selected channels in the Run channel map.

For each selected channel:

    top row: the same waveform display currently produced by Analysis::Loop(), including waveform, baseline segments, hit boxes, hit peak markers, and registered analysis parameters;
    bottom row: one persistent histogram of the charge of every individual hit found in that channel, accumulated from the beginning of the Loop2() invocation through the currently displayed event.

The method must not modify the behavior or public signature of the existing Analysis::Loop().
Current code context

Relevant existing behavior:

    Analysis::Loop():

        reads the run directly, without requiring process();

        resets the run before iteration;

        identifies selected channels through the active entries of Run’s ChannelMap;

        creates MetaWaveformAna objects through fFactory;

        detects the hit-aware implementation with:

        cpp

        WaveAna* wa = dynamic_cast<WaveAna*>(ptr.get());

        draws WaveAna::Baselines() and WaveAna::Hits().

    WaveAna::Hits() returns:

    cpp

    const std::vector<Hit>& Hits() const;

    Each Hit contains:

    cpp

    double charge;

    This is the quantity to fill in the new lower-row histograms.

    Only WaveAna exposes individual hits. Loop2() must still work with another MetaWaveformAna implementation or the default WaveformAna factory: in that case, the waveform row must still display normally, while charge histograms remain empty.

Required implementation
1. Add Analysis::Loop2()

Implement this method in lib/Analysis.hpp, in the public section, immediately after the existing Loop() method:

cpp

void Loop2(int maxEvents = -1);

Document it clearly.

The method must:

    obtain a stable snapshot of selected channels before iterating;
    return safely with an explanatory message if no channels are selected;
    call fRun.Reset();
    iterate over the run using HasNext() and NextEvent();
    stop after maxEvents when maxEvents > 0;
    pause after every event, including events where all selected channels are invalid;
    stop when the user types q then Enter through PauseExecution().

Use the existing selected-channel API if it is available:

cpp

const auto selectedChannels = fRun.GetSelectedChannels();

If that method is not present in the current checkout, obtain the same snapshot by traversing fRun.GetChannelMap() once, exactly as Loop() currently does. Do not repeatedly scan all ADC/channel slots inside the per-event loop.

The selected-channel order must remain deterministic: ADC-major, then channel-major.
2. Canvas layout

Create one canvas for the full Loop2() call.

The canvas layout must be:

cpp

canvas->Divide(nSelectedChannels, 2);

where:

cpp

const int nSelectedChannels =
    static_cast<int>(selectedChannels.size());

The pad mapping must be:
Pad	Content
i + 1	waveform for selected channel i
nSelectedChannels + i + 1	cumulative charge histogram for selected channel i

Thus, each channel occupies one column:

text

channel 0       channel 1       ...       channel N - 1
waveform         waveform                  waveform
charge spectrum  charge spectrum           charge spectrum

Use a canvas width that scales with the selected-channel count so each column is reasonably visible. For example:

cpp

const int canvasWidth = std::max(1200, 450 * nSelectedChannels);
const int canvasHeight = 900;

Use a unique canvas name distinct from the existing "Loop_canvas", for example:

cpp

"Loop2_canvas"

Set:

cpp

gStyle->SetOptStat(0);

3. Persistent per-channel charge histograms

At the start of each Loop2() invocation, create one charge histogram per selected channel.

Use these fixed binning defaults:

cpp

constexpr int    kChargeHistBins = 200;
constexpr double kChargeHistMin  = 0.0;
constexpr double kChargeHistMax  = 100000.0;

Create each histogram with a unique name containing at least:

    ADC index;
    channel index;
    a Loop2() instance identifier if needed to avoid ROOT name collisions across repeated method calls.

For example:

cpp

h_hit_charge_adc%d_ch%d_loop2%zu

Requirements for each lower-row histogram:

    x-axis title:

    text

    Hit charge (ADC counts × ticks)

    y-axis title:

    text

    Hits

    title includes the ADC/channel identity;

    it is created once and persists throughout the full Loop2() invocation;

    it is not reset between events;

    it accumulates all individual hit charges seen so far for that channel;

    it should be redrawn in its lower pad for every event update;

    normal ROOT underflow and overflow behavior is acceptable for charges outside the fixed configured range;

    do not dynamically change the range or rebin the histogram after processing begins.

Use an explicit ownership strategy that avoids a new persistent charge histogram allocation for every event. Histograms must remain valid while the canvas displays them.

If appropriate for the chosen ownership approach, use:

cpp

hist->SetDirectory(nullptr);

to avoid unintended attachment to the current ROOT directory.
4. Fill individual hit charges, not total waveform charge

For each selected channel in every event:

    If the waveform is invalid:
        do not create an analysis object;
        do not fill the charge histogram;
        show an invalid/empty label in the corresponding waveform pad;
        still draw the channel’s unchanged cumulative charge histogram in the lower pad.

    If the waveform is valid:

        construct the analysis object once with the existing factory:

        cpp

        std::unique_ptr<MetaWaveformAna> ptr =
            fFactory(wf, true);

        use this same ptr for:
            waveform parameter display;
            WaveAna hit/baseline overlays;
            filling the charge histogram.

    If the result is a WaveAna:

    cpp

    WaveAna* wa = dynamic_cast<WaveAna*>(ptr.get());

    then fill the channel’s charge histogram once for every individual hit:

    cpp

    for (const auto& hit : wa->Hits()) {
        chargeHist->Fill(hit.charge);
    }

Do not fill one entry using total_charge.

Do not fill the histogram with waveform samples.

Do not fill one entry per event unless that event actually contains a hit.

The value represented by the lower plot must be the distribution of individual Hit::charge values accumulated over all events already displayed.
5. Top row must match existing Loop() behavior

For each selected channel’s top-row pad, preserve the current display behavior of Loop() as closely as possible:

    waveform as a TH1F with one bin per waveform tick;
    raw ADC samples in the waveform histogram;
    title with:
        ADC;
        channel;
        TPC;
        trap type;
    x-axis: ticks;
    y-axis: ADC counts;
    blue waveform line;
    green baseline lines for WaveAna::Baselines();
    semi-transparent red hit boxes for WaveAna::Hits();
    red peak markers;
    TPaveText with registered analysis parameters.

Retain the current event-validity behavior in the top row:

    if a selected channel is invalid in an event, show an explanatory label rather than attempting to analyze its waveform;
    the channel still keeps its fixed top-row and bottom-row positions;
    do not rearrange columns based on validity.

The top row and bottom row must remain aligned by selected-channel index.
6. Event updates and pausing

For every event read from the run:

    update/reset and redraw all top waveform pads;
    fill and redraw all bottom cumulative charge-histogram pads;
    update the canvas title with:
        current event count;
        raw event ID;
        user controls.

For example:

cpp

"Loop2 | Event %d | ID %llu | [Enter] next | [q] quit"

    call:

cpp

PauseExecution(...)

once per event.

This pause must happen even if no selected channel is valid in the event.

Loop2() must continue using the responsive PauseExecution() behavior that processes ROOT GUI events while waiting, so users can zoom, inspect, and interact with both waveform and charge-histogram pads during the pause.

Before rendering the next event, clear only the event-specific waveform drawings/pads. Do not reset, recreate, or erase the cumulative charge histograms.

Avoid unbounded memory growth from per-event waveform histograms, TLine, TBox, TMarker, and TPaveText objects.

A valid strategy is to clear and redivide the canvas between events, then redraw:

    updated top waveform pads;
    persistent bottom charge histogram pads.

Do not clear the canvas after the final event if doing so would erase the final display before the user can inspect it.
7. Scope and compatibility

    Do not change Loop().
    Do not change WaveAna::Hit, WaveAna::Hits(), hit-finding logic, or charge calculation.
    Do not require Analysis::process() before Loop2().
    Do not add charge histograms to EventAna or WaveAna; they are interactive display state local to Loop2().
    Do not alter Dump().
    The method must compile with the project’s ROOT/C++ setup.
    Add only required ROOT and standard-library headers.
    Remove the duplicate #include "Utils.hpp" in Analysis.hpp while editing that file.

Update the example macro

In macros/example_loop.C, update the interactive display call to demonstrate the new method:

cpp

analysis.Loop2();

Optionally leave the existing analysis.Loop(); as a commented alternative.

Update nearby comments to explain:

    top row: current-event waveform, baseline segments, and hits;
    bottom row: cumulative individual-hit charge distribution for each selected channel.

Acceptance criteria

    With:

    cpp

    run.ResetChannels(false);
    run.SelectChannel(2, 4, true);
    run.SelectChannel(2, 5, true);

    Loop2() creates exactly four pads arranged as:

    text

    top-left:      ADC 2 / CH 4 waveform
    top-right:     ADC 2 / CH 5 waveform
    bottom-left:   ADC 2 / CH 4 cumulative hit-charge histogram
    bottom-right:  ADC 2 / CH 5 cumulative hit-charge histogram

    After event 1, each lower histogram contains charges from all hits found in event 1 for its channel.

    After event K, each lower histogram contains charges from every individual hit observed for its channel from events 1 through K.

    The charge histogram is not reset when the user presses Enter for the next event.

    The waveform display is replaced by the next event’s waveform when the user presses Enter.

    An event in which all selected channels are invalid is still displayed and pauses for user input; the lower histograms remain visible and unchanged.

    With a factory that does not produce WaveAna, no crash occurs; the lower histograms simply remain empty.

    Pressing q then Enter exits cleanly.

    Existing Analysis::Loop() behavior remains unchanged.
