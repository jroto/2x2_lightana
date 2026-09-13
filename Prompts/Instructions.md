# Prompt: C++/ROOT header-only library to read NDLAr light-system HDF5 files

## Context

I work with HDF5 files produced by the `h5flow` framework for the DUNE
Near Detector Liquid Argon (ND-LAr) light readout system. Each file is a
**subrun**; several subruns (files) together make up a **run**. I need a
C++ library, meant to be used inside ROOT (as an interpreted macro via
`#include`, and optionally compiled with ACLiC), that reads these files
**event by event** (never loading a whole file into memory) and exposes
the data through a small object model: `Run` -> `Event` -> `Waveform`.

## Input file structure

Each subrun file has this relevant HDF5 layout (from `h5dump -H`):

```
/light/events/data                      (N events, compound, extendible)
    id            : uint64
    event         : int32
    sn            : int32[8]
    utime_ms      : uint64[8]
    tai_ns        : uint64[8]
    wvfm_valid    : uint8[8][64]         (per ADC, per channel)
    trig_type     : uint8

/light/events attributes include: n_adcs=8, n_channels=64, n_samples=600

/light/wvfm/data                        (N events, compound, extendible)
    samples       : int16[8][64][600]   (per ADC, per channel, per sample)
    clipped       : enum(int8) FALSE=0/TRUE=1 [8][64]

/light/wvfm/ref/light/events/ref_region  -- region reference dataset linking
/light/events/ref/light/wvfm/ref         -- events <-> wvfm rows
```

- `/light/events/data` and `/light/wvfm/data` have the **same length** `N`
  (in the sample file, `N = 905`) and are, in practice, aligned 1:1 by row
  index within a given subrun file (row `i` in one dataset corresponds to
  row `i` in the other). There is also an explicit HDF5 reference
  (`ref`/`ref_region`) linking them, intended for the general/robust case.
- Terminology to use in the code: **ADC** (8 of them), **channel** (64 per
  ADC), **sample** (600 per waveform). Do not use "TPC" for this
  dimension — reserve that name for other contexts.
- Subrun filenames look like:
  `mpd_run_data_2026_07_16_14_05_34_CST_001130_p00003.FLOW.hdf5`
  where `001130` is the run number and `p00003` is the subrun index. The
  exact date/time part of the name should be treated as irrelevant/variable.

## Object model

### `Waveform`
- Raw storage only, no analysis/processing methods (no pedestal, no
  integral, no peak finding — that will be added later in a separate
  layer).
- Holds exactly what is physically read for one (ADC, channel) pair from
  `/light/wvfm/data`:
  - `int16_t samples[600]`
  - a `clipped` flag (bool or the original enum meaning)
  - the ADC index and channel index it belongs to (so a `Waveform` is
    self-describing when passed around on its own)
- No file-format knowledge should leak into this class; it's a plain data
  holder.

### `Event`
- Represents one row (one entry) merged from `/light/events/data` and
  `/light/wvfm/data`.
- Contains an 8x64 matrix/array of `Waveform` (one per ADC/channel).
- Contains the full metadata from `/light/events/data`:
  `id`, `event`, `sn[8]`, `utime_ms[8]`, `tai_ns[8]`, `wvfm_valid[8][64]`,
  `trig_type`.
- Provides convenient accessors, e.g. `GetWaveform(adc, channel)`,
  `IsValid(adc, channel)` (from `wvfm_valid`), `GetTriggerType()`, etc.
- Should be reusable: the `Run`/reader should fill the *same* `Event`
  instance on each read rather than allocating a new one per event (each
  event is ~600 KB, so avoid per-event heap churn in tight loops).

### `Run`
- Represents a full run: an ordered collection of subrun files.
- Two ways to build it:
  1. From an explicit, ordered list of subrun file paths.
  2. From a directory + run number: scan the directory, find all files
     matching the subrun naming convention for that run number, and sort
     them by subrun index automatically. Make the filename pattern
     configurable (e.g. a regex or a small pluggable "convention" object)
     rather than hardcoded, since the naming scheme may change.
- Iterates **event by event across all subruns transparently**, i.e. the
  caller doesn't need to know where one subrun ends and the next begins.
  Support both:
  - Sequential/streaming access (an iterator or `NextEvent()`/`HasNext()`
    style API), which is the primary intended use.
  - Random access by a *global* event index, internally translated to
    (subrun index, local row index) — nice to have, not the main use
    case.
