Prompt: Fix static initialization of WaveformAna::kMeanIndex for Cling/ACLiC

Problem: In lib/WaveformAna.hpp, kMeanIndex is declared as an inline const static member initialized via:

cpp

inline const std::size_t WaveformAna::kMeanIndex =
    MetaWaveformAna::RegisterParam(WaveformAna::kMeanName);

In Cling (interpreted ROOT), inline static member initializers are not guaranteed to run before first use, so "mean" may not be registered in the MetaWaveformAna registry when the first WaveformAna is constructed. As a result, ParamNames() is empty at construction time, the mean is never stored at a valid registry index, and Dump() produces all-NaN values for mean.

Fix — changes only in lib/WaveformAna.hpp:

    Remove the inline const static member kMeanIndex and its out-of-class initializer.

    Replace it with a function-local static accessor, following the same safe pattern as MetaWaveformAna::MutableRegistry():

    cpp

    static std::size_t MeanIndex() {
        static const std::size_t idx =
            MetaWaveformAna::RegisterParam(kMeanName);
        return idx;
    }

    In the constructor, call MeanIndex() explicitly at the start to force registration before resizing fParams:

    cpp

    WaveformAna(const Waveform& wf, bool isValid)
        : fAdc(wf.GetADC())
        , fChannel(wf.GetChannel())
        , fClipped(wf.IsClipped())
        , fValid(isValid)
    {
        const std::size_t meanIdx = MeanIndex(); // ensures "mean" is registered

        fParams.resize(MetaWaveformAna::ParamNames().size(),
                       std::numeric_limits<double>::quiet_NaN());

        double sum = 0.0;
        for (std::size_t s = 0; s < wf.Size(); ++s) sum += wf.GetSample(s);

        if (meanIdx >= fParams.size())
            fParams.resize(meanIdx + 1,
                           std::numeric_limits<double>::quiet_NaN());
        fParams[meanIdx] = sum / static_cast<double>(wf.Size());
    }

    Update GetMean() to use MeanIndex():

    cpp

    double GetMean() const { return GetParamByIndex(MeanIndex()); }

    No other files need to change. Analysis.hpp, MetaWaveformAna.hpp, EventAna.hpp, Run.hpp, and all macros remain untouched.

Expected result: After this fix, analysis.Dump("out.root") produces a TTree with a mean branch correctly filled for every waveform.