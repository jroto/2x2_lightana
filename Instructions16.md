Modify the current `main` branch of https://github.com/jroto/2x2_lightana.

# Goals

Implement two connected changes:

1. Correct the `HistName` canonical grammar so parsing remains unambiguous while `mode` and `tag` may contain underscores.
2. Add an `Analysis` method that creates one charge histogram per selected channel from already processed events, stores the histograms in a `HistCollection`, and dumps that collection to a ROOT file.

The new analysis output must contain one histogram entry per individual `WaveAna::Hit::charge`.

Do not re-read the `Run` or re-run the waveform analysis in the histogram-dumping method. It must use the existing `Analysis::fEvents` output from `process()`.

---

# Part 1 — Correct `HistName` grammar

## Problem to fix

The current `HistName` format is:

```text
h_adc{adc}_ch{ch}_run{run}_time{time}_mode{mode}_tag{tag}

This is ambiguous when both mode and tag can contain underscores.

For example, the name:

text

h_adc2_ch4_run1130_time1786665600_modehit_charge_tagvbr_scan_01

cannot be parsed unambiguously if both values accept underscores.

The parser must be corrected before relying on HistName::Parse() for persisted histogram output.
New canonical grammar

Replace the old underscore-separated grammar with this exact canonical format:

text

h-adc{adc}-ch{ch}-run{run}-time{time}-mode{mode}-tag{tag}

Example:

text

h-adc2-ch4-run1130-time1786665600-modehit_charge-tagvbr_scan_01

Metadata represented by that name:

text

adc  = 2
ch   = 4
run  = 1130
time = 1786665600
mode = "hit_charge"
tag  = "vbr_scan_01"

The separator is -.

Therefore:

    hyphens are not allowed inside mode;
    hyphens are not allowed inside tag;
    underscores remain allowed inside both mode and tag.

Text validation changes

Update HistName validation.

mode and tag must both be non-empty and restricted to:

text

[A-Za-z0-9_]+

Allowed examples:

text

charge
hit_charge
amplitude
vbrscan
vbr_scan_01
calibration_test

Rejected examples:

text

charge-spectrum
vbr-scan
my tag
tag/name
tag.name
tag:one
""

Do not sanitize invalid text. Throw std::invalid_argument.
Required HistName changes

In lib/HistName.hpp:

    Update all file-level documentation.

    Update ToString() to generate exactly:

    cpp

    "h-adc"  + std::to_string(fAdc)
    + "-ch"  + std::to_string(fCh)
    + "-run" + std::to_string(fRun)
    + "-time" + std::to_string(fTime)
    + "-mode" + fMode
    + "-tag" + fTag;

    Update IsValidTextComponent().

    Update parsing to accept only the new exact grammar.

    Reject every old-format name using underscores as field separators.

    Preserve exact canonical round-trip validation:

    cpp

    HistName::Parse(name.ToString()).ToString() == name.ToString()

A suitable strict regular expression is:

cpp

R"(^h-adc(\d+)-ch(\d+)-run(-?\d+)-time(\d+)-mode([A-Za-z0-9_]+)-tag([A-Za-z0-9_]+)$)"

The -tag delimiter is now unambiguous because hyphens are forbidden inside both mode and tag.
Required HistName validation examples

This must succeed:

cpp

HistName name(
    2,
    4,
    1130,
    1786665600ULL,
    "hit_charge",
    "vbr_scan_01");

assert(
    name.ToString() ==
    "h-adc2-ch4-run1130-time1786665600-modehit_charge-tagvbr_scan_01");

This must throw:

cpp

HistName(
    2,
    4,
    1130,
    1786665600ULL,
    "hit-charge",
    "vbr_scan");

This must throw:

cpp

HistName(
    2,
    4,
    1130,
    1786665600ULL,
    "charge",
    "vbr-scan");

Part 2 — Add Run::RunNumber()

Analysis needs the run number to construct HistName metadata.

In lib/Run.hpp, add this public read-only accessor near the existing run-summary accessors:

cpp

/// Run number supplied when constructing this Run.
/// Returns -1 when the Run was constructed from an explicit file list.
int RunNumber() const
{
    return fRunNumber;
}

Do not change the meaning or storage of fRunNumber.
Part 3 — Fix duplicate ROOT-key handling in HistCollection::Load()

Before using HistCollection for persisted analysis products, fix one important loading behavior.

The current implementation iterates TKey objects but reads them through:

cpp

file.Get(key->GetName());

If a ROOT file has multiple key cycles with the same name, this can silently select one cycle rather than detecting the duplicate.
Required correction

In lib/HistCollection.hpp, inside Load():

    read each key directly with:

    cpp

    TObject* obj = key->ReadObj();

    manage the returned object with a local std::unique_ptr<TObject>;

    parse and add the object normally through:

    cpp

    HistName name = HistName::Parse(obj->GetName());
    loaded.Add(name, *obj);

This allows duplicate canonical histogram names in the input file to reach loaded.Add(), where they must cause loading to fail.

Do not silently choose a ROOT key cycle.

The collection must continue to:

    skip unsupported object classes with a concise warning;
    throw for supported objects with unparsable names;
    throw for duplicate canonical names;
    leave the existing collection unchanged when loading fails.

Part 4 — Add Analysis::DumpChargeHistograms()
New public method

In lib/Analysis.hpp, add:

cpp

/// Build one per-channel individual-hit charge histogram from the
/// already processed analysis results and dump it as a HistCollection.
void DumpChargeHistograms(
    const std::string& filename,
    const std::string& tag) const;

Place it after the existing Dump() method.

Add the required include:

cpp

#include "HistCollection.hpp"

HistCollection.hpp already includes HistName.hpp; adding HistName.hpp separately is optional.
Required behavior

DumpChargeHistograms() must:

    require that process() has already been called;

    use fEvents only;

    not call:
        fRun.Reset();
        fRun.HasNext();
        fRun.NextEvent();
        fFactory();

    obtain a current selected-channel snapshot once:

    cpp

    const auto selectedChannels = fRun.GetSelectedChannels();

    create exactly one TH1F charge histogram per selected channel;

    fill it once for every individual WaveAna::Hit::charge;

    add every completed histogram to a local HistCollection;

    call:

    cpp

    collection.Dump(filename);

The output file must contain only the charge histograms in this new collection.
Preconditions

If fEvents is empty, throw:

cpp

std::logic_error

with a message explaining that the caller must invoke:

cpp

analysis.process();

before DumpChargeHistograms().

If no channels are currently selected, throw:

cpp

std::logic_error

with an explanatory message.

Do not infer selection from the events. The selected-channel snapshot must come from Run.
Histogram binning

Use the same fixed defaults currently used by Analysis::Loop2():

cpp

constexpr int    kChargeHistBins = 200;
constexpr double kChargeHistMin  = 0.0;
constexpr double kChargeHistMax  = 30000.0;

Use one histogram per selected channel.

Each histogram must have:

cpp

"Hit charge (ADC counts #times ticks)"

as the x-axis title, and:

cpp

"Hits"

as the y-axis title.

Use a readable title such as:

cpp

"ADC %d / CH %d individual hit charge;Hit charge (ADC counts #times ticks);Hits"

Do not omit selected channels that have zero hits. Their empty charge histograms are still meaningful and must be stored.
HistName metadata

For every selected (adc, ch) channel, create:

cpp

const HistName histName(
    adc,
    ch,
    fRun.RunNumber(),
    fRun.StartTimeUnixSeconds(),
    "charge",
    tag);

Then add the corresponding final histogram with:

cpp

collection.Add(histName, histogram);

The stored ROOT object must therefore have a name such as:

text

h-adc2-ch4-run1130-time1786665600-modecharge-tagvbr_scan_01

tag must be validated by the HistName constructor. Do not pre-sanitize it in Analysis.
Filling logic

For every EventAna in fEvents and every selected channel:

    skip invalid event/channel slots:

    cpp

    if (!eventAna.Meta().IsValid(adc, ch)) {
        continue;
    }

    retrieve the analyzed waveform:

    cpp

    const MetaWaveformAna& waveformAna =
        eventAna.GetWaveformAna(adc, ch);

    detect hit-aware analyses:

    cpp

    const WaveAna* waveAna =
        dynamic_cast<const WaveAna*>(&waveformAna);

    if waveAna == nullptr, do not fill that event/channel histogram;

    otherwise fill every individual hit:

    cpp

    for (const Hit& hit : waveAna->Hits()) {
        histogram->Fill(hit.charge);
    }

Do not use the total_charge analysis parameter.

Do not fill one entry per waveform.

Do not fill one entry per event unless the event contains exactly one actual hit.

The histogram must represent:

text

all individual Hit::charge values
for one selected channel
over every event in fEvents

Ownership and efficient construction

HistCollection::Add() clones its input object and stores the clone.

Therefore:

    build mutable temporary TH1F objects locally;
    fill them across all fEvents;
    call collection.Add() exactly once per selected channel after filling;
    do not add a histogram to HistCollection before it is filled;
    do not repeatedly clone or allocate a histogram for every event;
    do not use the temporary ROOT histogram names as metadata identifiers.

A suitable internal structure is:

cpp

struct ChargeHistogram {
    HistName metadata;
    std::unique_ptr<TH1F> histogram;
};

Build one ChargeHistogram per selected channel.

After all events are processed:

cpp

for (const auto& item : chargeHistograms) {
    collection.Add(item.metadata, *item.histogram);
}

The HistCollection will assign its own canonical name from the HistName.

For temporary histograms, call:

cpp

histogram->SetDirectory(nullptr);

to avoid accidental ROOT-directory ownership.
Behavior with non-WaveAna factories

Analysis supports generic MetaWaveformAna factories.

If the analysis factory did not produce WaveAna objects:

    the method must not crash;
    all selected channels still receive an empty charge histogram;
    the resulting HistCollection is dumped normally.

Do not throw merely because no WaveAna is present.
Part 5 — Update macros/example_loop.C

Update the example to demonstrate the new workflow.

After:

cpp

analysis.process();

add:

cpp

analysis.DumpChargeHistograms(
    "charge_histograms.root",
    "vbrscan");

Keep the existing analysis-tree output if useful:

cpp

analysis.Dump("analysis.root");

Update comments to explain that:

    analysis.root is the per-waveform analysis TTree;
    charge_histograms.root contains one TH1F per selected channel;
    each histogram is filled once per individual WaveAna::Hit::charge;
    all histogram metadata is encoded in the canonical HistName.

Documentation requirements

Document DumpChargeHistograms() clearly:

    it requires a prior call to process();
    it does not re-read the run;
    it uses the currently selected Run channels;
    it produces one histogram per selected channel;
    it fills one entry per individual hit;
    it uses mode = "charge" and the caller-provided tag;
    it stores the output through HistCollection.

Update all HistName comments to reflect the new hyphen-separated grammar and the new text restriction:

text

[A-Za-z0-9_]+

Acceptance criteria
HistName

cpp

const HistName name(
    2, 4, 1130, 1786665600ULL,
    "hit_charge", "vbr_scan_01");

assert(
    name.ToString() ==
    "h-adc2-ch4-run1130-time1786665600-modehit_charge-tagvbr_scan_01");

const HistName parsed = HistName::Parse(name.ToString());

assert(parsed.Mode() == "hit_charge");
assert(parsed.Tag() == "vbr_scan_01");
assert(parsed.ToString() == name.ToString());

Both of these must throw std::invalid_argument:

cpp

HistName(2, 4, 1130, 1786665600ULL, "hit-charge", "tag");
HistName(2, 4, 1130, 1786665600ULL, "charge", "vbr-scan");

Charge output

Given:

cpp

run.ResetChannels(false);
run.SelectChannel(2, 4, true);
run.SelectChannel(2, 5, true);

analysis.process();

analysis.DumpChargeHistograms(
    "charge_histograms.root",
    "vbr_scan_01");

the output file must contain exactly two supported objects:

text

h-adc2-ch4-run1130-time{start_time}-modecharge-tagvbr_scan_01
h-adc2-ch5-run1130-time{start_time}-modecharge-tagvbr_scan_01

where {start_time} equals:

cpp

run.StartTimeUnixSeconds()

Each object must be a TH1F.

For each channel:

text

TH1F::GetEntries()

must equal the number of individual Hit::charge values found for that channel across all processed events, including underflow and overflow entries according to normal ROOT semantics.

A selected channel with no hits must still be present in the file as an empty TH1F.
Scope constraints

    Do not modify WaveAna::Hit, hit finding, charge calculation, baseline calibration, or Analysis::Loop() / Loop2().
    Do not reprocess the run inside DumpChargeHistograms().
    Do not store a separate metadata TTree.
    Do not add mutable access to HistCollection objects.
    Do not silently skip malformed supported histogram names while loading a HistCollection.
