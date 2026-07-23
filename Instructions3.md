Prompt: Add a ROOT TTree dump method to ndlar_light::Analysis (dynamic analysis variables)
Context

This extends the existing header-only C++/ROOT library ndlar_light, which reads and analyzes DUNE ND-LAr light-system HDF5 “FLOW” files event by event via HighFive + the raw HDF5 C API.

Current, implemented architecture (from lib/ and existing design docs , , ):

    Data model:
        lib/Waveform.hpp — raw per-(ADC, channel) waveform:
            int16_t samples[600] (kNumSamples).
            bool clipped.
            int adc, int channel indices.
        lib/EventMetadata.hpp — per-event metadata from /light/events/data:
            uint64_t id.
            int32_t event.
            uint8_t trig_type.
            per-ADC arrays: sn[8], utime_ms[8], tai_ns[8].
            bool wvfm_valid[8][64] and IsValid(adc, channel).
        lib/Event.hpp — one event:
            EventMetadata meta.
            8×64 matrix of Waveform .

    I/O layer , :
        lib/SubRunReader.hpp — internal single-file reader:
            Uses HighFive for file/dataset handling.
            Uses raw HDF5 C API (compound types + hyperslabs) to read one row at a time from /light/events/data and /light/wvfm/data.
            Fills a caller-supplied Event in place (no per-event heap allocations).
        lib/Run.hpp — full run abstraction:
            Represents a run as an ordered list of subrun files.
            Sequential API: Reset(), HasNext(), NextEvent().
            Random access: GetEvent(globalIndex) via a prefix-sum table .

    Analysis layer , , , :
        lib/WaveformAna.hpp — analysis results per waveform:
            Copies adc, channel, and clipped from Waveform.
            Stores bool isValid (from Event::IsValid(adc, ch)).
            Holds a generic map: std::map<std::string, double> results.
            Defines known key constants (e.g. static constexpr const char* kMean = "mean";) and typed getters (GetMean()).
            For now, computes only "mean" (arithmetic mean of all samples) .
        lib/EventAna.hpp — analyzed counterpart to Event:
            EventMetadata meta.
            8×64 matrix of WaveformAna.
        lib/Analysis.hpp — analysis driver:
            Holds Run& fRun (non-owning reference).
            Holds std::vector<EventAna> fEvents.
            void process():
                Calls fRun.Reset().
                Streams raw Events via HasNext() / NextEvent().
                For each event:
                    Copies event.Meta() into a new EventAna::Meta().
                    For each (adc, channel), constructs WaveformAna from the raw waveform and Event::IsValid(adc, ch).
                Appends each EventAna to fEvents.

The key design decision for this iteration: we want to export all analysis variables present in WaveformAna::results, not a hardcoded list like "mean" only . This ensures that when new analysis quantities are added in WaveformAna, they automatically appear in the ROOT output without touching the dump code again.
Goal of this iteration

Add a method to ndlar_light::Analysis that dumps the contents of fEvents (vector of EventAna) into a ROOT file containing a TTree with one entry per waveform, and with branches for:

    event-level metadata ,
    per-waveform indexing and flags ,
    all analysis quantities present in the WaveformAna::results map (dynamically discovered at runtime) .

This functionality lives only in the analysis layer (Analysis.hpp) to keep the lower-level reader clean and header-only , , .
New API in lib/Analysis.hpp

Modify lib/Analysis.hpp to:

    Include the necessary ROOT headers at the top:

    cpp

    #include "TFile.h"
    #include "TTree.h"

    Add a new public method to ndlar_light::Analysis:

    cpp

    namespace ndlar_light {

    class Analysis {
    public:
        explicit Analysis(Run& run);
        void process();
        const std::vector<EventAna>& GetEvents() const;
        size_t GetNEvents() const;

        /// Dump the current analysis results (fEvents) into a ROOT file.
        /// - filename: output ROOT file path (created or overwritten).
        /// - treename: name of the TTree inside the file (default: "waveforms").
        /// Uses only the already-computed contents of fEvents; does NOT
        /// re-read the Run or call process().
        void Dump(const std::string& filename,
                  const std::string& treename = "waveforms") const;

    private:
        Run& fRun;
        std::vector<EventAna> fEvents;
    };

    } // namespace ndlar_light

TTree structure

Dump() should create a ROOT TFile and TTree with the following branches.
1. Event-level metadata (from EventMetadata)

