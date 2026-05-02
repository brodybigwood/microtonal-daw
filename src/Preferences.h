#pragma once

#define DCT 256 // doubleclick time

#include "Bus.h"

/** Edit these values to change app-wide defaults (no separate config file). */
namespace Preferences {

/** Initial NodeEditor port style when a patcher window is opened (square IDs vs rect+labels). */
inline constexpr PortDisplayMode defaultPortDisplayMode = PortDisplayMode::RectLabels;

} // namespace Preferences
