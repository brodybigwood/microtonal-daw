#pragma once

#include <cstdint>
#include <vector>

/// Reads a native X11 window (e.g. a VST3 editor) into a PNG buffer.
/// Kept out of ControlApi.cpp because X11's Window/Region typedefs collide
/// with the DAW's own types.
bool x11WindowToPng(void* handle, std::vector<uint8_t>& png, int& w, int& h);
