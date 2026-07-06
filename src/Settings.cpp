#include "Settings.h"
#include "styles.h"
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

static void addCol(nlohmann::json& o, const char* k, const uint8_t c[4]) { o[k] = {c[0],c[1],c[2],c[3]}; }

static nlohmann::json builtinDefaultPreset() {
    auto& d = getDefaultColors();
    nlohmann::json j;
    j["immutable"] = true;
    auto& col = j["colors"];
    addCol(col, "background", d.background);
    addCol(col, "grid", d.grid);
    addCol(col, "note", d.note);
    addCol(col, "noteSelected", d.noteSelected);
    addCol(col, "noteBorder", d.noteBorder);
    addCol(col, "noteSelectedBorder", d.noteSelectedBorder);
    addCol(col, "noteBackground", d.noteBackground);
    addCol(col, "keyWhite", d.keyWhite);
    addCol(col, "playHead", d.playHead);
    addCol(col, "trackBackground", d.trackBackground);
    addCol(col, "trackBody", d.trackBody);
    addCol(col, "trackBorder", d.trackBorder);
    addCol(col, "trackAudio", d.trackAudio);
    addCol(col, "trackNotes", d.trackNotes);
    addCol(col, "trackAutomation", d.trackAutomation);
    addCol(col, "nodeGraphBg", d.nodeGraphBg);
    addCol(col, "elementListBg", d.elementListBg);
    nlohmann::json result;
    result["Default"] = j;
    return result;
}

nlohmann::json Settings::getColorPresets() const {
    nlohmann::json merged = builtinDefaultPreset();
    if (defaults_.contains("colorPresets") && defaults_["colorPresets"].is_object())
        merged.update(defaults_["colorPresets"]);
    if (data_.contains("colorPresets") && data_["colorPresets"].is_object())
        merged.update(data_["colorPresets"]);
    return merged;
}

nlohmann::json Settings::getDefaultColorPresets() const {
    if (defaults_.contains("colorPresets") && defaults_["colorPresets"].is_object())
        return defaults_["colorPresets"];
    return nlohmann::json::object();
}

void Settings::setColorPreset(const std::string& name, const nlohmann::json& colors) {
    if (!data_.contains("colorPresets") || !data_["colorPresets"].is_object())
        data_["colorPresets"] = nlohmann::json::object();
    data_["colorPresets"][name] = nlohmann::json::object();
    data_["colorPresets"][name]["colors"] = colors;
    save();
}

void Settings::deleteColorPreset(const std::string& name) {
    auto merged = getColorPresets();
    if (merged.contains(name) && merged[name].value("immutable", false)) return;
    if (data_.contains("colorPresets") && data_["colorPresets"].is_object()) {
        data_["colorPresets"].erase(name);
        if (currentColorPreset() == name)
            setCurrentColorPreset("Default");
        save();
    }
}

std::string Settings::currentColorPreset() const {
    if (data_.contains("currentColorPreset"))
        return data_["currentColorPreset"].get<std::string>();
    if (defaults_.contains("currentColorPreset"))
        return defaults_["currentColorPreset"].get<std::string>();
    return "Default";
}

void Settings::setCurrentColorPreset(const std::string& name) {
    data_["currentColorPreset"] = name;
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
