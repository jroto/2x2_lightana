#pragma once
#include "Channel.hpp"
#include "EventMetadata.hpp" // defines kNumADCs/kNumChannels; included directly
                             // (rather than NDLArLight.hpp) to avoid a circular
                             // include, since NDLArLight.hpp -> SubRunReader.hpp/
                             // Run.hpp -> ChannelMap.hpp already.
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace ndlar_light {

/// Default location for the channel map CSV, tried by Run's constructors
/// before falling back to an all-channels-active default map. Defined
/// here (rather than NDLArLight.hpp, per the original sketch) so it's
/// available to Run.hpp, which includes this header directly.
static constexpr const char* kDefaultChannelMapPath = "data/channel_map2.csv";

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
        std::cout << "Loading channel map from '" << path << "'...";
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
        std::cout << "Done. Loaded channel map with " << kNumADCs << " ADCs and "
                  << kNumChannels << " channels per ADC." << std::endl;
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
    void ResetAll(bool active=false) {
        for (int a = 0; a < kNumADCs; ++a)
            for (int c = 0; c < kNumChannels; ++c)
                fChannels[a][c].active = active;
    } 

private:
    Channel fChannels[kNumADCs][kNumChannels];
};

} // namespace ndlar_light
