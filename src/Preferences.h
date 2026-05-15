#pragma once

#include "Bus.h"
#include "Settings.h"

/** Double-click time window in milliseconds. */
#define DCT (Settings::instance().doubleClickTimeMs())

namespace Preferences {

inline PortDisplayMode defaultPortDisplayMode() {
    return static_cast<PortDisplayMode>(Settings::instance().defaultPortDisplayMode());
}

} // namespace Preferences