Branches (types are ROOT C++ typedefs):

    event_id — ULong64_t:
        Filled from meta.GetId().
    event_number — Int_t:
        Filled from meta.GetEventNumber().
    trig_type — UChar_t:
        Filled from meta.GetTriggerType().

Per-ADC metadata (for the ADC this waveform belongs to):

    sn — Int_t:
        Filled from meta.GetSerialNumber(adc).
    utime_ms — ULong64_t:
        Filled from meta.GetUTimeMs(adc).
    tai_ns — ULong64_t:
        Filled from meta.GetTaiNs(adc).

2. Per-waveform indexing and flags ,

    adc — Int_t:
        The ADC index (0–7).
    channel — Int_t:
        The channel index (0–63).
    valid — Bool_t:
        meta.IsValid(adc, channel).
    clipped — Bool_t:
        wa.IsClipped() from the corresponding WaveformAna.

3. Analysis variables (dynamic from WaveformAna::results)

For every distinct key string present in any WaveformAna::GetResults() map across all EventAna in fEvents, create one branch:

    Branch name: exactly the key string (e.g. "mean", "rms", "integral").
    Type: Double_t (ROOT double).
    Semantics:
        For a given waveform, if its results map contains that key, the branch value is the corresponding double.
        If not present, the branch value is set to a sentinel (recommended: NaN).

Branch name constraints:

    Assume that WaveformAna keys are chosen to comply with ROOT TTree leaf naming rules (no spaces or invalid punctuation) . The dump code does not attempt to sanitize or transform names.

Detailed behavior of Analysis::Dump

Implement Dump in lib/Analysis.hpp as follows.
Step 0: Includes and helpers

At the top of Analysis.hpp (after existing includes):

cpp

#include "TFile.h"
#include "TTree.h"
#include <set>
#include <limits>

We’ll use std::set<std::string> for collecting parameter names and std::numeric_limits<double>::quiet_NaN() as a sentinel.
Step 1: Handle empty fEvents

If fEvents is empty:

    Create a ROOT file and an empty TTree, then return:

    cpp

    if (fEvents.empty()) {
        TFile outfile(filename.c_str(), "RECREATE");
        if (outfile.IsZombie()) {
            throw std::runtime_error("Analysis::Dump: failed to create ROOT file '" + filename + "'");
        }
        TTree* tree = new TTree(treename.c_str(), "NDLAr light analysis per waveform");
        outfile.cd();
        tree->Write();
        outfile.Close();
        return;
    }

This ensures downstream tools can still open the file and see the tree, even if it has zero entries.
Step 2: Collect union of parameter names from WaveformAna::results

Before creating analysis branches, traverse all EventAna and all WaveformAna in fEvents to obtain the union of all keys in results :

cpp

std::set<std::string> paramNames;
for (const auto& eventAna : fEvents) {
    for (int adc = 0; adc < kNumADCs; ++adc) {
        for (int ch = 0; ch < kNumChannels; ++ch) {
            const WaveformAna& wa = eventAna.GetWaveformAna(adc, ch);
            const auto& results = wa.GetResults();
            for (const auto& kv : results) {
                paramNames.insert(kv.first);
            }
        }
    }
}

    This is a read-only pass over the in-memory fEvents.
    It determines the full set of column names for the TTree, satisfying ROOT’s requirement that branches are defined before filling.

Step 3: Create ROOT file and static branches

Create the file and tree:

cpp

TFile outfile(filename.c_str(), "RECREATE");
if (outfile.IsZombie()) {
    throw std::runtime_error("Analysis::Dump: failed to create ROOT file '" + filename + "'");
}

TTree* tree = new TTree(treename.c_str(), "NDLAr light analysis per waveform");

Define C++ variables for static branches:

cpp

// Event-level
ULong64_t event_id;
Int_t     event_number;
UChar_t   trig_type;
Int_t     sn;
ULong64_t utime_ms;
ULong64_t tai_ns;

// Waveform-level
Int_t  adc;
Int_t  channel;
Bool_t valid;
Bool_t clipped;

Create branches:

cpp

tree->Branch("event_id",     &event_id,     "event_id/l");
tree->Branch("event_number", &event_number, "event_number/I");
tree->Branch("trig_type",    &trig_type,    "trig_type/b");

tree->Branch("sn",      &sn,      "sn/I");
tree->Branch("utime_ms",&utime_ms,"utime_ms/l");
tree->Branch("tai_ns",  &tai_ns,  "tai_ns/l");

