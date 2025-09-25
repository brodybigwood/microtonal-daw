#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct ScaleNote{
    float midiNum;
    std::string identifier;
};

struct TuningTable {
    std::string filepath;
    std::string name;
    std::vector<ScaleNote> notes;
    std::vector<float> getNoteNums();

    int id;

    TuningTable(bool=true);
};
