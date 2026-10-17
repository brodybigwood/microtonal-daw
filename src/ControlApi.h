#pragma once

#include <string>

// Executes one JSON request line against the running DAW.
//
// Runs on the GUI thread (called from ControlServer::poll), so it may read and
// mutate the GUI graph directly through the UndoManager. Returns one response
// line without a trailing newline. Never throws.
class ControlApi {
public:
    static std::string handle(const std::string& line);
};
