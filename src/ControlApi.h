#pragma once

#include <string>
#include <utility>
#include <vector>

struct SDL_Renderer;
class WindowManager;

// Executes one JSON request line against the running DAW.
//
// Runs on the GUI thread (called from ControlServer::poll), so it may read and
// mutate the GUI graph directly through the UndoManager. Returns one response
// line without a trailing newline, or "" when the request is deferred (screen
// capture: it must be answered after the frame is rendered). Never throws.
class ControlApi {
public:
    static std::string handle(const std::string& line, int clientFd = -1);

    /// Answered from WindowHandler::tick after rendering but before presenting,
    /// so SDL_RenderReadPixels sees the freshly drawn backbuffers.
    static std::vector<std::pair<int, std::string>> flushCaptures(SDL_Renderer* mainRenderer,
                                                                  WindowManager* wm);
};
