#pragma once

#include <filesystem>
#include <string>

// Directory the DAW was launched from. VST plugins may chdir() the process, so
// relative asset paths must be resolved against this, not the live cwd.
extern std::filesystem::path g_launchDir;

inline std::string resolveAssetPath(const std::string& p) {
    if (p.empty()) return p;
    std::filesystem::path path(p);
    if (path.is_absolute() || g_launchDir.empty()) return p;
    return (g_launchDir / path).string();
}
