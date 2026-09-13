Here is the updated prompt:
Prompt: Add Run::Print() and supporting infrastructure
Context

Run is in lib/Run.hpp. It currently has:

    fRunNumber (private int, may be -1 if constructed from file list)
    fSubrunFiles (private std::vector<std::string>)
    fTotalEvents (private size_t)
    fSubrunStart (private std::vector<size_t>, prefix sums — fSubrunStart[i+1] - fSubrunStart[i] = events in subrun i)
    fChannelMap (private ChannelMap)
    Init() — opens each subrun briefly to count events, called from both constructors
    GetEvent(globalIndex) — already uses random access via ReadRow(row, event) on any row

EventMetadata provides:

    GetUTimeMs(adc) — uint64_t, milliseconds since Unix epoch
    GetSerialNumber(adc) — int32_t

SubRunReader::ReadRow(row, event) supports direct random access to any row including the last, via HDF5 hyperslab selection. Reading row N-1 is exactly as cheap as reading row 0.
Changes required
1. Add private cached fields to Run

cpp

uint64_t fStartTimeMs  = 0;                  // utime_ms[0] of first event
uint64_t fEndTimeMs    = 0;                  // utime_ms[0] of last event
std::array<int32_t, kNumADCs> fStartSn = {}; // sn[8] of first event
std::string fChannelMapPath = "(none — all channels active)";

2. Update Init() to cache first and last event metadata

At the end of Init(), after fSubrunStart and fTotalEvents are fully built, add:

cpp

// Cache first-event metadata (start time, serial numbers)
{
    SubRunReader r(fSubrunFiles.front(), &fChannelMap);
    Event e;
    r.ReadRow(0, e);
    fStartTimeMs = e.Meta().GetUTimeMs(0);
    for (int adc = 0; adc < kNumADCs; ++adc)
        fStartSn[adc] = e.Meta().GetSerialNumber(adc);
}

// Cache last-event metadata (end time) via direct random access
// ReadRow supports any row index — reading the last row is as
// cheap as reading the first (HDF5 hyperslab selection).
{
    const size_t lastSubrun = fSubrunFiles.size() - 1;
    const size_t lastRow    = fSubrunStart[lastSubrun + 1]
                            - fSubrunStart[lastSubrun] - 1;
    SubRunReader r(fSubrunFiles.back(), &fChannelMap);
    Event e;
    r.ReadRow(lastRow, e);
    fEndTimeMs = e.Meta().GetUTimeMs(0);
}

3. Update LoadDefaultChannelMap() to record the path

cpp

void LoadDefaultChannelMap() {
    try {
        fChannelMap    = ChannelMap::LoadFromCSV(kDefaultChannelMapPath);
        fChannelMapPath = kDefaultChannelMapPath;
    } catch (...) {
        fChannelMap    = ChannelMap();
        fChannelMapPath = "(none — all channels active)";
    }
}

Update SetChannelMap() the same way:

cpp

void SetChannelMap(const std::string& csvPath) {
    fChannelMap    = ChannelMap::LoadFromCSV(csvPath);
    fChannelMapPath = csvPath;
}

4. Add Run::Print(std::ostream& os = std::cout) const

Produce output in this format:

text

============================================================
 Run summary
============================================================
 Run number      : 1130   (or "N/A (constructed from file list)")
 Total events    : 45320
 Subruns         : 4

 Subrun files:
   [0]  /path/to/file_p00000.FLOW.hdf5   (11200 events)
   [1]  /path/to/file_p00001.FLOW.hdf5   (11400 events)
   [2]  /path/to/file_p00002.FLOW.hdf5   (11360 events)
   [3]  /path/to/file_p00003.FLOW.hdf5   (11360 events)

 Start time      : 2026-07-16 14:05:34 UTC  (raw: 1752674734123 ms)
 End time        : 2026-07-16 15:12:07 UTC  (raw: 1752678727451 ms)
 Run duration    : 1h 06m 33s

 ADC serial numbers (first event):
   ADC 0: 100042   ADC 1: 100043   ADC 2: 100044   ADC 3: 100045
   ADC 4: 100046   ADC 5: 100047   ADC 6: 100048   ADC 7: 100049

 Channel map     : data/channel_map.csv
 Active channels : 312 / 512
   ADC 0:  39 active    ADC 1:  40 active    ADC 2:  38 active    ADC 3:  39 active
   ADC 4:  40 active    ADC 5:  38 active    ADC 6:  39 active    ADC 7:  39 active
============================================================

Implementation notes:

    UTC time string: convert fStartTimeMs / 1000 to time_t, pass to std::gmtime, format with std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tm_ptr).
    Duration: uint64_t durationSec = (fEndTimeMs - fStartTimeMs) / 1000. Print as Xh Ym Zs using integer division.
    Run number: if fRunNumber == -1, print "N/A (constructed from file list)".
    Events per subrun: fSubrunStart[i+1] - fSubrunStart[i].
    Active channels per ADC: iterate fChannelMap.IsActive(adc, ch) for all ch in [0, kNumChannels).

5. Add operator<< overload

cpp

friend std::ostream& operator<<(std::ostream& os, const Run& r) {
    r.Print(os);
    return os;
}

Required includes in Run.hpp

Add if not already present:

cpp

#include <array>
#include <ctime>
#include <iomanip>
#include <sstream>

Summary of changes
File	Change
lib/Run.hpp	Add fStartTimeMs, fEndTimeMs, fStartSn, fChannelMapPath; update Init(), LoadDefaultChannelMap(), SetChannelMap(); add Print() and operator<<
Everything else	No change