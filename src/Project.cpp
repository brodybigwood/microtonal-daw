#include "Project.h"

Project::Project(std::string filepath) {
        this->filepath = filepath;
        load();

        for(size_t i = 0; i<5; i++) {

            fract pos(0,1);
            Region midiRegion(pos, i);
            regions.push_back(midiRegion);
        }
        for(size_t i = 0; i<5; i++) {

            Instrument instrument;
            instruments.push_back(instrument);
        }   
        edit = new tracktion_engine::Edit { engine, tracktion_engine::Edit::EditRole::forEditing};

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
    if(playState == PlayState::Stop) {
        playHeadStart = playHeadPos;
        playState = PlayState::Play;
        edit->getTransport().play(false);
    }
}

void Project::stop() {
    if(playState == PlayState::Play) {
        playState = PlayState::Stop;
        edit->getTransport().stop(true, false);
        tracktion::core::TimePosition transPos = tracktion::core::toPosition(tracktion::core::operator""_td(static_cast<long double>(static_cast<double>(playHeadStart))));

        edit->getTransport().setPosition(transPos);
    }
}
