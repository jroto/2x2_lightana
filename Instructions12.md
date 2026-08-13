Modify the current `main` branch of https://github.com/jroto/2x2_lightana.

## Goal

Make `BaselineCalibrator` process only the channels selected in the `Run` channel map, rather than repeatedly traversing every `(adc, channel)` slot in the full `kNumADCs × kNumChannels` grid.

The design must preserve `Run`’s encapsulation: callers and analysis components should not receive mutable access to `ChannelMap`. [K2] [K5]

A channel is “selected” when its `ChannelMap` entry is active.

## Current relevant implementation

- `Run` owns `ChannelMap fChannelMap` and already offers:
  - `ResetChannels(bool active = false)`
  - `SelectChannel(int adc, int ch, bool active)`
  - `GetChannelMap() const`
- `BaselineCalibrator::Calibrate(Run&)` currently:
  1. calls `run.Reset()`;
  2. loops over every event;
  3. loops over all ADCs and all channels;
  4. checks `event.IsValid(adc, ch)`;
  5. accumulates baseline window means;
  6. again loops over the full grid to call `FitChannel(adc, ch)`.
- `BaselineCalibrator::Print()` currently prints all channels.
- `BaselineCalibrator::Draw()` currently draws only channels that have histograms.
- `Run` channel-map changes apply to newly created `SubRunReader` objects, i.e. after `Reset()`. `Calibrate()` already resets the run, so the intended workflow is compatible with this behavior. [K6]

## Required changes

### 1. Add a read-only selected-channel iteration API to `Run`

In `lib/Run.hpp`, add a public, read-only method:

