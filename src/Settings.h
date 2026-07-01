#pragma once

#include <string>
#include <vector>
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
    int audioOutputDevice() const { return getInt("audioOutputDevice", -1); }
    void setAudioOutputDevice(int v) { setInt("audioOutputDevice", v); }
    int audioInputDevice() const { return getInt("audioInputDevice", -1); }
    void setAudioInputDevice(int v) { setInt("audioInputDevice", v); }
    int audioBufferSize() const { return getInt("audioBufferSize", 3); }
    void setAudioBufferSize(int v) { setInt("audioBufferSize", v); }
    int audioSampleRate() const { return getInt("audioSampleRate", 0); }
    void setAudioSampleRate(int v) { setInt("audioSampleRate", v); }
    bool audioTripleBuffer() const { return getBool("audioTripleBuffer", false); }
    void setAudioTripleBuffer(bool v) { setBool("audioTripleBuffer", v); }
    int audioEngine() const { return getInt("audioEngine", 0); }
    void setAudioEngine(int v) { setInt("audioEngine", v); }

    std::vector<std::string> getVstHistory() const;
    void addVstToHistory(const std::string& path);

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
