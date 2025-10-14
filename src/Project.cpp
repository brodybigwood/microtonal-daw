#include "Project.h"
#include "Region.h"
#include <memory>
#include <filesystem>
#include <fstream>
#include "AudioManager.h"
#include "NodeManager.h"
#include "TrackList.h"
#include "ElementManager.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

void Project::process(float* input, float* output, int& bufferSize, int& numChannelsIn, int& numChannelsOut, int& sampleRate) {
    TrackList* tl = TrackList::get();

    tl->process(input, bufferSize);

    NodeManager* nm = NodeManager::get();
    nm->process(output, bufferSize, numChannelsIn, sampleRate);
};

Project::Project() {}

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

    ElementManager::get()->fromJSON(j["elementManager"]);

    TrackList::get()->fromJSON(j["tracks"]);
}

void Project::save() {
    if (filepath.empty()) return;

    std::filesystem::path folder(filepath);
    std::filesystem::create_directories(folder);
    std::filesystem::path file = folder / "save.json";

    json j;
    j["tempo"] = tempo;

    j["tracks"] = TrackList::get()->toJSON();

    j["elementManager"] = ElementManager::get()->toJSON();

    std::ofstream outFile(file);
    if (outFile.is_open()) {
        outFile << j.dump(2);
    }

}

void Project::createRegion() {
    auto em = ElementManager::get();
    em->newRegion();
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
