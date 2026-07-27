# Copilot Instructions for NDLAr_code

## What this is
Ad-hoc ROOT macros (`.C` files) for exploring DUNE Near Detector Liquid Argon (NDLAr)
HDF5 "FLOW" data files using the [HighFive](https://github.com/BlueBrain/HighFive) C++ wrapper
around HDF5, run interactively via ROOT's Cling interpreter. There is no build system
(no CMakeLists, no Makefile) — every `.C` file is a standalone, self-contained macro.

## Environment (must source before running anything)
Dependencies (root, hdf5, highfive) come from a Spack environment, not system packages.
Always run:
```bash
source Setup.sh
```
before invoking ROOT. `Setup.sh` activates the `dune-prototype` Spack environment from
CVMFS (`/cvmfs/dune.opensciencegrid.org/spack/setup-env.sh`) and loads a pinned GCC
(`gcc@12.5.0 arch=linux-almalinux9-x86_64_v2`) so the system compiler/libstdc++ isn't used.
`spack.yaml` declares the specs (`hdf5`, `root`, `highfive`); `spack.lock` pins exact
versions/hashes — check it if you hit HighFive API mismatches (see below).

## Running a macro
```bash
source Setup.sh
root -l -q test2.C        # -q exits after running; drop -q to stay interactive
```
Each file's entry point is a `void <filename>()` function matching the file's base name
(e.g. `test2.C` → `void test2()`), which is how ROOT/Cling knows what to execute when the
file is processed. Compile errors surface as Cling diagnostics referencing `input_line_N`.

## Key conventions in this codebase
- **HighFive version is old/limited** — do not assume the full modern HighFive API exists.
  `DataType::isInteger()`, `isFloat()`, `isFixedLenString()` are NOT available here; use
  `dtype.getClass()` switched over `HighFive::DataTypeClass` instead (see
  `print_datatype_class` in `test.C`).
- **Compound HDF5 types need the raw C API.** HighFive can't easily describe nested
  fixed-size array fields inside a compound type, so `test2.C` drops down to `<hdf5.h>`
  (`H5Tarray_create2`, `H5Tcreate(H5T_COMPOUND, ...)`, `H5Tinsert`) to build a struct-matching
  `hid_t`, then calls `H5Dread` directly. Get the raw dataset id via `HighFive::Object::getId()`
  (e.g. `ds.getId()`) to interop between HighFive objects and raw C API calls.
- **Struct layout must mirror the HDF5 schema exactly**, including field order and array
  dims, e.g. `WvfmRecord` mirrors `H5T_ARRAY {[8][64][600]} H5T_STD_I16LE "samples"` and
  `H5T_ARRAY {[8][64]} H5T_ENUM(int8) "clipped"`. If the dataset schema changes, update the
  struct and `make_wvfm_compound_type()` together, and use `HOFFSET(WvfmRecord, field)` for
  offsets — never hardcode byte offsets.
- **Always wrap file/dataset access in try/catch** for `HighFive::Exception` (and generic
  `std::exception`) — reading from `/pnfs/...` scratch paths can fail for permissions,
  missing files, or schema mismatches, and ROOT macros should print a clear message rather
  than crash the interpreter.
- **Manually close raw HDF5 handles** (`H5Tclose`, `H5Sclose`) opened via the C API — HighFive
  RAII wrappers don't manage handles you create yourself with `H5T*`/`H5S*` calls.
- Input files live under `/pnfs/dune/scratch/users/<user>/...FLOW.hdf5` (dCache/scratch, not
  local) — paths are currently hardcoded per-macro; when adding new macros keep the
  `filename`/`dataset_path` constants near the top of the `void <macroname>()` function for
  easy editing.

## Adding a new exploration macro
Follow the `test2.C` pattern: one `void <name>()` entry point, helper functions above it,
constants for file/dataset paths declared locally, try/catch around all HighFive calls, and
prefer capping event loops (`max_events` parameter) since datasets can have thousands of
records. More macros like `test.C`/`test2.C` are expected over time — this pattern is the
project's standard, not a one-off.

## Reusable library code
Shared/reusable code (not one-off exploration) lives flat under `lib/` as header-only C++
(`.hpp`, `#pragma once`), consumed by macros via `#include "../lib/NDLArLight.hpp"`
(the umbrella header) or individual headers like `#include "../lib/Run.hpp"` — no separate
build/link step, consistent with running everything through Cling.

The first (and so far only) such library is `ndlar_light` (namespace), implemented per the
design specs in `Instructions.md` (reader layer) and `Instructions2.md` (print + analysis
layer), reading `/light/events/data` and `/light/wvfm/data`:
- `lib/Waveform.hpp` — raw per-(ADC,channel) waveform: `samples[600]`, `clipped` flag, adc/
  channel indices. Pure data holder, no analysis methods (no pedestal/peak-finding). Has a
  tabular `Print(os, maxSamples=10)` + `operator<<` for console debugging.
- `lib/EventMetadata.hpp` — the metadata fields extracted from `/light/events/data` (`id`,
  `event`, `sn[8]`, `utime_ms[8]`, `tai_ns[8]`, `trig_type`, `wvfm_valid[8][64]`), plus
  `IsValid(adc, ch)` and a tabular `Print()`. Shared **by composition** (never inheritance)
  by both `Event` and `EventAna` via a `Meta()` accessor — this is the single source of truth
  for those fields; don't duplicate them elsewhere.
- `lib/Event.hpp` — one event: an `EventMetadata fMeta` member + an 8x64 `Waveform` matrix.
  Keeps its old flat accessors (`GetId()`, `IsValid()`, etc.) as thin delegates to `fMeta` so
  `SubRunReader`/`Run`/existing macros don't need to change. `Event::IsValid(adc, ch)` is a
  **distinct concept** from `Waveform::IsClipped()` — a channel can be
  valid-but-clipped or simply invalid; don't conflate the two accessors.
- `lib/SubRunReader.hpp` — internal, low-level single-file reader. Builds raw HDF5 compound
  types by hand for both datasets (mirroring the hybrid HighFive/raw-C-API pattern below),
  validates the two datasets have equal length at open time (throws otherwise — this project
  does NOT cross-check the `ref`/`ref_region` HDF5 references, by design, for simplicity),
  and reads one row at a time via hyperslab selection into a caller-supplied, reused `Event`.
  Holds `HighFive::DataSet` via `std::unique_ptr` (not by value) — this HighFive version's
  `DataSet` isn't usable as a plain default-constructed member.
- `lib/Run.hpp` — iterates events across multiple subrun files transparently. Two
  constructors: an explicit `std::vector<std::string>` of file paths, or
  `Run(directory, runNumber, std::regex pattern)` where **the regex is always supplied by the
  caller** (never inferred/hardcoded) since naming conventions vary across productions.
  Supports both sequential (`HasNext()`/`NextEvent()`, primary use case) and random access
  (`GetEvent(globalIndex)`, via an internal prefix-sum table) iteration, fully implemented
  (not stubbed). `Reset()` rewinds sequential iteration to the first event of the first
  subrun (used internally by `Analysis::process()` — call it if you reuse a `Run` after
  external iteration).
- `lib/MetaWaveformAna.hpp` — pure abstract interface (`GetADC/GetChannel/IsClipped/IsValid/
  Print`, plus `HasParamIndex(index)`/`GetParamByIndex(index)`, no data members) implemented
  by `WaveformAna`. Exists so `EventAna` and `Analysis`/`Dump()` depend only on this
  interface, never on the concrete `WaveformAna` type — this is what makes the analysis step
  pluggable (see `Analysis.hpp` below). Analysis parameter *names* (e.g. `"mean"`) are not
  per-instance data — they live in a **process-wide shared registry** on `MetaWaveformAna`
  itself: a private nested `Registry{names, indexByName}` accessed only via a function-local
  static `static Registry& MutableRegistry()` (this avoids static-initialization-order issues
  across headers, since concrete classes register their parameter names via out-of-line
  `static const std::size_t` member initializers evaluated at first use — see `WaveformAna`
  below). Public static API: `RegisterParam(name)` (registers if new, returns existing index
  otherwise — idempotent), `ParamIndex(name)` (**throws `std::out_of_range`** if the name was
  never registered), `ParamNames()` (returns the full, insertion-ordered list of registered
  names — this is what `Analysis::Dump()` iterates to build branches). Non-virtual
  convenience wrappers `HasParam(name)`/`GetParam(name)` are built on top of the virtual
  index-based methods + `ParamIndex(name)`, so callers can use whichever is more convenient.
- `lib/WaveformAna.hpp` — the default/standard analysis implementation, `class WaveformAna :
  public MetaWaveformAna`. Constructed as
  `WaveformAna(const Waveform&, bool isValid)` — `isValid` must be passed explicitly
  (`Event::IsValid(adc, ch)`) since `Waveform` itself has no notion of validity. Computed
  quantities are stored in a dense per-instance `std::vector<double> fParams`, indexed by the
  shared registry index (NOT a per-instance map) — `fParams` is sized to
  `MetaWaveformAna::ParamNames().size()` and filled with `NaN` as the "not present" sentinel,
  so `HasParamIndex(i)` is simply `i < fParams.size() && !std::isnan(fParams[i])`. This dense
  layout is why the registry exists: it avoids per-instance map overhead across the 8x64
  waveforms held per event. Follow the existing pattern when adding a new quantity (e.g.
  RMS): a `static constexpr const char* k<Name>Name = "<name>";` string constant, an
  out-of-line `static const std::size_t k<Name>Index` defined at file scope as
  `inline const std::size_t WaveformAna::k<Name>Index = MetaWaveformAna::RegisterParam(WaveformAna::k<Name>Name);`
  (this is what actually registers the name in the shared registry, the first time the header
  is loaded), and a typed `Get<Name>()` accessor built on `GetParamByIndex(k<Name>Index)` (see
  `kMeanName`/`kMeanIndex`/`GetMean()`). `GetParamByIndex(index)` **throws
  `std::out_of_range`** if `!HasParamIndex(index)` — this project treats a missing/NaN value
  as a bug (the class always computes what it advertises), not a soft case. Has a default
  constructor (all fields default/-1) so it can live in a fixed-size 8x64 array before being
  analyzed.
- `lib/EventAna.hpp` — analyzed counterpart to `Event`: same `EventMetadata fMeta` (via
  `Meta()`, composition) but an 8x64 matrix of `std::unique_ptr<MetaWaveformAna>` (polymorphic,
  not a concrete `WaveformAna`) instead of raw `Waveform`. `GetWaveformAna(adc, ch)` returns
  `const MetaWaveformAna&` and **throws `std::runtime_error`** if that slot is still `nullptr`
  (i.e. `Analysis::process()` hasn't populated it yet) — never dereference without going
  through this accessor. Structurally parallel to `Event` by design — don't duplicate
  metadata handling here.
- `lib/Analysis.hpp` — processes a full `Run` into `std::vector<EventAna>`, **pluggable** via
  a `WaveformAnaFactory` (`std::function<std::unique_ptr<MetaWaveformAna>(const Waveform&,
  bool isValid)>`) passed to the constructor (defaults to `DefaultWaveformAnaFactory()`,
  which builds the standard `WaveformAna`). To run a custom analysis, derive a class from
  `MetaWaveformAna` and pass a factory lambda constructing it —
  `Analysis`/`EventAna`/`Dump()` only ever touch the `MetaWaveformAna` base interface, so no
  other code needs to change. Holds a **non-owning `Run&`** (caller must keep the `Run`
  alive). `process()` always calls `fRun.Reset()` first (so it covers the whole run
  regardless of prior iteration state), then streams one raw `Event` at a time via
  `HasNext()`/`NextEvent()`, calling `fFactory(waveform, isValid)` per (adc, channel) — it
  must never hold more than one raw `Event` in memory at once, even though the resulting
  `EventAna` vector for a full run is fine to keep (each `EventAna` is far lighter than a raw
  `Event`: doubles instead of 600 raw samples per channel). **This is the only header that
  depends on ROOT I/O** (`TFile.h`/`TTree.h`) — the reader/data-model layers stay
  ROOT-I/O-free by design; keep new ROOT persistence code here, not in
  `Event`/`EventAna`/`WaveformAna`.
  - `Dump(filename, treename="waveforms")` writes one TTree entry per (event, adc, channel)
    waveform (so `NumEvents * 8 * 64` entries — e.g. 905 events → 463,360 entries) from the
    already-computed `fEvents` (does **not** re-read the `Run` or call `process()` itself —
    call `process()` first). Static branches: `event_id/l`, `event_number/I`, `trig_type/b`,
    `sn/I`, `utime_ms/l`, `tai_ns/l`, `adc/I`, `channel/I`, `valid/O`, `clipped/O`. Analysis
    variable branches are **discovered via the shared registry**: it reads
    `MetaWaveformAna::ParamNames()` directly (the full set of names ever registered by any
    loaded `MetaWaveformAna` implementation — no per-event union pass needed), resolves each
    name's registry index once via `MetaWaveformAna::ParamIndex(name)`, creates one
    `Double_t` branch per name (e.g. `mean/D`), then fills each waveform's branch value via
    `wa.HasParamIndex(index) ? wa.GetParamByIndex(index) : NaN`. This means adding a new
    quantity to any `MetaWaveformAna` implementation (e.g. registering `"rms"`) automatically
    produces a new TTree column with zero changes to `Dump()` — don't hardcode parameter
    names here. Empty `fEvents` still produces a valid (zero-entry) TTree rather than
    erroring.
- `lib/NDLArLight.hpp` — umbrella header aggregating the above.
- `macros/example_loop.C` — reference example for the raw reader: build a `Run` from
  an explicit file list, loop with `HasNext()`/`NextEvent()`, print metadata + first 5 samples
  per valid (adc, channel). Verified against a real file (905 events).
- `macros/example_analysis.C` — reference example for the analysis layer: build a `Run`,
  construct `Analysis analysis(run)`, call `process()`, then iterate
  `analysis.GetEvents()` printing metadata + `WaveformAna::GetMean()` for a couple of valid
  channels. Verified against a real file (905 events processed).
- `macros/example_dump.C` — reference example for `Analysis::Dump`: build+process a `Run`,
  call `analysis.Dump("analysis.root")`, then reopen the file and `tree->Print()` to show the
  branch list, including the dynamically-created `mean` branch. Verified against a real file
  (905 events → 463,360 TTree entries, 11 branches).

Printing conventions: `Waveform::Print`, `EventMetadata::Print`, and `WaveformAna::Print` all
use tabular (`<iomanip>`-aligned column) output, not free-form text — match this style for
any new `Print()` methods. Every printable class also gets a matching `friend operator<<`
that just delegates to `Print(os)`.

When extending this library: keep headers flat in `lib/` (not nested subfolders), keep
everything ACLiC-compatible (no ODR violations, inline-safe) even though it's currently only
run uninclu­ded via Cling — compiling with `.L lib/Run.hpp+` may be wanted later.

## Spack environment
The `dune-prototype` Spack environment (activated by `Setup.sh`) is stable/shared — safe to
assume its package versions (per `spack.lock`) persist across sessions and don't need
re-resolving per macro.
