#include "Settings.h"
#include <fstream>
#include <algorithm>
using json = nlohmann::json;

Settings& Settings::instance() {
    static Settings s;
    return s;
}

Settings::Settings() {
    path_ = "assets/settings.json";
    load();
    // Load defaults (read-only reference for fallback values).
    std::ifstream df("assets/settings.json.default");
    if (df.is_open()) {
        try { defaults_ = json::parse(df); } catch (...) { defaults_ = json::object(); }
    }
}

void Settings::load() {
    std::ifstream f(path_);
    if (!f.is_open()) return;
    try { data_ = json::parse(f); } catch (...) { data_ = json::object(); }
}

void Settings::save() {
    std::ofstream f(path_);
    if (f.is_open()) f << data_.dump(2);
}

bool Settings::getBool(const char* key, bool defaultVal) const {
    if (data_.contains(key)) {
        try { return data_[key].get<bool>(); } catch (...) {}
    }
    if (defaults_.contains(key)) {
        try { return defaults_[key].get<bool>(); } catch (...) {}
    }
    return defaultVal;
}

void Settings::setBool(const char* key, bool val) {
    data_[key] = val;
    save();
}

int Settings::getInt(const char* key, int defaultVal) const {
    if (data_.contains(key)) {
        try { return data_[key].get<int>(); } catch (...) {}
    }
    if (defaults_.contains(key)) {
        try { return defaults_[key].get<int>(); } catch (...) {}
    }
    return defaultVal;
}

void Settings::setInt(const char* key, int val) {
    data_[key] = val;
    save();
}

void Settings::loadDefaults() {
    // Retry loading the defaults file if it wasn't available at construction time.
    if (defaults_.empty()) {
        std::ifstream df("assets/settings.json.default");
        if (df.is_open()) {
            try { defaults_ = json::parse(df); } catch (...) {}
        }
    }
    if (defaults_.empty()) {
        // Hardcoded fallback defaults so the button always works.
        defaults_["showFps"] = false;
        defaults_["doubleClickTimeMs"] = 256;
        defaults_["portDisplayMode"] = 1;
        defaults_["audioOutputDevice"] = -1;
        defaults_["audioInputDevice"] = -1;
        defaults_["audioBufferSize"] = 3;
        defaults_["audioSampleRate"] = 0;
        defaults_["audioTripleBuffer"] = false;
        defaults_["audioEngine"] = 0;
    }
    json old = data_;           // snapshot current settings
    old.erase("backup");        // remove nested backup if present
    data_ = defaults_;          // reset to defaults
    data_["backup"] = old;      // store previous values
    save();
}

void Settings::restoreBackup() {
    if (!data_.contains("backup")) return;
    data_ = data_["backup"]; // previous state IS the backup
    save();
}

bool Settings::hasBackup() const {
    return data_.contains("backup");
}

std::vector<std::string> Settings::getVstHistory() const {
    std::vector<std::string> result;
    if (data_.contains("vstHistory")) {
        for (auto& p : data_["vstHistory"]) {
            try { result.push_back(p.get<std::string>()); } catch (...) {}
        }
    }
    return result;
}

void Settings::addVstToHistory(const std::string& path) {
    auto history = getVstHistory();
    // Remove if already present, then push to front
    auto it = std::find(history.begin(), history.end(), path);
    if (it != history.end()) history.erase(it);
    history.insert(history.begin(), path);
    // Keep max 20 entries
    if (history.size() > 20) history.resize(20);
    data_["vstHistory"] = history;
    save();
}
