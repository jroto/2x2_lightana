#pragma once
//
// HistCollection.hpp
//
// Manages a collection of ROOT histogram/graph objects together with their
// ndlar_light::HistName metadata. Supports:
//
//   TH1F, TH1D  (and any future concrete TH1 subclass)
//   TH2F, TH2D  (and any other concrete TH2 subclass — all derive from TH1)
//   TGraph
//   TGraphErrors (derives from TGraph — accepted naturally)
//
// Unsupported families: TCanvas, TF1, TTree, TDirectory, raw TNamed, etc.
//
// Ownership model
// ---------------
// HistCollection owns independent clones of all added objects.  Add() does NOT
// take ownership of the caller's object — the caller retains its original
// unchanged.  Cloned TH1 objects are detached from any ROOT TDirectory via
// SetDirectory(nullptr).
//
// Central invariant
// -----------------
// For every stored entry:
//
//   entry.object->GetName() == entry.name.ToString()
//
// This invariant is enforced by Add(), checked by Dump(), and re-established
// by Load() through Add().  HistName::ToString() is the authoritative object
// name; caller-provided histogram names are overridden.
//
// Dump() behaviour
// ----------------
// Writes all stored objects to a new ROOT file using "RECREATE".  No append or
// update mode is supported.
//
// Load() behaviour
// ----------------
// Transactional: the current collection is replaced only if the load succeeds
// completely.  Unsupported top-level file objects are skipped with a warning to
// std::cerr.  Supported objects with names that cannot be parsed by
// HistName::Parse() cause Load() to fail — a malformed name is a metadata
// integrity failure, not a soft skip.  Duplicate canonical names also cause
// Load() to fail.
//

#include "HistName.hpp"

#include "TFile.h"
#include "TGraph.h"
#include "TH1.h"
#include "TKey.h"
#include "TNamed.h"
#include "TObject.h"
#include "TList.h"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace std;

namespace ndlar_light {

/// One entry in the collection: structured metadata + owned ROOT object.
/// The invariant  entry.object->GetName() == entry.name.ToString()
/// must hold for every entry at all times.
struct HistEntry {
    HistName                 name;
    std::unique_ptr<TObject> object;
};

/// ROOT-aware container for histogram/graph objects with HistName metadata.
///
/// See file header for ownership model, central invariant, Dump/Load
/// semantics, and supported ROOT object families.
class HistCollection {
public:
    HistCollection()                                    = default;
    ~HistCollection()                                   = default;

    // Non-copyable (owned unique_ptrs; clone explicitly if a copy is needed)
    HistCollection(const HistCollection&)               = delete;
    HistCollection& operator=(const HistCollection&)    = delete;

    // Movable
    HistCollection(HistCollection&&) noexcept           = default;
    HistCollection& operator=(HistCollection&&) noexcept = default;

    // -----------------------------------------------------------------------
    // Basic queries
    // -----------------------------------------------------------------------

    std::size_t Size()  const { return fEntries.size(); }
    bool        Empty() const { return fEntries.empty(); }

    void Clear() { fEntries.clear(); }

    // -----------------------------------------------------------------------
    // Add
    // -----------------------------------------------------------------------

    /// Clone and store a supported ROOT object.
    ///
    /// The stored clone is renamed to name.ToString() regardless of the
    /// name the caller gave to `object`.  The caller's original object is
    /// never modified.
    ///
    /// Throws:
    ///   std::invalid_argument — unsupported ROOT type or duplicate name
    ///   std::runtime_error   — Clone() returned null or result is not TNamed
    void Add(const HistName& name, const TObject& object)
    {
        // 1. Check supported type.
        if (!IsSupportedType(object)) {
            const char* cls = object.ClassName();
            throw std::invalid_argument(
                std::string("HistCollection::Add: unsupported ROOT type '")
                + (cls ? cls : "<unknown>") + "'");
        }

        // 2. Reject duplicates.
        const std::string canonical = name.ToString();
        if (Contains(canonical)) {
            throw std::invalid_argument(
                "HistCollection::Add: duplicate canonical name \""
                + canonical + "\"");
        }

        // 3. Clone the input object (caller's object is not modified).
        TObject* rawCopy = object.Clone();
        if (!rawCopy) {
            throw std::runtime_error(
                "HistCollection::Add: Clone() returned nullptr for \""
                + canonical + "\"");
        }
        std::unique_ptr<TObject> copy(rawCopy);

        // 4. Verify TNamed compatibility (all supported types are TNamed).
        TNamed* named = dynamic_cast<TNamed*>(copy.get());
        if (!named) {
            throw std::runtime_error(
                "HistCollection::Add: cloned object is not TNamed for \""
                + canonical + "\"");
        }

        // 5. Rename to canonical form — overrides whatever name the caller used.
        named->SetName(canonical.c_str());

        // 6. Detach TH1 clones from any ROOT directory.
        if (TH1* hist = dynamic_cast<TH1*>(copy.get())) {
            hist->SetDirectory(nullptr);
        }

        // 7. Store.
        HistEntry entry{name, std::move(copy)};
        fEntries.push_back(std::move(entry));
    }

