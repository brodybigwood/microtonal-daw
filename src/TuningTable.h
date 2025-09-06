#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct TuningTable {
    std::string filepath;
    std::string name;
    std::vector<float> notes;

    TuningTable(bool);
};
