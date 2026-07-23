# Prompt: Add a print layer and a waveform/event analysis layer to `ndlar_light`

## Context

This extends an existing header-only C++/ROOT library, namespace `ndlar_light`,
that reads DUNE ND-LAr light-system HDF5 "FLOW" files event by event via
HighFive + the raw HDF5 C API. The current classes (already implemented) are:

- `lib/Waveform.hpp` — raw per-(ADC,channel) waveform: `samples[600]`,
  `clipped` flag, `adc`/`channel` indices. Pure data holder, no analysis.
- `lib/Event.hpp` — one event: metadata (`id`, `event`, `sn[8]`,
  `utime_ms[8]`, `tai_ns[8]`, `trig_type`) + an 8x64 matrix of `Waveform`.
  `Event::IsValid(adc, ch)` (from the file's `wvfm_valid`) is distinct from
  `Waveform::IsClipped()`.
- `lib/SubRunReader.hpp` — internal single-file reader, hyperslab-based,
  fills a caller-supplied, reused `Event`.
- `lib/Run.hpp` — iterates events across subrun files transparently
  (`HasNext()`/`NextEvent()` for streaming, `GetEvent(globalIndex)` for
  random access via an internal prefix-sum table).
- `lib/NDLArLight.hpp` — umbrella header.
- `macros/example_loop.C` — reference usage example.

**Attach the project's current Copilot/agent instructions file (or
equivalent architecture doc) alongside this prompt** so the code-generation
AI has the exact existing signatures, coding conventions (namespace,
header-only + ACLiC-compatible, `#pragma once`, HOFFSET-based struct
offsets, RAII/manual-close rules for raw HDF5 handles, try/catch
conventions, Doxygen style, flat `lib/` layout) and should follow them
exactly for all new code in this task.

## Goals of this iteration

1. Add printing utilities to `Waveform` and `Event`.
2. Add a `WaveformAna` class: takes a `Waveform`, analyzes it, and stores
   analysis results (starting with just the mean).
3. Refactor `Event`'s metadata into a shared `EventMetadata` class, and add
   an `EventAna` class that reuses that metadata but holds analyzed
   waveforms (`WaveformAna`) instead of raw ones.
4. Add an `Analysis` class that processes a full `Run`, event by event and
   waveform by waveform, accumulating the results as a vector of `EventAna`.

## 1. Printing

- Add `Waveform::Print(int maxSamples = 10) const` — prints ADC/channel
  index, `clipped` flag, and the first `maxSamples` samples (not all 600 by
  default, to keep output readable). Also add
  `friend std::ostream& operator<<(std::ostream&, const Waveform&)`
  delegating to `Print` (or an equivalent stream-building helper).
- Add `Event::Print(bool printWaveforms = false, int maxSamplesPerWaveform = 5) const`
  — always prints the metadata fields (`id`, `event`, `sn`, `utime_ms`,
  `tai_ns`, `trig_type`); if `printWaveforms` is true, also prints each
  *valid* (per `IsValid(adc, ch)`) waveform via `Waveform::Print`, skipping
  invalid channels rather than printing 512 mostly-empty entries. Add a
  matching `operator<<` as well.
- Keep these purely for human-readable console output (no formatting
  framework dependency beyond `<iostream>`/`<iomanip>`).

## 2. `WaveformAna`

- New file: `lib/WaveformAna.hpp`, class `ndlar_light::WaveformAna`.
- Constructed from a `const Waveform&`; computes and stores its analysis
  results immediately in the constructor (no separate `Analyze()` call
  needed for this first version).
- Stores:
  - `adc`, `channel` indices (copied from the source `Waveform`, so a
    `WaveformAna` is self-describing).
  - `clipped` — copied from `Waveform::IsClipped()`.
  - `isValid` — copied from the owning `Event::IsValid(adc, channel)` at
    construction time (passed in explicitly, since `Waveform` itself
    doesn't know about validity — that's an `Event`-level concept). Make
    the constructor signature `WaveformAna(const Waveform&, bool isValid)`
    to make this dependency explicit.
  - `std::map<std::string, double> results` — generic parameter storage,
    chosen deliberately over fixed named fields so future analysis
    quantities (RMS, integral, peak amplitude, peak time, baseline, ...)
    can be added without changing the class's public interface.
  - For the first version, only compute and store `results["mean"]` (the
    arithmetic mean of the 600 samples).
- To avoid magic strings scattered across the codebase, define the known
  parameter keys as `static constexpr const char*` members (e.g.
  `static constexpr const char* kMean = "mean";`) and provide typed
  convenience getters on top of the map, e.g. `double GetMean() const`
  that looks up `kMean` (throwing or returning `NaN` with a clear
  convention if missing — pick one and be consistent). New analysis
  quantities added later should follow the same pattern: a `k<Name>`
  key constant + a `Get<Name>()` accessor.
- Add a `Print()` (and `operator<<`) similar to `Waveform::Print`, showing
  adc/channel, `clipped`, `isValid`, and all entries currently in
  `results`.

## 3. `EventMetadata` + `EventAna`

- Extract the metadata currently embedded in `Event` into a new,
  standalone class: `lib/EventMetadata.hpp`, `ndlar_light::EventMetadata`.
  It should own: `id`, `event`, `sn[8]`, `utime_ms[8]`, `tai_ns[8]`,
  `trig_type`, the `wvfm_valid[8][64]` data, the `IsValid(adc, channel)`
  accessor, and a `Print()`/`operator<<`.