    // -----------------------------------------------------------------------
    // Containment queries
    // -----------------------------------------------------------------------

    /// Return whether an entry with this HistName is stored.
    bool Contains(const HistName& name) const
    {
        return Find(name.ToString()) != fEntries.cend();
    }

    /// Return whether an entry with this canonical ROOT object name is stored.
    bool Contains(const std::string& canonicalName) const
    {
        return Find(canonicalName) != fEntries.cend();
    }

    // -----------------------------------------------------------------------
    // Access by HistName / canonical name
    // -----------------------------------------------------------------------

    /// Return the stored object.  Throws std::out_of_range when absent.
    const TObject& Get(const HistName& name) const
    {
        return Get(name.ToString());
    }

    /// Return the stored object by canonical name.  Throws std::out_of_range
    /// when absent.
    const TObject& Get(const std::string& canonicalName) const
    {
        auto it = Find(canonicalName);
        if (it == fEntries.cend()) {
            throw std::out_of_range(
                "HistCollection::Get: \"" + canonicalName + "\" not found");
        }
        return *(it->object);
    }

    /// Return a typed const pointer to the stored object, or nullptr if the
    /// HistName is absent or the stored object is not of type T.
    template <typename T>
    const T* GetAs(const HistName& name) const
    {
        auto it = Find(name.ToString());
        if (it == fEntries.cend()) return nullptr;
        return dynamic_cast<const T*>(it->object.get());
    }

    // -----------------------------------------------------------------------
    // Index-based access
    // -----------------------------------------------------------------------

    /// Metadata of the entry at `index`.  Throws std::out_of_range.
    const HistName& GetName(std::size_t index) const
    {
        if (index >= fEntries.size())
            throw std::out_of_range("HistCollection::GetName: index out of range");
        return fEntries[index].name;
    }

    /// ROOT object of the entry at `index`.  Throws std::out_of_range.
    const TObject& GetObject(std::size_t index) const
    {
        if (index >= fEntries.size())
            throw std::out_of_range("HistCollection::GetObject: index out of range");
        return *(fEntries[index].object);
    }

    // -----------------------------------------------------------------------
    // Persistence
    // -----------------------------------------------------------------------

    /// Write all stored objects to a new ROOT file (RECREATE).
    ///
    /// Throws std::runtime_error  if the file cannot be created.
    /// Throws std::logic_error    if the central name/metadata invariant is
    ///                            violated (should never happen in normal use).
    void Dump(const std::string& filename, std::string option="RECREATE") const
    {
        TFile file(filename.c_str(), option.c_str());
        if (file.IsZombie()) {
            throw std::runtime_error(
                "HistCollection::Dump: cannot create ROOT file \""
                + filename + "\"");
        }

        for (const auto& entry : fEntries) {
            // Verify central invariant before writing.
            const std::string storedName = entry.object->GetName();
            const std::string expected   = entry.name.ToString();
            if (storedName != expected) {
                throw std::logic_error(
                    "HistCollection::Dump: invariant violation — "
                    "stored object name \"" + storedName
                    + "\" != HistName \"" + expected + "\"");
            }
            entry.object->Write(expected.c_str(), TObject::kOverwrite);
//            file.WriteObject(entry.object.get(), expected.c_str(), TObject::kOverwrite);
        }

        file.Write();
        file.Close();
        std::cout << "HistCollection::Dump: wrote " << fEntries.size()
                  << " object(s) to \"" << filename << "\"\n";
    }