```cpp
std::vector<std::pair<int, int>> GetSelectedChannels() const;

Requirements:

    Return a value/snapshot containing exactly the active channels from fChannelMap.
    Each pair is (adc, channel).
    Traverse the fixed channel map once inside this method:
        ADC-major order;
        then channel-major order within each ADC.
    Do not expose mutable access to ChannelMap.
    Do not change or remove GetChannelMap() const, ResetChannels(), or SelectChannel().
    Add any necessary standard includes, notably <utility> if needed.
    Document that “selected” means ChannelMap::IsActive(adc, ch) == true.
    Document that the returned vector is a snapshot: later calls to SelectChannel, ResetChannels, or SetChannelMap do not mutate a vector previously returned by this method.

The API should support the project’s intended workflow in which channel selection is controlled through Run::SelectChannel().
2. Make BaselineCalibrator::Calibrate() iterate only selected channels

In lib/BaselineCalibrator.hpp, update BaselineCalibrator::Calibrate(Run& run).

Required sequence:

    Clear all calibration state from a possible previous call, as specified in section 3 below.
    Call run.Reset().
    Obtain one local snapshot:

    cpp

    const auto selectedChannels = run.GetSelectedChannels();

    Store this snapshot in the calibrator for later reporting.
    For every processed event, iterate directly over selectedChannels, not over the complete ADC/channel grid.
    For each selected (adc, ch):
        retain the existing event.IsValid(adc, ch) check;
        retain the existing waveform extraction, baseline finding, and sample accumulation behavior.
    After event accumulation, call FitChannel(adc, ch) only for channels in selectedChannels.

Do not replace the existing full-grid loops merely with a full-grid loop plus run.GetChannelMap().IsActive(...): that still performs the full scan for every event and does not meet the performance objective.

Behavior for no selected channels:

    Calibrate() must complete safely.
    It must consume no waveform channels.
    It must produce no fitted histograms.
    Print() and Draw() must behave sensibly without throwing or dereferencing null pointers.
    A concise informational message is acceptable but not required.

3. Make each calibration invocation independent

A call to:

cpp

calibrator.Calibrate(run);

must discard all state from any preceding calibration before processing the new run/channel selection.

Add a private reset/clear helper, e.g.:

cpp

void ClearCalibration();

Its responsibilities:

    For every possible (adc, ch):
        delete any existing TH1F* in fHist[adc][ch];
        delete any existing TF1* in fFit[adc][ch];
        set both pointers to nullptr;
        clear fSamples[adc][ch];
        reset fResult[adc][ch] to a default ChannelBaseline{}.
    Clear the stored selected-channel snapshot.
    Be safe when called before any calibration and when pointers are null.
    Call it at the beginning of Calibrate().
    Reuse it from the destructor if appropriate, avoiding duplicate deletion logic.

Do not leave stale fitted values, histograms, or samples from channels selected in a prior calibration.
4. Make Print() report only the calibrated selection

Update BaselineCalibrator::Print() so that it prints only channels from the selected-channel snapshot used by the most recent Calibrate() call.

Requirements:

    Keep the existing columns:
        ADC
        CH
        mean
        sigma
        n_wins
        calibrated
    Iterate over the stored selected-channel list, in its existing deterministic order.
    Do not print the remaining unselected channels.
    If no calibration has been run, or the last calibration had no selected channels, print the header and either:
        no data rows, or
        one concise explanatory line.
    Do not query current Run state from Print(): the output must correspond to the exact channel-selection snapshot that was used for the calibration.

5. Keep Draw() consistent with the selection

Draw() already collects channels with non-null histograms. Preserve that behavior, since a selected channel without usable baseline samples should not produce a histogram.

Make these small consistency updates:

    Update comments/title text that say “all channels” to say “selected channels” or equivalent.
    Keep dynamic canvas sizing.
    Do not draw stale histograms after repeated calls to Calibrate(); the clear/reset behavior must ensure this.
    Preserve existing ROOT ownership/deletion conventions unless a change is necessary for correctness.

6. Preserve API compatibility and existing analysis workflow

    Keep the public signature:

    cpp

    void BaselineCalibrator::Calibrate(Run& run);

    Keep:

    cpp

    const ChannelBaseline& GetBaseline(int adc, int ch) const;

    unchanged. For channels absent from the most recent calibration selection, it should return the reset/default, uncalibrated result.
    Do not require callers to pass a ChannelMap directly to BaselineCalibrator.
    Do not add mutable ChannelMap access through Run.
    Maintain the existing example workflow:

    cpp

    run.ResetChannels(false);
    run.SelectChannel(2, 4, true);
    run.SelectChannel(2, 5, true);

    calibrator.Calibrate(run);
    calibrator.Print();
    calibrator.Draw();

    The calibration, printed rows, and drawn channels should now be limited to (2,4) and (2,5), subject to event validity and whether baseline windows are found.

Documentation updates

Update Doxygen/comments in affected headers so they accurately state:

    Run::GetSelectedChannels() returns the active-channel snapshot.
    Calibrate() processes selected channels only.
    Every call to Calibrate() starts a fresh calibration.
    Print() reports the selection used by the most recent calibration.
    Draw() displays available histograms for the selected channels.

Validation / acceptance criteria

    The project still compiles in its intended ROOT/C++ environment.
    With:

    cpp

    run.ResetChannels(false);
    run.SelectChannel(2, 4, true);
    run.SelectChannel(2, 5, true);

    GetSelectedChannels() contains exactly (2,4) and (2,5), in that order.
    During calibration, only those two channels are considered per event; no full ADC/channel traversal remains in the event-processing loop.
    Only selected channels are passed to FitChannel().
    Print() reports only the selected channels from the latest call to Calibrate().
    Calling Calibrate() twice with different selections does not retain:
        old samples,
        old fit results,
        old ROOT histogram/function objects,
        old printed/drawn channels.
    Empty selection is safe and produces no fits or drawings.
    Existing event-validity behavior remains intact: a selected but invalid waveform in a particular event is skipped.

Scope constraints

Keep this change focused on selected-channel iteration and calibrator state reset. Do not redesign the channel-map CSV format, waveform processing, baseline-fit algorithm, Analysis, or SubRunReader beyond any strictly necessary compilation/documentation adjustments.



The key design point is that the channel-map scan happens once per calibration invocation, while the costly per-event loop iterates only the selected-channel vector.
