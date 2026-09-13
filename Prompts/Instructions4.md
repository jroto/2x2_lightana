Prompt: Add MetaWaveformAna base class and make Analysis pluggable

Context (minimal):

    Namespace: ndlar_light.
    Header-only, ROOT/ACLiC friendly (#pragma once, no dictionaries).
    Current analysis stack:
        Waveform.hpp – raw waveform (adc, channel, samples, clipped).
        EventMetadata.hpp – metadata (id, event, trig_type, sn[8], utime_ms[8], tai_ns[8], IsValid(adc,ch)).
        EventAna.hpp – analyzed event: EventMetadata meta + WaveformAna fWaveformAnas[kNumADCs][kNumChannels].
        WaveformAna.hpp – analysis for one waveform:
            Holds adc, channel, clipped, isValid.
            std::map<std::string,double> results + GetResults().
        Analysis.hpp – builds std::vector<EventAna> fEvents from a Run&.
            process() constructs WaveformAna for each (adc, ch).
            Dump() writes a TTree with one entry per waveform, using WaveformAna::GetResults() to dynamically create branches.

Goal:

Introduce an abstract base class MetaWaveformAna and make WaveformAna inherit from it. Refactor EventAna and Analysis so that:

    Analysis uses runtime polymorphism: the user can select which concrete waveform-analysis class to run (default remains WaveformAna).
    Analysis::Dump() and any existing code continue to work, using only the base-class interface.

1. New base class MetaWaveformAna

Create lib/MetaWaveformAna.hpp (or similar) with:

    class MetaWaveformAna in namespace ndlar_light.

    Pure virtual interface covering what Analysis/Dump and EventAna need:

    cpp

    class MetaWaveformAna {
    public:
        virtual ~MetaWaveformAna() = default;

        virtual int  GetADC() const = 0;
        virtual int  GetChannel() const = 0;
        virtual bool IsClipped() const = 0;
        virtual bool IsValid() const = 0;

        virtual const std::map<std::string, double>& GetResults() const = 0;

        // Optional, but useful for debugging:
        virtual void Print(std::ostream& os = std::cout) const = 0;
    };

    No data members here; it’s purely an interface.

2. Make WaveformAna inherit from MetaWaveformAna

Modify lib/WaveformAna.hpp:

    class WaveformAna : public MetaWaveformAna.

    Implement all pure virtual methods:
        GetADC(), GetChannel(), IsClipped(), IsValid(), GetResults(), Print().

    Keep existing behavior (mean computation, results map, etc.) unchanged.

3. Refactor EventAna to store MetaWaveformAna (polymorphic)

Change lib/EventAna.hpp:

    Instead of:

    cpp

    WaveformAna fWaveformAnas[kNumADCs][kNumChannels];

    Use polymorphic storage, e.g.:

    cpp

    std::unique_ptr<MetaWaveformAna> fWaveformAnas[kNumADCs][kNumChannels];

    Update accessors:

    cpp

    const MetaWaveformAna& GetWaveformAna(int adc, int channel) const;
    MetaWaveformAna&       MutableWaveformAna(int adc, int channel); // if needed

    For construction, keep EventAna default-constructible (pointers start as nullptr).

    Print() should now use MetaWaveformAna::Print() when the pointer is non-null and meta.IsValid() is true.

4. Make Analysis pluggable via a factory

Refactor lib/Analysis.hpp so Analysis knows how to construct concrete analyzers, but stores only MetaWaveformAna:

    Add a type alias for a factory:

    cpp

    using WaveformAnaFactory =
        std::function<std::unique_ptr<MetaWaveformAna>(const Waveform&, bool isValid)>;

    Add a new constructor:

    cpp

    class Analysis {
    public:
        explicit Analysis(Run& run,
                          WaveformAnaFactory factory = DefaultWaveformAnaFactory());
        // ...
    private:
        Run& fRun;
        WaveformAnaFactory fFactory;
        std::vector<EventAna> fEvents;
    };

    Implement DefaultWaveformAnaFactory() (e.g. as a static method in Analysis.hpp) to construct the current WaveformAna:

    cpp

    inline WaveformAnaFactory DefaultWaveformAnaFactory() {
        return [](const Waveform& wf, bool isValid) {
            return std::make_unique<WaveformAna>(wf, isValid);
        };
    }

    In process(), change waveform construction to use the factory and store in EventAna:

    cpp

    for (int adc = 0; adc < kNumADCs; ++adc) {
        for (int ch = 0; ch < kNumChannels; ++ch) {
            bool valid = event.IsValid(adc, ch);
            auto ptr = fFactory(event.GetWaveform(adc, ch), valid);
            // Ensure EventAna exposes a setter that takes unique_ptr<MetaWaveformAna>
            ana.SetWaveformAna(adc, ch, std::move(ptr));
        }
    }

    Analysis::Dump() must be updated to work with MetaWaveformAna pointers:

        Replace uses of WaveformAna with MetaWaveformAna in Dump:

        cpp

        const MetaWaveformAna& wa = *eventAna.GetWaveformAna(adc_idx, ch_idx);
        const auto& results = wa.GetResults();
        clipped = wa.IsClipped();

        When collecting paramNames, use MetaWaveformAna::GetResults() the same way.

5. Keep public APIs aligned and backwards compatible

    Ensure existing code like:

    cpp

    ndlar_light::Analysis analysis(run);
    analysis.process();
    analysis.Dump("out.root");

    still compiles and behaves the same (default is the current WaveformAna).

    The new capability is that advanced users can pass a different factory:

    cpp

    auto myFactory = [](const Waveform& wf, bool valid) {
        return std::make_unique<MyCustomWaveformAna>(wf, valid);
    };
    ndlar_light::Analysis analysis(run, myFactory);

    No changes needed in Run, Event, Waveform, or SubRunReader.

