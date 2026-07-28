Prompt: Add ChannelMap and integrate into Run, SubRunReader, and Event
Context

    Namespace: ndlar_light. Header-only, ROOT/ACLiC friendly (#pragma once, no dictionaries).
    Constants in NDLArLight.hpp: kNumADCs = 8, kNumChannels = 64, kNumSamples = 600.
    Relevant existing classes:
        Waveform.hpp — fixed-size value type. Default state: fAdc = -1, fChannel = -1, zeroed samples.
        Event.hpp — holds EventMetadata fMeta and Waveform fWaveforms[kNumADCs][kNumChannels] (fixed array). IsValid(adc, ch) delegates to fMeta.wvfm_valid[adc][ch].
        SubRunReader.hpp — reads one HDF5 file. ReadRow(row, event) reads the full compound row into stack buffers EventsRecord and WvfmRecord, then calls FillEvent() which loops over all 8×64 channels unconditionally copying samples into event.MutableWaveform(adc, ch) and calling meta.SetValid(adc, ch, ...).
        Run.hpp — owns a SubRunReader, exposes Reset(), HasNext(), NextEvent().
        Analysis::process() — checks event.IsValid(adc, ch) before constructing WaveformAna. No changes needed here.

Goal

Add Channel and ChannelMap classes. Integrate ChannelMap into Run and SubRunReader so that:

    Inactive channels are never copied into the Event's Waveform slots — their slots remain default-constructed.
    meta.SetValid(adc, ch, ...) is forced to false for inactive channels, regardless of the HDF5 data — so all downstream code (IsValid, Analysis::process(), Dump()) ignores them with zero changes.
    The user can load a channel map from a CSV or override it at any time via Run.

Note: The HDF5 read still loads all samples into the stack WvfmRecord buffer — skipping happens at the copy step inside FillEvent(), not at the HDF5 level. The Waveform fixed array in Event is unchanged.

No changes to WaveformAna, EventAna, Analysis, EventAna, or macros.
1. New file lib/Channel.hpp

cpp

#pragma once
#include <string>

namespace ndlar_light {

struct Channel {
    int         adc       = -1;
    int         channel   = -1;
    int         tpc       = -1;
    float       x         = 0.f;
    float       y         = 0.f;
    float       z         = 0.f;
    std::string trap_type = "";
    bool        active    = true;
};

} // namespace ndlar_light

2. New file lib/ChannelMap.hpp

cpp

#pragma once
#include "Channel.hpp"
#include "NDLArLight.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace ndlar_light {

class ChannelMap {
public:
    /// Default constructor: all channels active, no physical metadata.
    ChannelMap() {
        for (int a = 0; a < kNumADCs; ++a)
            for (int c = 0; c < kNumChannels; ++c) {
                fChannels[a][c].adc     = a;
                fChannels[a][c].channel = c;
                fChannels[a][c].active  = true;
            }
    }

    /// Load from CSV. Expected header (order must match):
    ///   adc,channel,tpc,x,y,z,trap_type,active
    /// `active` is 1 (active) or 0 (inactive).
    /// Throws std::runtime_error on file or parse errors.
    static ChannelMap LoadFromCSV(const std::string& path) {
        ChannelMap cm;
        std::ifstream f(path);
        if (!f.is_open())
            throw std::runtime_error(
                "ChannelMap::LoadFromCSV: cannot open '" + path + "'");
        std::string line;
        std::getline(f, line); // skip header
        while (std::getline(f, line)) {
            if (line.empty()) continue;
            std::istringstream ss(line);
            std::string tok;
            Channel ch;
            std::getline(ss, tok, ','); ch.adc       = std::stoi(tok);
            std::getline(ss, tok, ','); ch.channel   = std::stoi(tok);
            std::getline(ss, tok, ','); ch.tpc       = std::stoi(tok);
            std::getline(ss, tok, ','); ch.x         = std::stof(tok);
            std::getline(ss, tok, ','); ch.y         = std::stof(tok);
            std::getline(ss, tok, ','); ch.z         = std::stof(tok);
            std::getline(ss, tok, ','); ch.trap_type = tok;
            std::getline(ss, tok, ','); ch.active    = (std::stoi(tok) != 0);
            if (ch.adc < 0 || ch.adc >= kNumADCs ||
                ch.channel < 0 || ch.channel >= kNumChannels)
                throw std::runtime_error(
                    "ChannelMap::LoadFromCSV: out-of-range entry");
            cm.fChannels[ch.adc][ch.channel] = ch;
        }
        return cm;
    }

    bool IsActive(int adc, int ch) const {
        return fChannels[adc][ch].active;
    }

    const Channel& GetChannel(int adc, int ch) const {
        return fChannels[adc][ch];
    }

    /// Mark a single channel active or inactive at runtime.
    void SetActive(int adc, int ch, bool active) {
        fChannels[adc][ch].active = active;
    }

private:
    Channel fChannels[kNumADCs][kNumChannels];
};

} // namespace ndlar_light

