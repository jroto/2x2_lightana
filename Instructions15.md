Modify the current `main` branch of https://github.com/jroto/2x2_lightana.

## Goal

Add a new ROOT-aware container class:

```cpp
ndlar_light::HistCollection

HistCollection manages a collection of ROOT histogram/graph objects together with their ndlar_light::HistName metadata.

It must:

    add supported ROOT objects safely;
    ensure every stored ROOT object has the canonical name produced by its associated HistName;
    own independent clones of added objects;
    dump all stored objects to a ROOT file;
    load all valid supported objects from a ROOT file;
    reconstruct HistName metadata by parsing the stored ROOT object names.

The class must support these ROOT object families:

text

TH1F
TH1D
all concrete TH2-derived histogram types, e.g. TH2F and TH2D
TGraph
TGraphErrors

Do not support unrelated ROOT objects such as TCanvas, TF1, TTree, TDirectory, or TNamed by themselves.
Prerequisite

This implementation assumes that the project already contains:

cpp

lib/HistName.hpp

with an API equivalent to:

cpp

class HistName {
public:
    std::string ToString() const;
    static HistName Parse(const std::string& name);

    int ADC() const;
    int Channel() const;
    int Run() const;
    std::uint64_t Time() const;
    const std::string& Mode() const;
    const std::string& Tag() const;
};

HistName::ToString() is the authoritative canonical ROOT object name.
New file: lib/HistCollection.hpp

Create a new header-only class in:

text

lib/HistCollection.hpp

Use this namespace:

cpp

namespace ndlar_light {
    ...
}

Include the required project and ROOT headers. Expected includes include:

cpp

#include "HistName.hpp"

#include "TFile.h"
#include "TGraph.h"
#include "TH1.h"
#include "TKey.h"
#include "TNamed.h"
#include "TObject.h"

#include <algorithm>
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

Add further standard or ROOT headers only where necessary.
Internal design

Do not use parallel vectors such as:

cpp

std::vector<HistName> names;
std::vector<TObject*> objects;

Instead, use one coherent entry type:

cpp

struct HistEntry {
    HistName name;
    std::unique_ptr<TObject> object;
};

The collection stores:

cpp

std::vector<HistEntry> fEntries;

This prevents metadata and ROOT objects from becoming misaligned after insertion, deletion, loading, or failure handling.
Central invariant

For every stored entry:

cpp

entry.object->GetName() == entry.name.ToString()

This invariant must hold:

    after Add();
    after Load();
    before and after Dump();
    after copying/cloning internally.

HistName is the authoritative metadata source.

The stored ROOT object name must always be set from:

cpp

histName.ToString()

Do not treat a caller-provided histogram name as authoritative.
Required public API

Implement at least this interface:

cpp

class HistCollection {
public:
    HistCollection() = default;
    ~HistCollection() = default;

    HistCollection(const HistCollection&) = delete;
    HistCollection& operator=(const HistCollection&) = delete;

    HistCollection(HistCollection&&) noexcept = default;
    HistCollection& operator=(HistCollection&&) noexcept = default;

    std::size_t Size() const;
    bool Empty() const;
    void Clear();

    /// Clone and store a supported ROOT object.
    /// The stored clone is renamed to name.ToString().
    void Add(const HistName& name, const TObject& object);

    /// Return whether this exact metadata/name is stored.
    bool Contains(const HistName& name) const;

    /// Return whether this canonical ROOT object name is stored.
    bool Contains(const std::string& canonicalName) const;

    /// Return the stored generic ROOT object.
    /// Throw std::out_of_range when absent.
    const TObject& Get(const HistName& name) const;

    /// Return the stored generic ROOT object by canonical ROOT name.
    /// Throw std::out_of_range when absent.
    const TObject& Get(const std::string& canonicalName) const;

    /// Return a typed pointer to a stored object, or nullptr if:
    /// - the HistName is absent; or
    /// - the stored object is not of requested type T.
    template <typename T>
    const T* GetAs(const HistName& name) const;

    /// Metadata of the entry at index.
    /// Throw std::out_of_range for an invalid index.
    const HistName& GetName(std::size_t index) const;

    /// ROOT object of the entry at index.
    /// Throw std::out_of_range for an invalid index.
    const TObject& GetObject(std::size_t index) const;

    /// Write all stored objects into a new ROOT file.
    void Dump(const std::string& filename) const;

    /// Replace this collection with all valid supported objects loaded
    /// from a ROOT file.
    void Load(const std::string& filename);
};

A non-const Get() must not be added unless its design preserves the invariant between HistName and TObject::GetName().

The initial implementation should expose read-only objects only.
Supported ROOT type handling

Implement an internal helper equivalent to:

cpp

static bool IsSupportedType(const TObject& object)
{
    return dynamic_cast<const TH1*>(&object) != nullptr ||
           dynamic_cast<const TGraph*>(&object) != nullptr;
}

This is intentional:

    TH1F, TH1D, and concrete TH2 types derive from TH1;
    TGraphErrors derives from TGraph.

Do not check exact class names.

Do not require a special explicit check for TGraphErrors; the TGraph inheritance check must accept it naturally.

Reject unsupported types in Add() by throwing:

cpp

std::invalid_argument

with a useful message containing the ROOT class name where available.
Add() requirements

Implement:

cpp

void Add(const HistName& name, const TObject& object);

Required behavior:

    Check that object is supported.

    Reject duplicates before cloning:
        duplicate canonical name;
        equivalently, duplicate HistName::ToString().

    Clone the input object:

    cpp

    TObject* rawCopy = object.Clone();

    If cloning fails, throw std::runtime_error.

    Take ownership immediately with:

    cpp

    std::unique_ptr<TObject> copy(rawCopy);

    Verify the clone is TNamed-compatible:

    cpp

    TNamed* named = dynamic_cast<TNamed*>(copy.get());

    If not, throw std::runtime_error.

    Rename the clone:

    cpp

    named->SetName(name.ToString().c_str());

    If the cloned object is TH1-derived, detach it from any ROOT directory:

    cpp

    if (TH1* hist = dynamic_cast<TH1*>(copy.get())) {
        hist->SetDirectory(nullptr);
    }

    Store exactly one entry:

    cpp

    fEntries.push_back({name, std::move(copy)});

Add() must not alter the caller’s original object.
Lookup behavior

Implement lookup by canonical string through:

cpp

name.ToString()

A simple linear search is sufficient initially because collections are expected to be modest in size.

Use a private helper such as:

cpp

std::vector<HistEntry>::const_iterator Find(
    const std::string& canonicalName) const;

Do not introduce an unordered-map index unless needed; keeping one source of truth is more important than premature optimization.

For typed access:

cpp

template <typename T>
const T* GetAs(const HistName& name) const
{
    if (!Contains(name)) {
        return nullptr;
    }

    return dynamic_cast<const T*>(&Get(name));
}

It is acceptable to implement this more efficiently through the private finder.
Dump() requirements

Implement:

cpp

void Dump(const std::string& filename) const;

Required behavior:

    Open the output file with:

    cpp

    TFile file(filename.c_str(), "RECREATE");

    Throw std::runtime_error if:

    cpp

    file.IsZombie()

    For every stored entry:

        verify the central invariant:

        cpp

        entry.object->GetName() == entry.name.ToString()

        throw std::logic_error if violated;

        write the object using its canonical HistName name.

A valid approach is:

cpp

file.WriteObject(
    entry.object.get(),
    entry.name.ToString().c_str());

    Write and close the file cleanly.

Use "RECREATE" for this first implementation. Do not implement append/update behavior.

The output file should contain objects such as:

text

h_adc2_ch4_run1130_time1786665600_modecharge_tagvbrscan
h_adc2_ch5_run1130_time1786665600_modecharge_tagvbrscan
h_adc2_ch4_run1130_time1786665600_modeamplitude_tagvbrscan

No additional metadata TTree is needed: HistName encodes all required metadata in the ROOT object name.
Load() requirements

Implement:

cpp

void Load(const std::string& filename);

This must replace the current collection only if loading succeeds completely.
Transactional loading

Use a temporary collection:

cpp

HistCollection loaded;

Populate loaded fully. Only on success:

cpp

*this = std::move(loaded);

If any fatal failure occurs, the current collection must remain unchanged.
Reading workflow

    Open the file with:

    cpp

    TFile file(filename.c_str(), "READ");

    Throw std::runtime_error if the file cannot be opened or is a zombie.

    Iterate through the file’s top-level ROOT keys using TKey.

    For each key:
        read the object;
        skip unsupported object types, printing one concise warning to std::cerr;
        for supported objects:

            parse the object’s ROOT name:

            cpp

            const HistName name = HistName::Parse(object->GetName());

            use:

            cpp

            loaded.Add(name, *object);

            This ensures:
                validation;
                cloning;
                detachment from the input file;
                canonical renaming;
                duplicate detection.

    Let the input TFile close normally after all objects have been cloned into loaded.

    Replace the current collection only after a fully successful load.

Invalid supported objects

If a supported object has a name that cannot be parsed by HistName::Parse(), loading must fail with a useful exception. Do not silently skip a malformed supported object because it is a metadata integrity failure.
Duplicate names

If the file contains multiple supported objects that resolve to the same canonical HistName, loading must fail.

Do not silently select the newest ROOT key cycle.
ROOT ownership constraints

The implementation must respect ROOT ownership rules.

Never retain objects directly from an open TFile, for example do not do this:

cpp

fEntries.push_back(
    {name, std::unique_ptr<TObject>(file.Get(objectName))});

Those objects may be deleted or invalidated when the TFile closes.

Instead, always clone through Add().

For TH1 objects, call:

cpp

hist->SetDirectory(nullptr);

on the clone.

TGraph objects do not need directory detachment.
Add to umbrella header

In lib/NDLArLight.hpp, add:

cpp

#include "HistCollection.hpp"

Place it immediately after:

cpp

#include "HistName.hpp"

or near other reusable utility/model headers.
Documentation

Add clear Doxygen-style comments describing:

    supported ROOT object families;
    that HistCollection owns cloned objects;
    that Add() does not take ownership of the caller’s object;
    the central name/metadata invariant;
    that Dump() uses RECREATE;
    that Load() is transactional;
    that Load() skips unsupported top-level file objects but fails for supported objects with invalid HistName-encoded names.

Validation / acceptance criteria

The following examples must work.
1. TH1F

cpp

TH1F source("temporary", "Source histogram", 100, 0.0, 100.0);
source.Fill(42.0);

HistName name(
    2, 4, 1130, 1786665600ULL,
    "charge", "vbrscan");

HistCollection collection;
collection.Add(name, source);

assert(collection.Size() == 1);
assert(collection.Contains(name));

const TH1F* stored = collection.GetAs<TH1F>(name);
assert(stored != nullptr);
assert(std::string(stored->GetName()) == name.ToString());
assert(std::string(source.GetName()) == "temporary");

2. Type preservation

cpp

TH2F source2D("temporary2d", "2D source", 10, 0, 10, 10, 0, 10);
collection.Add(
    HistName(2, 5, 1130, 1786665600ULL, "correlation", "vbrscan"),
    source2D);

assert(collection.GetAs<TH2F>(
    HistName(2, 5, 1130, 1786665600ULL, "correlation", "vbrscan")
) != nullptr);

Also validate TGraph and TGraphErrors.
3. Duplicate prevention

cpp

collection.Add(name, source);
collection.Add(name, source); // must throw std::invalid_argument

4. Unsupported types

cpp

TF1 function("f", "gaus", -5.0, 5.0);
collection.Add(name, function); // must throw std::invalid_argument

5. ROOT round trip

cpp

collection.Dump("hist_collection_test.root");

HistCollection loaded;
loaded.Load("hist_collection_test.root");

assert(loaded.Size() == collection.Size());

const TH1F* loadedHist = loaded.GetAs<TH1F>(name);
assert(loadedHist != nullptr);
assert(std::string(loadedHist->GetName()) == name.ToString());

Scope constraints

    Do not modify BaselineCalibrator, WaveAna, Analysis, or Run beyond necessary include integration.
    Do not change the HistName format or parser.
    Do not add a separate metadata TTree.
    Do not add support for nested ROOT directories in this task; load only top-level objects.
    Do not add mutable object access that could violate the HistName/ROOT-name invariant.
