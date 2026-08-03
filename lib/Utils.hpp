#pragma once
//
// Utils.hpp
//
// Shared utility functions for interactive ROOT macros.
//

#include <iostream>
#include <string>

namespace ndlar_light {

/// Prints `prompt` to stdout, then waits for the user to press Enter.
/// Returns true  → user pressed Enter (continue).
/// Returns false → user typed 'q' or 'Q' before Enter (quit).
/// Used by Analysis::Loop() and BaselineCalibrator::Draw() to pause
/// execution between canvas updates.
inline bool PauseExecution(const std::string& prompt = "[Enter] continue   [q] quit: ") {
    std::cout << prompt << std::flush;
    std::string line;
    std::getline(std::cin, line);
    return line.empty() || (line[0] != 'q' && line[0] != 'Q');
}

} // namespace ndlar_light
