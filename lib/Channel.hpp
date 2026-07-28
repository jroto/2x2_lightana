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
