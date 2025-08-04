#include "Project.h"
#include "Plugin.h"
#include "Region.h"
#include <memory>

#include <fstream>
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

    std::ifstream inFile(filepath);
    if (!inFile.is_open()) {
        std::cout<<"file didnt open"<<std::endl;

        return;
    }


    json j;
    inFile >> j;

    tempo = j.value("tempo", 120.0f);

    if (j.contains("tracks")) {
        for (auto& jt : j["tracks"]) {
            auto* track = new MixerTrack(this);
            track->name = jt.value("name", "Mixer Track");
            tracks.push_back(track);
        }
    }

    if (j.contains("instruments")) {
        int i = 0;
        for (auto& ji : j["instruments"]) {
            auto* inst = new Instrument(this, i++);
            inst->name = ji.value("name", "Instrument");
            instruments.push_back(inst);
        }
    }

    if (j.contains("regions")) {
        int i = 0;
        for (auto& ji : j["regions"]) {
            auto reg = std::make_shared<DAW::Region>();
            reg->name = ji.value("name", "MIDI Region");
            regions.push_back(reg);
        }
    }

    if(tracks.empty()) {
        tracks.push_back(new MixerTrack(this));
    }

}

void Project::save() {
    if (filepath.empty()) return;

    json j;
    j["tempo"] = tempo;

    j["tracks"] = json::array();
    for (auto e : tracks) {
        json je;
        je["name"] = e->name;
        j["tracks"].push_back(je);
    }

    j["instruments"] = json::array();
    for (auto e : instruments) {
        json je;
        je["name"] = e->name;
        j["instruments"].push_back(je);
    }

    j["regions"] = json::array();
    for (auto e : regions) {
        json je;
        je["name"] = e->name;
        j["regions"].push_back(je);
    }

    std::ofstream outFile(filepath);
    if (outFile.is_open()) {
        outFile << j.dump(2);
    }

}

void Project::createRegion() {
    regions.push_back(std::make_shared<DAW::Region>());
}

void Project::createInstrument() {
    instruments.push_back(new Instrument(this, instruments.size()));
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
    for(auto& inst : instruments) {
        auto& rack = inst->rack;
        for(auto& plug : rack.plugins) {
            plug->setup();
        }
    }
    for(auto& track : tracks) {
        auto& rack = track->rack;
        for(auto& plug : rack.plugins) {
            plug->setup();
        }
    }
}
