#pragma once
//
// HistName.hpp
//
// Pure C++ (no ROOT dependency) utility class that creates, validates, and
// parses deterministic ROOT histogram names from structured analysis metadata.
//
// Canonical name grammar:
//
//   h_adc{adc}_ch{ch}_run{run}_time{time}_mode{mode}_tag{tag}
//
// Example:
//
//   h_adc2_ch4_run1130_time1786665600_modecharge_tagvbrscan
//
// Field encoding:
//
//   adc   — decimal integer, must be in [0, kNumADCs)
//   ch    — decimal integer, must be in [0, kNumChannels)
//   run   — signed decimal integer; -1 means "no run number" (Run built
//            from a file list), values < -1 are rejected
//   time  — UTC Unix timestamp in whole seconds (std::uint64_t); zero is valid
//   mode  — non-empty string, restricted to [A-Za-z0-9_-]+
//   tag   — non-empty string, restricted to [A-Za-z0-9_-]+
//
// Spaces, slashes, dots, colons, percent signs, quotes, and all other
// characters outside [A-Za-z0-9_-] are rejected in mode and tag.
//
// HistName is a logical metadata encoding — it does not own any ROOT object
// and does not implicitly associate with any ROOT TDirectory or TH1.
// The full metadata can always be reconstructed from a valid canonical name
// via HistName::Parse().
//
// Round-trip guarantee:
//
//   HistName::Parse(h.ToString()).ToString() == h.ToString()
//

#include "EventMetadata.hpp"  // kNumADCs, kNumChannels

#include <cctype>
#include <cstdint>
#include <regex>
#include <stdexcept>
#include <string>

namespace ndlar_light {

/// Structured metadata for a ROOT histogram name.
///
/// See the file header for the exact grammar, validation rules, and
/// round-trip guarantee.
class HistName {
public:
    /// Construct a validated HistName.
    ///
    /// Throws std::invalid_argument if:
    ///   - adc is outside [0, kNumADCs)
    ///   - ch  is outside [0, kNumChannels)
    ///   - run < -1
    ///   - mode or tag are empty or contain characters outside [A-Za-z0-9_-]
    HistName(int adc,
             int ch,
             int run,
             std::uint64_t time,
             const std::string& mode,
             const std::string& tag)
        : fAdc(adc), fCh(ch), fRun(run), fTime(time), fMode(mode), fTag(tag)
    {
        Validate(fAdc, fCh, fRun, fMode, fTag);
    }

    int           ADC()     const { return fAdc;  }
    int           Channel() const { return fCh;   }
    int           Run()     const { return fRun;  }
    std::uint64_t Time()    const { return fTime; }
    const std::string& Mode() const { return fMode; }
    const std::string& Tag()  const { return fTag;  }

    /// Return the canonical ROOT-safe histogram name for this metadata.
    /// The result always round-trips exactly through Parse().
    std::string ToString() const
    {
        return "h_adc"  + std::to_string(fAdc)
             + "_ch"    + std::to_string(fCh)
             + "_run"   + std::to_string(fRun)
             + "_time"  + std::to_string(fTime)
             + "_mode"  + fMode
             + "_tag"   + fTag;
    }

    /// Parse and validate a canonical histogram name.
    ///
    /// Throws std::invalid_argument with a descriptive message if the input
    /// does not match the exact canonical grammar or if any field fails its
    /// range/text constraint. Internally delegates to TryParse().
    static HistName Parse(const std::string& name)
    {
        HistName out(0, 0, 0, 0, "x", "x"); // placeholder
        std::string err;
        if (!TryParse(name, out, &err)) {
            throw std::invalid_argument(
                "HistName::Parse: invalid histogram name \"" + name + "\": " + err);
        }
        return out;
    }

