#pragma once

#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <SDL3/SDL.h>

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

    int byID(std::string);

    int id;

    TuningTable(SDL_Window*, bool=true);

    json serialize();
    void deSerialize(json);
};