- Internally, for each subrun file, read `/light/events/data` and
  `/light/wvfm/data` **one row at a time** using HDF5 hyperslab selection
  (never `H5S_ALL` for the whole dataset), matching rows 1:1 by index.
  At file-open time, validate that both datasets have the same length;
  if you want extra robustness, spot-check a few entries against the
  `ref`/`ref_region` datasets and warn (don't just silently trust) if
  they don't correspond to a simple identity mapping.
- Should expose basic info: total number of events in the run, number of
  subruns, current subrun/file name, etc.

## Implementation requirements

- **Language**: C++17, usable from ROOT 6 (cling interpreter) as well as
  compiled code.
- **Dependencies**: ROOT (for macro usage / eventual histogram output),
  the HDF5 C library, and HighFive (header-only C++ wrapper over HDF5).
- **Distribution**: header-only (`#pragma once`), so a user can just
  `#include "NDLArLight/Run.hpp"` and run it as an uncompiled ROOT macro.
  At the same time, keep the code ACLiC-friendly (no ODR violations, no
  reliance on features that break under `.L file.C+`) in case someone
  wants to compile it for speed.
- **Reading nested-array compound types**: `/light/wvfm/data` has arrays
  nested inside a compound type (`samples[8][64][600]`,
  `clipped[8][64]`), which HighFive's automatic type mapping does not
  handle well. Use the hybrid approach already validated for this file
  format: open the file/dataset via HighFive, but build the compound
  `hid_t` by hand with the raw HDF5 C API (`H5Tarray_create2`,
  `H5Tcreate(H5T_COMPOUND, ...)`, `H5Tinsert`) and read via `H5Dread`
  using `HighFive::DataSet::getId()` to get the raw dataset handle. Use
  hyperslab selection (`H5Sselect_hyperslab`) to read one row at a time.
- **Memory**: no full-file reads; no growing containers of full events;
  reuse a single `Event` buffer during iteration whenever possible.
- **Namespace**: put everything under a single namespace, e.g.
  `ndlar_light`, with English class names (`Waveform`, `Event`, `Run`,
  and an internal helper for per-subrun reading if useful, e.g.
  `SubRunReader`).
- **Error handling**: throw exceptions (`std::runtime_error` or a small
  custom exception type) with messages that include the file path and
  dataset path on failure. Don't let raw HDF5 error codes leak
  unexplained.
- **Documentation**: Doxygen-style comments on public classes/methods.
- **File layout**:
  ```
  include/NDLArLight/Waveform.hpp
  include/NDLArLight/Event.hpp
  include/NDLArLight/SubRunReader.hpp   (internal, low-level per-file reader)
  include/NDLArLight/Run.hpp
  include/NDLArLight/NDLArLight.hpp     (single umbrella header)
  macros/example_loop.C                  (example ROOT macro using the library)
  ```
- **Example macro**: include a small `example_loop.C` showing: building a
  `Run` from a directory + run number, iterating events, accessing
  `event.GetWaveform(adc, channel).samples`, and printing/filling a
  simple `TH1D` with e.g. the first sample of a given channel across
  events, to prove it runs as an uncompiled ROOT macro
  (`root -l example_loop.C`).

## Explicit non-goals (for this first version)

- No waveform analysis (pedestal subtraction, peak/charge extraction,
  filtering, etc.) — `Waveform` is intentionally a raw data holder.
- No writing/output of processed data (e.g. no TTree output) — this
  library is a *reader* only.
- No multithreading/parallel file reading.
- No use of ROOT's I/O system (no `ClassDef`/dictionaries) — ROOT is used
  only as the interactive/macro environment and for optional histogram
  utilities in the example macro, not for serializing these objects.

## Open assumptions to double-check in the generated code

- 1:1 row-index correspondence between `/light/events/data` and
  `/light/wvfm/data` per subrun is used as the primary (fast) path, with
  a length check (and optional light validation against `ref_region`) at
  open time rather than resolving every event through HDF5 references.
- Subrun filename convention is `..._<runNumber>_p<subrunIndex>.FLOW.hdf5`
  with configurable pattern — confirm/adjust the regex to your actual
  naming scheme if it varies.
- `Waveform` carries its own ADC/channel index as plain metadata (not
  considered "analysis").