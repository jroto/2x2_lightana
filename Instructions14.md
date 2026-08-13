Modify the current `main` branch of https://github.com/jroto/2x2_lightana.

## Goal

Add a pure C++ utility class named `ndlar_light::HistName` that creates, validates, and parses deterministic ROOT histogram names from the following metadata:

- `adc`
- `ch`
- `run`
- `time`
- `mode`
- `tag`

The class must support both directions:

```cpp
metadata -> canonical histogram name
histogram name -> metadata

It must not depend on ROOT. It should be usable anywhere in the project, including calibration output, charge histograms, amplitude histograms, and future analysis products.
Naming format

Use this exact canonical format:

text

h_adc{adc}_ch{ch}_run{run}_time{time}_mode{mode}_tag{tag}

Example:

text

h_adc2_ch4_run1130_time1786665600_modecharge_tagvbrscan

Meaning:

text

adc  = 2
ch   = 4
run  = 1130
time = 1786665600
mode = "charge"
tag  = "vbrscan"

Field encoding requirements

    adc: decimal integer.

    ch: decimal integer.

    run: signed decimal integer. Support -1, since Run can currently have no explicit run number when constructed from a file list.

    time: UTC Unix timestamp in seconds, stored as an unsigned integer.

    mode: non-empty string restricted to:

    text

    [A-Za-z0-9_-]+

    tag: non-empty string restricted to:

    text

    [A-Za-z0-9_-]+

Reject spaces, slashes, dots, colons, percent signs, quotes, and all other characters outside that allowed set.

Do not silently sanitize or modify invalid mode or tag values. Invalid input must be rejected with a useful exception message.

The output of HistName::ToString() must always be ROOT-object-name safe and must round-trip exactly through HistName::Parse().
New file: lib/HistName.hpp

Create a new header-only class:

cpp

#pragma once

namespace ndlar_light {

class HistName {
    ...
};

} // namespace ndlar_light

Use only standard-library dependencies. Required headers will likely include:

cpp

#include "EventMetadata.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

Add other standard headers only as needed.

Including EventMetadata.hpp is acceptable so that the class can validate ADC and channel ranges using:

cpp

kNumADCs
kNumChannels

Do not include any ROOT headers in HistName.hpp.
Required public API

Implement the following API, or an equivalent API with the same functionality and clear naming:

cpp

class HistName {
public:
    HistName(
        int adc,
        int ch,
        int run,
        std::uint64_t time,
        const std::string& mode,
        const std::string& tag);

    int ADC() const;
    int Channel() const;
    int Run() const;
    std::uint64_t Time() const;
    const std::string& Mode() const;
    const std::string& Tag() const;

    /// Return the canonical ROOT-safe histogram name.
    std::string ToString() const;

    /// Parse and validate one exact canonical histogram name.
    /// Throw std::invalid_argument if parsing or validation fails.
    static HistName Parse(const std::string& name);

    /// Return whether mode/tag satisfy the allowed component grammar.
    static bool IsValidTextComponent(const std::string& value);
};

Optional but recommended:

cpp

    /// Non-throwing parser. Returns false on malformed input.
    /// If errorMessage is not nullptr, write a useful diagnostic there.
    static bool TryParse(
        const std::string& name,
        HistName& output,
        std::string* errorMessage = nullptr);

If TryParse() is implemented, Parse() should call it and throw std::invalid_argument with the diagnostic message on failure.
Validation rules

The constructor and parser must both enforce all of these rules.
ADC and channel

cpp

0 <= adc < kNumADCs
0 <= ch  < kNumChannels

Invalid values must throw std::invalid_argument.
Run

Allow:

cpp

run >= -1

where:

cpp

run == -1

means the run number is unavailable, for example when Run was constructed from an explicit file list.

Reject values less than -1.
Time

Accept any std::uint64_t value, including zero. Do not apply local-time conversion or time-zone conversion: this value is a UTC Unix timestamp in seconds.
Mode and tag

Both must be non-empty and contain only:

text

A-Z
a-z
0-9
_
-

Examples that must be accepted:

text

charge
amplitude
baseline
vbrscan
run3-test
my_tag_01

Examples that must be rejected:

text

""
"charge spectrum"
"tag/name"
"tag.name"
"tag:one"
"tag#1"
"my tag"

Parsing requirements

Parse() must accept only the exact canonical structure:

text

h_adc{adc}_ch{ch}_run{run}_time{time}_mode{mode}_tag{tag}

For example:

cpp

const auto name = ndlar_light::HistName::Parse(
    "h_adc2_ch4_run1130_time1786665600_modecharge_tagvbrscan");

assert(name.ADC() == 2);
assert(name.Channel() == 4);
assert(name.Run() == 1130);
assert(name.Time() == 1786665600ULL);
assert(name.Mode() == "charge");
assert(name.Tag() == "vbrscan");

Malformed inputs must be rejected, including:

text

h_adc2_ch4_run1130_modecharge_tagvbrscan
h_adc2_ch4_run1130_timeabc_modecharge_tagvbrscan
h_adc2_ch4_run1130_time1786665600_modecharge_tagmy tag
h_adc99_ch4_run1130_time1786665600_modecharge_tagvbrscan
wrong_prefix_adc2_ch4_run1130_time1786665600_modecharge_tagvbrscan

Avoid ambiguous parsing based only on splitting at underscores, because both mode and tag are explicitly allowed to contain underscores.

Use a robust parser. Acceptable approaches include:

    a strict regular expression with explicit capture groups; or
    explicit parsing based on the fixed field labels:
        h_adc
        _ch
        _run
        _time
        _mode
        _tag

If using a regular expression, ensure mode and tag are captured correctly even when they contain underscores.

After parsing, construct the result through the normal constructor so parsed values receive the same range and text validation as manually created values.

A useful additional canonicality check is:

cpp

if (parsed.ToString() != inputName) {
    // reject non-canonical spellings
}

This is recommended so the class has exactly one canonical serialization for every metadata set.
Add HistName.hpp to the umbrella header

In lib/NDLArLight.hpp, add:

cpp

#include "HistName.hpp"

Place it near the basic project data/model headers, before analysis-specific headers.
Add a start-time accessor to Run

The histogram metadata time must represent the start time of the run in Unix seconds.

Run already stores:

cpp

uint64_t fStartTimeMs;

Add this public read-only accessor in lib/Run.hpp:

cpp

/// UTC Unix timestamp of the first event in this run, in whole seconds.
std::uint64_t StartTimeUnixSeconds() const
{
    return fStartTimeMs / 1000;
}

Do not change how fStartTimeMs is populated. Do not change the existing Print() output.

Optionally also add:

cpp

/// UTC Unix timestamp of the first event in this run, in milliseconds.
std::uint64_t StartTimeUnixMilliseconds() const
{
    return fStartTimeMs;
}

but StartTimeUnixSeconds() is the required API for HistName users.
Intended usage example

The new class should make histogram creation look like this:

cpp

const ndlar_light::HistName histName(
    adc,
    ch,
    runNumber,
    run.StartTimeUnixSeconds(),
    "charge",
    "vbrscan");

TH1F* hist = new TH1F(
    histName.ToString().c_str(),
    "Hit charge;Hit charge (ADC counts #times ticks);Hits",
    200,
    0.0,
    100000.0);

And parsing later:

cpp

const ndlar_light::HistName metadata =
    ndlar_light::HistName::Parse(hist->GetName());

std::cout
    << "ADC = " << metadata.ADC()
    << ", channel = " << metadata.Channel()
    << ", run = " << metadata.Run()
    << ", start time = " << metadata.Time()
    << ", mode = " << metadata.Mode()
    << ", tag = " << metadata.Tag()
    << "\n";

Documentation requirements

Document:

    the exact naming grammar;
    that time is UTC Unix time in seconds;
    that mode and tag are intentionally restricted to [A-Za-z0-9_-]+;
    that HistName is a logical metadata encoding, not a ROOT pointer or an implicit ROOT association;
    that the full metadata can be reconstructed from a valid canonical name.

Validation / acceptance criteria

    This code must compile:

    cpp

    HistName h(2, 4, 1130, 1786665600ULL, "charge", "vbrscan");
    assert(
        h.ToString() ==
        "h_adc2_ch4_run1130_time1786665600_modecharge_tagvbrscan");

    The following must round-trip:

    cpp

    const std::string encoded = h.ToString();
    const HistName parsed = HistName::Parse(encoded);

    assert(parsed.ADC() == h.ADC());
    assert(parsed.Channel() == h.Channel());
    assert(parsed.Run() == h.Run());
    assert(parsed.Time() == h.Time());
    assert(parsed.Mode() == h.Mode());
    assert(parsed.Tag() == h.Tag());
    assert(parsed.ToString() == encoded);

    Mode and tag values containing underscores and hyphens must round-trip:

    cpp

    HistName h(2, 4, 1130, 1786665600ULL,
               "hit_charge",
               "vbr-scan_01");

    Invalid ADC/channel, runs below -1, malformed names, empty mode/tag, and invalid text characters must throw std::invalid_argument.

    Run::StartTimeUnixSeconds() returns fStartTimeMs / 1000.

    No ROOT header is included by HistName.hpp.

    Do not modify histogram names elsewhere in the project yet, except optionally in one small documented example. This task creates the reusable naming infrastructure first.
