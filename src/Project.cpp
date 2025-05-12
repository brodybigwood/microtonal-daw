#include "Project.h"

Project::Project(std::string filepath) {
        this->filepath = filepath;
        load();

        edit = new tracktion_engine::Edit { engine, tracktion_engine::Edit::EditRole::forEditing};
        setTime(0);




        juce::AudioDeviceManager::AudioDeviceSetup setup;
        



        setup.inputDeviceName = "";
        setup.outputChannels.setRange(0, 2, true);
        setup.inputChannels.clear();
        setup.sampleRate = 44100.0;
        setup.bufferSize = 256;


        
auto& types = deviceManager.getAvailableDeviceTypes();
for (auto* type : types)
{
    DBG("Trying: " + type->getTypeName());
    type->scanForDevices();
    auto names = type->getDeviceNames();
    for (const auto& name : names)
    {
        DBG("  Device: " + name);
        setup.outputDeviceName = name;
        auto err = deviceManager.setAudioDeviceSetup(setup, true);
        DBG("  Result: " + err);
        if(err.isEmpty()) {
            break;
        }
    }
}





        for(size_t i = 0; i<5; i++) {

            MixerTrack track(this);
            tracks.push_back(track);
        }  

        for(size_t i = 0; i<1; i++) {

            Instrument instrument(this);
            instruments.push_back(instrument);
        }   


        for(size_t i = 0; i<5; i++) {

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
        setTime(playHeadStart);
    }
}



void Project::setViewedElement(std::string type, int index) {
    viewedElement = new element(type, index);
}


void Project::setTime(double time) {
    tracktion::core::TimePosition transPos = tracktion::core::toPosition(tracktion::core::operator""_td(static_cast<long double>(time)));

    edit->getTransport().setPosition(transPos);
}