    /// Non-throwing parser.
    ///
    /// Returns true and fills `output` on success.
    /// Returns false on any parse or validation failure; if `errorMessage`
    /// is not nullptr, a useful diagnostic is written there.
    static bool TryParse(const std::string& name,
                         HistName& output,
                         std::string* errorMessage = nullptr)
    {
        // The regex captures mode and tag correctly even when they contain
        // underscores, because:
        //   - the _tag literal label is a fixed anchor;
        //   - mode is captured as everything between _mode and _tag;
        //   - tag is captured as everything after _tag to end-of-string.
        //
        // Grammar (simplified):
        //   h_adc<int>_ch<int>_run<-?int>_time<uint>_mode<text>_tag<text>
        //
        // Both <text> fields match [A-Za-z0-9_-]+ which is enforced by
        // the constructor's text-component validator after extraction.
        static const std::regex kPattern(
            R"(^h_adc(-?\d+)_ch(\d+)_run(-?\d+)_time(\d+)_mode([A-Za-z0-9_-]+)_tag([A-Za-z0-9_-]+)$)");

        std::smatch m;
        if (!std::regex_match(name, m, kPattern)) {
            if (errorMessage)
                *errorMessage = "does not match canonical pattern "
                                "h_adc{adc}_ch{ch}_run{run}_time{time}_mode{mode}_tag{tag}";
            return false;
        }

        int adc, ch, run;
        std::uint64_t time;
        std::string mode, tag;

        try {
            adc  = std::stoi(m[1].str());
            ch   = std::stoi(m[2].str());
            run  = std::stoi(m[3].str());
            // Use stoull; the regex already ensured it is digits-only.
            time = static_cast<std::uint64_t>(std::stoull(m[4].str()));
            mode = m[5].str();
            tag  = m[6].str();
        } catch (const std::exception& e) {
            if (errorMessage)
                *errorMessage = std::string("numeric conversion failed: ") + e.what();
            return false;
        }

        // Validate through the constructor (range + text checks).
        std::string valErr;
        if (!ValidateSafe(adc, ch, run, mode, tag, &valErr)) {
            if (errorMessage) *errorMessage = valErr;
            return false;
        }

        // Construct the result object — uses the private constructor to avoid
        // a second round of validation (already done above).
        output = HistName(adc, ch, run, time, mode, tag);

        // Canonicality check: the re-serialised form must equal the input.
        if (output.ToString() != name) {
            if (errorMessage)
                *errorMessage = "serialisation round-trip mismatch "
                                "(non-canonical spelling detected)";
            return false;
        }

        return true;
    }

    /// Return true if `value` is a valid text component for mode or tag:
    /// non-empty, only [A-Za-z0-9_-].
    static bool IsValidTextComponent(const std::string& value)
    {
        if (value.empty()) return false;
        for (char c : value) {
            if (!std::isalnum(static_cast<unsigned char>(c))
                && c != '_' && c != '-') {
                return false;
            }
        }
        return true;
    }

private:
    int           fAdc;
    int           fCh;
    int           fRun;
    std::uint64_t fTime;
    std::string   fMode;
    std::string   fTag;

    /// Shared validation logic — throws std::invalid_argument on failure.
    static void Validate(int adc, int ch, int run,
                         const std::string& mode, const std::string& tag)
    {
        std::string err;
        if (!ValidateSafe(adc, ch, run, mode, tag, &err))
            throw std::invalid_argument("HistName: " + err);
    }

    /// Non-throwing validation: returns true on success; fills `err` on failure.
    static bool ValidateSafe(int adc, int ch, int run,
                              const std::string& mode, const std::string& tag,
                              std::string* err)
    {
        if (adc < 0 || adc >= static_cast<int>(kNumADCs)) {
            if (err) *err = "adc " + std::to_string(adc)
                          + " out of range [0, " + std::to_string(kNumADCs) + ")";
            return false;
        }
        if (ch < 0 || ch >= static_cast<int>(kNumChannels)) {
            if (err) *err = "ch " + std::to_string(ch)
                          + " out of range [0, " + std::to_string(kNumChannels) + ")";
            return false;
        }
        if (run < -1) {
            if (err) *err = "run " + std::to_string(run)
                          + " is below minimum allowed value -1";
            return false;
        }
        if (!IsValidTextComponent(mode)) {
            if (err) *err = "mode \"" + mode + "\" is empty or contains "
                           "characters outside [A-Za-z0-9_-]";
            return false;
        }
        if (!IsValidTextComponent(tag)) {
            if (err) *err = "tag \"" + tag + "\" is empty or contains "
                           "characters outside [A-Za-z0-9_-]";
            return false;
        }
        return true;
    }
};

} // namespace ndlar_light
