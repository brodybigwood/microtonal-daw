#include "Settings.h"
#include <fstream>
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