3. NDLArLight.hpp — add default path constant

Add one line:

cpp

static constexpr const char* kDefaultChannelMapPath = "data/channel_map.csv";

4. Modify SubRunReader.hpp
4a. Add include

cpp

#include "ChannelMap.hpp"

4b. Add const ChannelMap* parameter to constructor

cpp

explicit SubRunReader(const std::string& filename,
                      const ChannelMap* channelMap = nullptr)
    : fFilename(filename)
    , fChannelMap(channelMap)
    , fFile(filename, HighFive::File::ReadOnly)
{ /* existing body unchanged */ }

Add private member:

cpp

const ChannelMap* fChannelMap = nullptr; // non-owning, may be null

4c. Change FillEvent to a non-static member and add the skip

Change FillEvent from static to a regular member (so it can access fChannelMap), and add the inactive-channel skip inside the per-channel loop:

cpp

void FillEvent(const detail::EventsRecord& er,
               const detail::WvfmRecord& wr,
               Event& event)
{
    EventMetadata& meta = event.Meta();
    meta.SetId(er.id);
    meta.SetEventNumber(er.event);
    meta.SetTriggerType(er.trig_type);

    for (int adc = 0; adc < kNumADCs; ++adc) {
        meta.SetSerialNumber(adc, er.sn[adc]);
        meta.SetUTimeMs(adc, er.utime_ms[adc]);
        meta.SetTaiNs(adc, er.tai_ns[adc]);

        for (int ch = 0; ch < kNumChannels; ++ch) {

            // Inactive channels: force valid=false, leave Waveform
            // slot default-constructed (no copy). Downstream code
            // (IsValid, Analysis::process) will skip them naturally.
            if (fChannelMap && !fChannelMap->IsActive(adc, ch)) {
                meta.SetValid(adc, ch, false);
                continue;
            }

            meta.SetValid(adc, ch, er.wvfm_valid[adc][ch] != 0);
            Waveform& wf = event.MutableWaveform(adc, ch);
            wf.SetADC(adc);
            wf.SetChannel(ch);
            wf.SetClipped(wr.clipped[adc][ch] != 0);
            int16_t* dst       = wf.MutableData();
            const int16_t* src = wr.samples[adc][ch];
            for (size_t s = 0; s < kNumSamples; ++s) dst[s] = src[s];
        }
    }
}

Also update the call site in ReadRow from FillEvent(...) (static call) to this->FillEvent(...) or just FillEvent(...).
5. Modify Run.hpp
5a. Add includes

cpp

#include "ChannelMap.hpp"

5b. Add private member

cpp

ChannelMap fChannelMap;

5c. Constructor: try to load default CSV, fall back silently

cpp

// In Run constructor body, before opening the first SubRunReader:
try {
    fChannelMap = ChannelMap::LoadFromCSV(kDefaultChannelMapPath);
} catch (...) {
    fChannelMap = ChannelMap(); // all channels active
}

5d. Pass fChannelMap to SubRunReader

Wherever Run constructs a SubRunReader, pass &fChannelMap:

cpp

fReader = std::make_unique<SubRunReader>(fFiles[fCurrentFile], &fChannelMap);

5e. Add public user-facing methods

cpp

/// Reload the channel map from a CSV file.
/// Takes effect from the next call to Reset() + process().
void SetChannelMap(const std::string& csvPath) {
    fChannelMap = ChannelMap::LoadFromCSV(csvPath);
}

/// Programmatically activate or deactivate a single channel.
void SelectChannel(int adc, int ch, bool active) {
    fChannelMap.SetActive(adc, ch, active);
}

/// Read-only access to the current channel map.
const ChannelMap& GetChannelMap() const { return fChannelMap; }

6. Add a sample data/channel_map.csv

Create data/channel_map.csv with the correct header and one row per (adc, channel) pair (8×64 = 512 rows), all set to active=1 as a starting point:

text

adc,channel,tpc,x,y,z,trap_type,active
0,0,0,0.0,0.0,0.0,unknown,1
0,1,0,0.0,0.0,0.0,unknown,1
...

Summary of files changed
File	Change
lib/Channel.hpp	New
lib/ChannelMap.hpp	New
lib/NDLArLight.hpp	Add kDefaultChannelMapPath
lib/SubRunReader.hpp	Accept const ChannelMap*; skip inactive channels in FillEvent()
lib/Run.hpp	Own ChannelMap; load default CSV; expose SetChannelMap(), SelectChannel(), GetChannelMap(); pass map to SubRunReader
data/channel_map.csv	New — default all-active map
Everything else	No change