tree->Branch("adc",     &adc,     "adc/I");
tree->Branch("channel", &channel, "channel/I");
tree->Branch("valid",   &valid,   "valid/O");
tree->Branch("clipped", &clipped, "clipped/O");

Step 4: Create dynamic analysis branches (from paramNames)

We need one Double_t per parameter name with a stable address:

cpp

struct ParamBranch {
    std::string name;
    Double_t    value;
};

std::vector<ParamBranch> paramBranches;
paramBranches.reserve(paramNames.size());

for (const auto& pname : paramNames) {
    ParamBranch pb;
    pb.name  = pname;
    pb.value = std::numeric_limits<double>::quiet_NaN(); // default sentinel
    tree->Branch(pb.name.c_str(), &pb.value, (pname + "/D").c_str());
    paramBranches.push_back(std::move(pb));
}

    Each entry in paramBranches corresponds to one branch named exactly as the analysis key string .
    Initial value is NaN; it will be overwritten for waveforms that actually contain that key.

Step 5: Loop over all events and waveforms to fill the tree

For each EventAna in fEvents, and for each (adc, channel):

cpp

for (const auto& eventAna : fEvents) {
    const EventMetadata& meta = eventAna.Meta();

    for (int adc_idx = 0; adc_idx < kNumADCs; ++adc_idx) {
        // Per-ADC metadata
        sn       = meta.GetSerialNumber(adc_idx);
        utime_ms = meta.GetUTimeMs(adc_idx);
        tai_ns   = meta.GetTaiNs(adc_idx);

        for (int ch_idx = 0; ch_idx < kNumChannels; ++ch_idx) {
            const WaveformAna& wa = eventAna.GetWaveformAna(adc_idx, ch_idx);
            const auto& results   = wa.GetResults();

            // Event-level fields
            event_id     = meta.GetId();
            event_number = meta.GetEventNumber();
            trig_type    = meta.GetTriggerType();

            // Waveform-level fields
            adc     = adc_idx;
            channel = ch_idx;
            valid   = meta.IsValid(adc_idx, ch_idx);
            clipped = wa.IsClipped();

            // Reset all param values to NaN (or chosen sentinel)
            for (auto& pb : paramBranches) {
                pb.value = std::numeric_limits<double>::quiet_NaN();
            }

            // Fill params that exist in this waveform's results map
            for (auto& pb : paramBranches) {
                auto it = results.find(pb.name);
                if (it != results.end()) {
                    pb.value = it->second;
                }
            }

            tree->Fill();
        }
    }
}

Design choices:

    We create one TTree entry for every (event, adc, channel) waveform .
        Whether that waveform is “meaningful” is encoded in the valid branch (from EventMetadata::IsValid) , .
    For analysis variables:
        If a key exists for that waveform, we write its double value.
        If it doesn’t, the corresponding branch entry for this waveform is NaN.

Step 6: Write and close file

After the loops:

cpp

outfile.cd();
tree->Write();
outfile.Close();

No explicit delete tree; is required, but it’s acceptable to add it after Write() if you prefer explicit deletion.
Requirements and constraints

    Header-only & ACLiC-friendly , :
        Analysis.hpp remains a header with #pragma once.
        Including ROOT’s TFile.h and TTree.h at the top is allowed; users will compile or run within ROOT (cling / ACLiC).
        No ROOT dictionaries or ClassDef macros are introduced .
    Layering , :
        Only Analysis.hpp depends on ROOT I/O.
        The lower layers (Waveform, Event, EventMetadata, SubRunReader, Run, WaveformAna, EventAna) remain free of ROOT I/O concerns.
    Dynamic analysis variables :
        Dump discovers all analysis variable names from WaveformAna::GetResults() and creates one Double_t branch per distinct key.
        Future additions to analysis (e.g. "rms", "integral", "peak_time") only require changes in WaveformAna and whoever fills results, not in Dump.
    No extra data reading:
        Dump uses only the data in fEvents (i.e. the output of process()).
        It does not re-iterate Run or re-open input files.

Optional example macro

If convenient, add a new macro macros/example_dump.C demonstrating:

    Build a Run from a file list (same as example_analysis.C).
    Construct Analysis analysis(run), call analysis.process().
    Call analysis.Dump("analysis.root").
    Optionally, open analysis.root in the same macro and print the TTree branches (e.g. via TFile f("analysis.root"); f.ls();; or tree->Print();) to show dynamically created analysis columns.

This is optional but useful as an integration test and demonstration.