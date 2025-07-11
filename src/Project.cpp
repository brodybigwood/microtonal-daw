#include "Project.h"
#include "Plugin.h"
#include "Region.h"
#include <memory>

Project::Project() {
    em.project = this;
    em.start();
}

Project::~Project() {
save();
}

Project* Project::instance() {
    static Project proj;
    return &proj;
}

void Project::load(std::string filepath) {
    if(filepath != "") {

    }

    this->filepath = filepath;
    timeSeconds = 0;

    for(size_t i = 0; i<1; i++) {

        tracks.push_back(new MixerTrack(this));
    }

    for(size_t i = 0; i<1; i++) {

        instruments.push_back(new Instrument(this, i));
    }
}

void Project::save() {
    if(filepath != "") {

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
    em.getEvents();
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
