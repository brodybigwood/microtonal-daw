#include "Project.h"
#include "Plugin.h"
#include "Region.h"
#include <memory>
#include <filesystem>

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

    std::filesystem::path folder(filepath);
    std::filesystem::create_directories(folder);
    std::filesystem::path file = folder / "save.json";

    std::ifstream inFile(file);
    if (!inFile.is_open()) {
        std::cout<<"file didnt open"<<std::endl;

        if(tracks.empty()) { //temporary
            tracks.push_back(new MixerTrack(this));
        }
        return;
    }



    json j;
    inFile >> j;

    tempo = j.value("tempo", 120.0f);

    constexpr uint16_t NO_ID = std::numeric_limits<uint16_t>::max();

    if (j.contains("tracks")) {
        for (auto& jt : j["tracks"]) {
            id_track = jt.value("id", NO_ID);
            auto* track = new MixerTrack(this);
            track->name = jt.value("name", "Mixer Track");
            tracks.push_back(track);
            track->fromJSON(jt);
        }
    }

    uint16_t max_id = 0;
    for (auto track : tracks) {
        if (track->id > max_id) max_id = track->id;
    }
    id_track = max_id + 1;


    if (j.contains("instruments")) {
        int i = 0;
        for (auto& ji : j["instruments"]) {
            id_inst = ji.value("id", NO_ID);
            auto* inst = new Instrument(this, i++);
            inst->name = ji.value("name", "Instrument");
            instruments.push_back(inst);
            inst->fromJSON(ji);
        }
    }

    max_id = 0;
    for (auto instrument : instruments) {
        if (instrument->id > max_id) max_id = instrument->id;
    }
    id_inst = max_id + 1;

    if (j.contains("regions")) {
        int i = 0;
        for (auto& ji : j["regions"]) {
            id_reg = ji.value("id", NO_ID);
            auto reg = std::make_shared<DAW::Region>();
            reg->name = ji.value("name", "MIDI Region");
            regions.push_back(reg);
        }
    }

    max_id = 0;
    for (auto region : regions) {
        if (region->id > max_id) max_id = region->id;
    }
    id_reg = max_id + 1;

    if(tracks.empty()) {
        tracks.push_back(new MixerTrack(this));
    }

}

void Project::save() {
    if (filepath.empty()) return;

    std::filesystem::path folder(filepath);
    std::filesystem::create_directories(folder);
    std::filesystem::path file = folder / "save.json";

    json j;
    j["tempo"] = tempo;

    j["tracks"] = json::array();
    for (auto e : tracks) {
        json je = e->toJSON();
        j["tracks"].push_back(je);
    }

    j["instruments"] = json::array();
    for (auto e : instruments) {
        json je = e->toJSON();
        j["instruments"].push_back(je);
    }

    j["regions"] = json::array();
    for (auto e : regions) {
        json je;
        je["name"] = e->name;
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