- Refactor `Event` to hold an `EventMetadata meta` member (composition —
  do **not** use inheritance here) instead of duplicating those fields
  directly. `Event`'s existing public API (`IsValid`, getters for `id`,
  `event`, etc.) should keep working, delegating to `meta` internally, so
  this refactor does not break `SubRunReader`, `Run`, or existing macros
  that use `Event`.
- New file: `lib/EventAna.hpp`, class `ndlar_light::EventAna`. Holds:
  - `EventMetadata meta` (same composition pattern as `Event`).
  - An 8x64 matrix of `WaveformAna` (mirroring `Event`'s waveform matrix
    shape, but analyzed instead of raw).
  - A `Print(bool printWaveforms = false) const` analogous to `Event`'s,
    delegating to `meta.Print()` and, optionally, to each valid
    `WaveformAna::Print()`.
- Note for the implementer: since `EventMetadata` is shared, `Event` and
  `EventAna` end up structurally parallel (`Event` = metadata + raw
  waveforms, `EventAna` = metadata + analyzed waveforms) without
  duplicating the metadata fields or their accessors.

## 4. `Analysis`

- New file: `lib/Analysis.hpp`, class `ndlar_light::Analysis`.
- Constructed from a `Run&` (stored as a non-owning reference/pointer —
  `Analysis` does not own or manage the `Run`'s lifetime; the caller keeps
  the `Run` alive for as long as `Analysis` is used).
- Holds `std::vector<EventAna> events` internally (this is fine memory-wise
  even for a full run: each `EventAna` is far lighter than an `Event`,
  since it stores a handful of doubles per channel instead of 600 raw
  samples).
- `void process()`:
  - Iterates the referenced `Run` via its existing streaming API
    (`HasNext()`/`NextEvent()`), reading one raw `Event` at a time (no
    change to `Run`/`SubRunReader`'s one-event-at-a-time behavior — this
    must **not** load the whole run into memory as raw `Event`s at any
    point).
  - For each raw `Event`, builds one `EventAna`: copies/constructs its
    `EventMetadata` from the raw event's metadata, then for every
    (adc, channel), constructs a `WaveformAna` from the raw `Waveform`
    plus `Event::IsValid(adc, channel)`, and stores it in the `EventAna`'s
    matrix.
  - Appends the resulting `EventAna` to `events` and discards the raw
    `Event`'s waveform data (it goes out of scope naturally once the next
    `NextEvent()` overwrites the reused buffer inside `Run`/`SubRunReader`).
  - **Add a `Reset()`/`Rewind()` method to `Run`** (not currently present)
    so `Analysis::process()` can be called on a `Run` regardless of
    whether it's already been partially iterated elsewhere — `process()`
    should call this at its start to guarantee it always processes the
    full run from the beginning.
- Provide read access to the results after processing, e.g.
  `const std::vector<EventAna>& GetEvents() const`, plus simple helpers if
  convenient (e.g. `size_t GetNEvents() const`).

## File layout (new/modified)

```
lib/EventMetadata.hpp   (new — extracted from Event.hpp)
lib/Event.hpp           (modified — composition with EventMetadata, + Print)
lib/Waveform.hpp        (modified — + Print)
lib/WaveformAna.hpp     (new)
lib/EventAna.hpp        (new)
lib/Analysis.hpp        (new)
lib/Run.hpp             (modified — + Reset()/Rewind())
lib/NDLArLight.hpp       (modified — include the new headers)
macros/example_analysis.C (new — see below)
```

## Example macro

Add `macros/example_analysis.C` demonstrating: build a `Run` (from an
explicit file list, matching `example_loop.C`'s style), construct
`Analysis analysis(run)`, call `analysis.process()`, then loop over
`analysis.GetEvents()` printing each `EventAna` (metadata + mean of a
couple of valid channels) to prove the whole chain runs as an uncompiled
ROOT macro (`root -l example_analysis.C`).

## Non-goals (for this iteration)

- No other analysis quantities besides the mean yet (RMS, integral, peak,
  baseline, etc. are future work — the `std::map`-based `results` in
  `WaveformAna` is specifically designed so those can be added later
  without an API change).
- No pluggable/strategy-pattern analysis algorithms yet — the mean
  computation can be a straightforward loop inside `WaveformAna`'s
  constructor for now.
- No plotting/histogramming in `Analysis` itself — that can consume
  `Analysis::GetEvents()` from separate code later.
- No `TTree`/ROOT I/O persistence of `EventAna` results yet.
- No multithreaded processing in `Analysis::process()`.

## Open assumptions to double-check in the generated code

- `EventMetadata` is shared via composition (a `meta` member), not
  inheritance, in both `Event` and `EventAna`.
- `WaveformAna` copies both `clipped` (from `Waveform`) and `isValid`
  (from `Event`, passed explicitly at construction) so an `EventAna` is
  fully self-contained and never needs to reference the original `Event`.
- Analysis parameters are stored in a generic `std::map<std::string, double>`
  plus named key constants and typed getters, not fixed struct fields.
- `Run` gains a `Reset()`/`Rewind()` method as part of this change so that
  `Analysis::process()` can guarantee it processes the run from the start.