    /// Replace this collection with all valid supported objects from a ROOT
    /// file.  Transactional: on any failure the current collection is intact.
    ///
    /// Unsupported top-level objects are skipped with a warning to std::cerr.
    /// Supported objects with unparseable names, or duplicate canonical names,
    /// cause Load() to throw std::runtime_error without modifying *this.
    void Load(const std::string& filename)
    {
        TFile file(filename.c_str(), "READ");
        if (!file.IsOpen() || file.IsZombie()) {
            throw std::runtime_error(
                "HistCollection::Load: cannot open ROOT file \""
                + filename + "\"");
        }

        // Build a temporary collection; replace *this only on full success.
        HistCollection loaded;

        TList* keys = file.GetListOfKeys();
        if (keys) {
            TIter next(keys);
            TKey* key = nullptr;
            while ((key = dynamic_cast<TKey*>(next()))) {
                // Read the object from the file (owned by the TFile).
                TObject* obj = file.Get(key->GetName());
                if (!obj) {
                    std::cerr << "HistCollection::Load: warning: could not read key \""
                              << key->GetName() << "\" — skipping.\n";
                    continue;
                }

                // Skip unsupported types with a warning.
                if (!IsSupportedType(*obj)) {
                    std::cerr << "HistCollection::Load: warning: skipping unsupported "
                              << "object type '" << obj->ClassName()
                              << "' (name: \"" << obj->GetName() << "\").\n";
                    continue;
                }

                // Parse the canonical name — failure is a metadata error.
                HistName name = HistName::Parse(obj->GetName()); // throws on bad name

                // Add() clones the object (detaches from TFile) and checks
                // for duplicates.
                loaded.Add(name, *obj);
            }
        }

        // All objects loaded and cloned; the TFile can now close safely.
        file.Close();

        // Commit.
        *this = std::move(loaded);
        std::cout << "HistCollection::Load: loaded " << fEntries.size()
                  << " supported object(s) from \"" << filename << "\"\n";
    }
    std::vector<TH1*> GetByChannel(int adc, int ch) const {
        if (adc < 0 || adc >= static_cast<int>(kNumADCs))
            throw std::invalid_argument(
                "HistCollection::GetChannel: adc " + std::to_string(adc) +
                " out of range [0, " + std::to_string(kNumADCs) + ")");
        if (ch < 0 || ch >= static_cast<int>(kNumChannels))
            throw std::invalid_argument(
                "HistCollection::GetChannel: ch " + std::to_string(ch) +
                " out of range [0, " + std::to_string(kNumChannels) + ")");

        std::vector<TH1*> result;
        int nMatched = 0;
        for (const auto& entry : fEntries) {
            if (entry.name.ADC() == adc && entry.name.Channel() == ch) {
                ++nMatched;
                TH1* h = dynamic_cast<TH1*>(entry.object.get());
                if (h) result.push_back(h);
            }
        }

        if (nMatched > 0 && result.empty())
            throw std::invalid_argument(
                "HistCollection::GetChannel: entries found for ADC " +
                std::to_string(adc) + " CH " + std::to_string(ch) +
                " but none are TH1-derived (are they TGraph objects?)");

        return result;
    }
    std::vector<TH1*> GetByChannelRun(int adc, int ch, int run) const {
        if (adc < 0 || adc >= static_cast<int>(kNumADCs))
            throw std::invalid_argument(
                "HistCollection::GetChannel: adc " + std::to_string(adc) +
                " out of range [0, " + std::to_string(kNumADCs) + ")");
        if (ch < 0 || ch >= static_cast<int>(kNumChannels))
            throw std::invalid_argument(
                "HistCollection::GetChannel: ch " + std::to_string(ch) +
                " out of range [0, " + std::to_string(kNumChannels) + ")");

        std::vector<TH1*> result;
        int nMatched = 0;
        for (const auto& entry : fEntries) {
            if (entry.name.ADC() == adc && entry.name.Channel() == ch && entry.name.Run() == run) {
                ++nMatched;
                TH1* h = dynamic_cast<TH1*>(entry.object.get());
                if (h) result.push_back(h);
            }
        }

        if (nMatched > 0 && result.empty())
            throw std::invalid_argument(
                "HistCollection::GetChannel: entries found for ADC " +
                std::to_string(adc) + " CH " + std::to_string(ch) +
                " but none are TH1-derived (are they TGraph objects?)");
        if(result.size()==0) {
            std::cerr << "HistCollection::GetByChannelRun: WARNING!!! No TH1 objects found for ADC "
             << adc << ", CH " << ch << ", Run " << run << ". You are probably about to get a segfault.\n";
        }

        return result;
    }
    void Print() const {
        std::cout << "HistCollection: " << fEntries.size() << " entries\n";
        for (const auto& entry : fEntries) {
            std::cout << "  Name: " << entry.name.ToString()
                      << ", Class: " << entry.object->ClassName() << "\n";
        }
    }

private:
    std::vector<HistEntry> fEntries;

    // -----------------------------------------------------------------------
    // Helpers
    // -----------------------------------------------------------------------

    /// Return true for ROOT object families this class supports.
    /// TH1F, TH1D, TH2F, TH2D etc. all derive from TH1.
    /// TGraphErrors derives from TGraph.
    static bool IsSupportedType(const TObject& object)
    {
        return dynamic_cast<const TH1*>  (&object) != nullptr ||
               dynamic_cast<const TGraph*>(&object) != nullptr;
    }

    /// Linear search by canonical name.  Returns cend() when not found.
    std::vector<HistEntry>::const_iterator
    Find(const std::string& canonicalName) const
    {
        return std::find_if(
            fEntries.cbegin(), fEntries.cend(),
            [&canonicalName](const HistEntry& e) {
                return e.name.ToString() == canonicalName;
            });
    }
};

} // namespace ndlar_light
