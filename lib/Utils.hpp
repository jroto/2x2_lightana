#pragma once
//
// Utils.hpp
//
// Shared utility functions for interactive ROOT macros.
//

#include "TSystem.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include <sys/select.h>
#include <unistd.h>

namespace ndlar_light {

/// Prints `prompt` and waits for terminal input while keeping the ROOT GUI
/// event loop responsive.
///
/// During the pause, ROOT canvas controls remain interactive: the user can
/// zoom, pan, inspect histograms, use context menus, and move/resize windows.
///
/// Returns true  -> user pressed Enter, or entered text other than q/Q.
/// Returns false -> user typed q or Q, or stdin was closed.
///
/// This implementation is intended for the Linux/Unix ROOT environment used
/// by this project. It polls stdin without blocking and calls
/// gSystem->ProcessEvents() between polls.
inline bool PauseExecution(
    const std::string& prompt = "[Enter] continue   [q] quit: ")
{
    std::cout << prompt << std::flush;

    while (true) {
        // Let ROOT dispatch mouse, keyboard, paint, and canvas events.
        gSystem->ProcessEvents();

        // Poll stdin with a zero timeout. std::getline() is called only once
        // a complete terminal line is available, so it will not block.
        fd_set readFds;
        FD_ZERO(&readFds);
        FD_SET(STDIN_FILENO, &readFds);

        timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 0;

        const int status = select(
            STDIN_FILENO + 1, &readFds, nullptr, nullptr, &timeout);

        if (status > 0 && FD_ISSET(STDIN_FILENO, &readFds)) {
            std::string line;

            if (!std::getline(std::cin, line)) {
                return false;
            }

            return line.empty() || (line[0] != 'q' && line[0] != 'Q');
        }

        // Limit CPU use while retaining responsive canvas interaction.
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

} // namespace ndlar_light