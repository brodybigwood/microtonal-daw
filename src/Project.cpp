#include "Project.h"
#include "Region.h"
#include <memory>
#include <filesystem>
#include <fstream>
#include "AudioManager.h"
#include "NodeManager.h"
#include "TrackList.h"
#include "ElementManager.h"
#include "ScaleManager.h"
#include "BusManager.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

int Project::beatsToSamples(float beats) {
    float minutes = beats / tempo;
    return AudioManager::instance()->sampleRate * 60 * minutes;
}

void Project::process(float* input, float* output, int& bufferSize, int& numChannelsIn, int& numChannelsOut, int& sampleRate) {

    BusManager* bm = BusManager::get();
    bm->process(bufferSize);

    auto* em = ElementManager::get();
    em->process(bufferSize);    

    TrackList* tl = TrackList::get();
    tl->process(input, bufferSize);

    NodeManager* nm = NodeManager::get();
    nm->process(output, bufferSize, numChannelsOut, sampleRate);
};

Project::Project() {}

Project::~Project() {
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


    ScaleManager::instance()->deSerialize(j["scaleManager"]);
    ElementManager::get()->fromJSON(j["elementManager"]);
    NodeManager::get()->deSerialize(j["nodeManager"]);

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

    j["scaleManager"] = ScaleManager::instance()->serialize();
    j["elementManager"] = ElementManager::get()->toJSON();
    j["nodeManager"] = NodeManager::get()->serialize();

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
