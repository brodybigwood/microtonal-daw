#include "Project.h"

Project::Project(std::string filepath) {
        this->filepath = filepath;
        load();
        setTime(0);

        for(size_t i = 0; i<1; i++) {

            MixerTrack track(this);
            tracks.push_back(track);
        }  

        for(size_t i = 0; i<1; i++) {

            instruments.push_back(new Instrument(this));
        }   


        for(size_t i = 0; i<1; i++) {

            fract pos(0,1);
            Region midiRegion(pos, i);
            regions.push_back(midiRegion);
        }
}

Project::~Project() {
save();
}

void Project::load() {
    if(filepath != "") {

    }
}

void Project::save() {
    if(filepath != "") {

    }
}

void Project::createRegion(fract x, int y) {
    Region region(x, y);
    regions.push_back(region);
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



void Project::setViewedElement(std::string type, int index) {
    viewedElement = new element{type, index};
}


void Project::setTime(double time) {
    timeSeconds = time;
}

