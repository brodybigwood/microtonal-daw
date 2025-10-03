#include "Project.h"
#include "Region.h"
#include <memory>
#include <filesystem>
#include <fstream>
#include "AudioManager.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

Project::Project() {
    em.project = this;
}

Project::~Project() {
save();
}

Project* Project::instance() {
    static Project proj;
    return &proj;
}

void Project::load(std::string path) {

    if (!path.empty()) {
        filepath = path;
    }

    std::filesystem::path folder(filepath);
    std::filesystem::create_directories(folder);
    std::filesystem::path file = folder / "save.json";

    std::ifstream inFile(file);
    if (!inFile.is_open()) {
        std::cout<<"file didnt open"<<std::endl;
        return;
    }



    json j;
    inFile >> j;

    tempo = j.value("tempo", 120.0f);

    constexpr uint16_t NO_ID = std::numeric_limits<uint16_t>::max();

    if (j.contains("regions")) {
        int i = 0;
        for (auto& ji : j["regions"]) {
            id_reg = ji.value("id", NO_ID);
            auto reg = std::make_shared<DAW::Region>();
            reg->fromJSON(ji);
            regions.push_back(reg);
        }
    }

    int max_id = 0;
    for (auto region : regions) {
        if (region->id > max_id) max_id = region->id;
    }
    id_reg = max_id + 1;

}

void Project::save() {
    if (filepath.empty()) return;

    std::filesystem::path folder(filepath);
    std::filesystem::create_directories(folder);
    std::filesystem::path file = folder / "save.json";

    json j;
    j["tempo"] = tempo;

    j["regions"] = json::array();
    for (auto e : regions) {
        json je = e->toJSON();
        j["regions"].push_back(je);
    }

    std::ofstream outFile(file);
    if (outFile.is_open()) {
        outFile << j.dump(2);
    }

}

void Project::createRegion() {
    regions.push_back(std::make_shared<DAW::Region>());
}

void Project::togglePlaying() {
    if (!isPlaying) {
        const double epsilon = static_cast<double>(AudioManager::instance()->latency) / AudioManager::instance()->sampleRate;
        timeSeconds = playHeadStart - epsilon;
        isPlaying = true;
    } else {
        isPlaying = false;
        timeSeconds = playHeadStart;
    }
}


void Project::stop() {
    if(isPlaying) {
        isPlaying = false;
        timeSeconds = playHeadStart;
    }
}


void Project::tick() {

}

void Project::setup() {
}
