#pragma once

#include <string>
#include <nlohmann/json.hpp>

class Settings {
public:
    static Settings& instance();

    // Generic key-based accessors — checks data_ first, then defaults_, then defaultVal.
    bool getBool(const char* key, bool defaultVal) const;
    void setBool(const char* key, bool val);
    int getInt(const char* key, int defaultVal) const;
    void setInt(const char* key, int val);

    // Typed convenience accessors.
    bool showFps() const { return getBool("showFps", false); }
    void setShowFps(bool v) { setBool("showFps", v); }
    int doubleClickTimeMs() const { return getInt("doubleClickTimeMs", 256); }
    int portDisplayMode() const { return getInt("portDisplayMode", 1); }
    void setPortDisplayMode(int v) { setInt("portDisplayMode", v); }

    // Load defaults from settings.json.default, saving current values to "backup".
    void loadDefaults();

    // Restore values from previously-saved backup.
    void restoreBackup();

    // Whether a backup exists (so UI can show/hide the restore button).
    bool hasBackup() const;

private:
    Settings();
    void load();
    void save();
    std::string path_;
    nlohmann::json data_;
    nlohmann::json defaults_;
};
