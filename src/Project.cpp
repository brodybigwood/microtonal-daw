#include "Project.h"
#include "Plugin.h"

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
    setTime(0);

    for(size_t i = 0; i<1; i++) {

        tracks.push_back(new MixerTrack(this));
    }

    for(size_t i = 0; i<1; i++) {

        instruments.push_back(new Instrument(this));
    }


    for(size_t i = 0; i<1; i++) {

        fract pos(0,1);
        Region* reg = new Region(pos, i);
        reg->outputs.push_back(0);
        regions.push_back(reg);
    }
}

void Project::save() {
    if(filepath != "") {

    }


}

void Project::createRegion(fract x, int y) {
    regions.push_back(new Region(x, y));
}

void Project::play() {
    if(!isPlaying) {
        isPlaying = true;
    }
}

void Project::stop() {
    if(isPlaying) {
        isPlaying = false;
        setTime(playHeadStart);
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


void Project::setTime(double time) {
    timeSeconds = time;
}

