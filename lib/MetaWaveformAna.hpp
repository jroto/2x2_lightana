#pragma once
//
// MetaWaveformAna.hpp
//
// Abstract interface for per-waveform analysis results. Exists so
// Analysis/EventAna/Dump can work with any concrete waveform-analysis
// implementation (the default being WaveformAna) via runtime
// polymorphism - see Analysis.hpp's WaveformAnaFactory.
//
// Pure interface: no data members, no default implementation.
//

#include <iostream>
#include <map>
#include <ostream>
#include <string>

namespace ndlar_light {

class MetaWaveformAna {
public:
    virtual ~MetaWaveformAna() = default;

    virtual int GetADC() const = 0;
    virtual int GetChannel() const = 0;
    virtual bool IsClipped() const = 0;
    virtual bool IsValid() const = 0;

    /// Generic access to all computed analysis parameters (e.g. "mean",
    /// "rms", ...). Analysis::Dump() discovers branch names dynamically
    /// from this map, so any implementation's keys automatically show up
    /// in the TTree output.
    virtual const std::map<std::string, double>& GetResults() const = 0;

    /// Human-readable console output; concrete classes decide the format.
    virtual void Print(std::ostream& os = std::cout) const = 0;
};

} // namespace ndlar_light
