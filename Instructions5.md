Prompt: Refactor MetaWaveformAna/WaveformAna to use an index-based registry instead of per-instance maps
Context (minimal)

    Namespace: ndlar_light.
    Header-only, ROOT/ACLiC friendly (#pragma once, no dictionaries).
    Current relevant classes:
        MetaWaveformAna.hpp – abstract interface:
            GetADC(), GetChannel(), IsClipped(), IsValid().
            GetResults() returns const std::map<std::string,double>&.
            Print().
        WaveformAna.hpp – concrete implementation:
            Inherits MetaWaveformAna.
            Holds adc, channel, clipped, valid.
            Stores std::map<std::string,double> fResults and fills "mean".
            Provides GetMean() etc.
        EventAna.hpp – analyzed event:
            EventMetadata fMeta.
            std::unique_ptr<MetaWaveformAna> fWaveformAnas[kNumADCs][kNumChannels];.
        Analysis.hpp:
            WaveformAnaFactory (std::function<std::unique_ptr<MetaWaveformAna>(const Waveform&, bool)>).
            Analysis::process() uses the factory to fill EventAna.
            Dump():
                Collects the union of all keys from wa.GetResults().
                Creates one TTree Double_t branch per key.
                For each waveform, fills branches from wa.GetResults(); missing keys → NaN.

Design goal now [K5–K6]: avoid storing the same strings and maps in every waveform; use a shared registry mapping parameter names to indices, and let each MetaWaveformAna implementation store a dense vector of doubles (plus a notion of presence), while keeping the external behavior (string-named parameters, dynamic branches in Dump()).
Goal

Refactor MetaWaveformAna, WaveformAna, and Analysis::Dump() to:

    Use a global/shared registry of parameter names:
        Each parameter name is registered once and assigned a stable index.
        All analyzers refer to parameters by index internally.
    Make each concrete analyzer store analysis values in a vector indexed by that registry, not in a std::map.
    Expose a new index-based API at the MetaWaveformAna level (and keep convenient string-based helpers built on top).
    Update Dump() to use the index-based API + registry, while still creating branches named by the parameter strings and filling them correctly.

No other parts (Run, Event, EventMetadata, Waveform, SubRunReader, macros) should be changed.
1. Redesign MetaWaveformAna interface to be index-based

Modify lib/MetaWaveformAna.hpp:

    Remove the GetResults() pure virtual and its map-based semantics.

    Introduce index-based virtual methods:

    cpp

    class MetaWaveformAna {
    public:
        virtual ~MetaWaveformAna() = default;

        virtual int  GetADC() const = 0;
        virtual int  GetChannel() const = 0;
        virtual bool IsClipped() const = 0;
        virtual bool IsValid() const = 0;

        // New: index-based access
        virtual bool   HasParamIndex(std::size_t index) const = 0;
        virtual double GetParamByIndex(std::size_t index) const = 0;

        virtual void Print(std::ostream& os = std::cout) const = 0;

        // Convenience string-based helpers (non-virtual):
        static std::size_t RegisterParam(const std::string& name);
        static std::size_t ParamIndex(const std::string& name);  // throws if unknown
        static const std::vector<std::string>& ParamNames();     // all registered names

        bool   HasParam(const std::string& name) const;
        double GetParam(const std::string& name) const;
    };

    Implement a static registry inside MetaWaveformAna (private or in an internal namespace) that:
        Maintains std::vector<std::string> names; and std::unordered_map<std::string,std::size_t> indexByName;.
        Guarantees that:
            RegisterParam(name) returns a stable index for that name, registering it if needed.
            ParamIndex(name) returns the existing index or throws.
            ParamNames() returns names.

    Keep this registry header-only and safe to use from ROOT/ACLiC (e.g. function-local statics for initialization).

2. Make WaveformAna use the registry and vectors

In lib/WaveformAna.hpp:

    Define a static index for "mean":

    cpp

    class WaveformAna : public MetaWaveformAna {
    public:
        static const std::size_t kMeanIndex;
        static constexpr const char* kMeanName = "mean";
        // ...
    };

    And in the implementation (still header-only):

    cpp

    inline const std::size_t WaveformAna::kMeanIndex =
        MetaWaveformAna::RegisterParam(WaveformAna::kMeanName);

    Replace std::map<std::string,double> fResults with dense storage:

    cpp

    private:
        int  fAdc = -1;
        int  fChannel = -1;
        bool fClipped = false;
        bool fValid = false;

        std::vector<double> fParams;   // size >= MetaWaveformAna::ParamNames().size()
        std::vector<bool>   fHasParam; // same size; or use NaN-sentinel instead

    Choose one of:
        fHasParam[index] marks presence, and fParams[index] holds the value, or
        No fHasParam, but use NaN as “missing” sentinel (then HasParamIndex() checks std::isnan).

    In the constructor:

        Ensure fParams (and fHasParam) are resized to at least ParamNames().size() at construction time (so they can contain all currently known indices).

        Compute the mean and store it at kMeanIndex:

        cpp

        fParams.resize(MetaWaveformAna::ParamNames().size(), std::numeric_limits<double>::quiet_NaN());
        // if using fHasParam, also resize and initialize to false

        double sum = 0.0;
        for (std::size_t s = 0; s < wf.Size(); ++s) sum += wf.GetSample(s);
        double mean = sum / static_cast<double>(wf.Size());

        fParams[kMeanIndex] = mean;
        // if using fHasParam: fHasParam[kMeanIndex] = true;

    Implement the new virtual methods:

    cpp

    bool HasParamIndex(std::size_t index) const override {
        if (index >= fParams.size()) return false;
        // either check fHasParam[index], or !std::isnan(fParams[index])
    }

    double GetParamByIndex(std::size_t index) const override {
        if (!HasParamIndex(index)) {
            throw std::out_of_range("WaveformAna::GetParamByIndex: missing index ...");
        }
        return fParams[index];
    }

    Implement typed GetMean() via kMeanIndex:

    cpp

    double GetMean() const {
        return GetParamByIndex(kMeanIndex);
    }

    Implement Print() using the registry (parameter names from MetaWaveformAna::ParamNames() and values from GetParamByIndex() where HasParamIndex() is true).

3. Update Analysis::Dump to use the registry + index-based API

In lib/Analysis.hpp:

    Stop relying on wa.GetResults() and union-of-maps. Instead:

        Use the registry once to get the full set of parameter names:

        cpp

        const auto& allNames = MetaWaveformAna::ParamNames();

        Assume this is the complete union of all parameters used by all analyzers (each implementation registers its keys at static initialization).

    Create dynamic branches for all registered parameter names:

    cpp

    std::vector<ParamBranch> paramBranches;
    paramBranches.reserve(allNames.size());

    for (const auto& pname : allNames) {
        ParamBranch pb;
        pb.name  = pname;
        pb.index = MetaWaveformAna::ParamIndex(pname); // store index here
        pb.value = std::numeric_limits<double>::quiet_NaN();
        tree->Branch(pb.name.c_str(), &pb.value, (pname + "/D").c_str());
        paramBranches.push_back(std::move(pb));
    }

    Extend ParamBranch to hold the index:

    cpp

    struct ParamBranch {
        std::string name;
        std::size_t index;
        Double_t    value;
    };

    In the fill loop, for each waveform:

    cpp

    const MetaWaveformAna& wa = eventAna.GetWaveformAna(adc_idx, ch_idx);

    // ...

    for (auto& pb : paramBranches) {
        if (wa.HasParamIndex(pb.index)) {
            pb.value = wa.GetParamByIndex(pb.index);
        } else {
            pb.value = std::numeric_limits<double>::quiet_NaN();
        }
    }
    tree->Fill();

    Event-level and waveform-level branches (event_id, adc, channel, valid, clipped, etc.) remain unchanged.

4. Adjust convenience functions and keep external behavior

    Ensure MetaWaveformAna::HasParam(name) and GetParam(name) are implemented using:

    cpp

    std::size_t idx = ParamIndex(name);
    return HasParamIndex(idx);
    // and GetParamByIndex(idx)

    Existing high-level behavior should remain the same:
        Analysis analysis(run);
        analysis.process();
        analysis.Dump("out.root");

    still produces a TTree with one branch per analysis variable (e.g. "mean"), and values filled correctly. The only internal change is that analysis variables are stored and accessed index-wise.

    WaveformAna keeps its typed getters (GetMean()), and new analyzers can follow the same pattern (register their own keys, define static indices, and store into vectors).
