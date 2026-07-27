#pragma once
//
// MetaWaveformAna.hpp
//
// Abstract interface for per-waveform analysis results. Exists so
// Analysis/EventAna/Dump can work with any concrete waveform-analysis
// implementation (the default being WaveformAna) via runtime
// polymorphism - see Analysis.hpp's WaveformAnaFactory.
//
// Parameter storage is index-based rather than map-based: every
// analysis parameter name (e.g. "mean", "rms", ...) is registered once
// in a process-wide shared registry (RegisterParam), which hands back a
// stable index. Concrete implementations (see WaveformAna) then store
// their computed values in a dense std::vector<double> indexed by that
// registry, instead of each instance carrying its own
// std::map<std::string,double> - this avoids repeating the same key
// strings and map overhead per waveform. The string-based API
// (HasParam/GetParam) is kept as a thin convenience layer on top of the
// index-based one, so callers who just want "mean" don't need to know
// about indices at all.
//
// Pure interface: no data members, no default implementation, aside
// from the static registry helpers (which are shared process-wide, not
// per-instance state).
//

#include <iostream>
#include <ostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace ndlar_light {

class MetaWaveformAna {
public:
    virtual ~MetaWaveformAna() = default;

    virtual int GetADC() const = 0;
    virtual int GetChannel() const = 0;
    virtual bool IsClipped() const = 0;
    virtual bool IsValid() const = 0;

    /// Whether this instance has a value stored for parameter `index`
    /// (registry index, see RegisterParam/ParamIndex below).
    virtual bool HasParamIndex(std::size_t index) const = 0;

    /// Value stored for parameter `index`. Throws std::out_of_range if
    /// HasParamIndex(index) is false.
    virtual double GetParamByIndex(std::size_t index) const = 0;

    /// Human-readable console output; concrete classes decide the format.
    virtual void Print(std::ostream& os = std::cout) const = 0;

    // --- Shared, process-wide parameter-name registry ---
    //
    // Every analysis parameter name used by any MetaWaveformAna
    // implementation is registered exactly once here (typically via a
    // `static const std::size_t kFooIndex = RegisterParam("foo");` member
    // initializer in the concrete class - see WaveformAna::kMeanIndex).
    // Function-local statics make this safe to use from Cling/ACLiC
    // without a separate .cpp/initialization step.

    /// Registers `name` if not already known, returning its (possibly
    /// newly assigned) stable index.
    static std::size_t RegisterParam(const std::string& name)
    {
        auto& registry = MutableRegistry();
        auto it = registry.indexByName.find(name);
        if (it != registry.indexByName.end()) return it->second;

        std::size_t index = registry.names.size();
        registry.names.push_back(name);
        registry.indexByName.emplace(name, index);
        return index;
    }

    /// Looks up the index for an already-registered `name`. Throws
    /// std::out_of_range if `name` was never registered.
    static std::size_t ParamIndex(const std::string& name)
    {
        const auto& registry = MutableRegistry();
        auto it = registry.indexByName.find(name);
        if (it == registry.indexByName.end()) {
            throw std::out_of_range("MetaWaveformAna::ParamIndex: unknown parameter '" + name + "'");
        }
        return it->second;
    }

    /// All parameter names registered so far (in registration order,
    /// i.e. `ParamNames()[i]` is the name for index `i`).
    static const std::vector<std::string>& ParamNames() { return MutableRegistry().names; }

    // --- String-based convenience helpers, built on the index-based
    // virtual API above. ---

    bool HasParam(const std::string& name) const { return HasParamIndex(ParamIndex(name)); }

    double GetParam(const std::string& name) const { return GetParamByIndex(ParamIndex(name)); }

private:
    struct Registry {
        std::vector<std::string> names;
        std::unordered_map<std::string, std::size_t> indexByName;
    };

    /// Function-local static: guarantees the registry is constructed on
    /// first use regardless of static-initialization order across
    /// translation units/headers, which matters for the
    /// `static const std::size_t kFooIndex = RegisterParam(...)`
    /// initialization pattern used by concrete analyzers.
    static Registry& MutableRegistry()
    {
        static Registry registry;
        return registry;
    }
};

} // namespace ndlar_light
