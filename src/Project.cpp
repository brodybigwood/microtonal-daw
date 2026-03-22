#include "Project.h"
#include "Region.h"
#include <memory>
#include <filesystem>
#include <fstream>
#include "AudioManager.h"
#include "TrackManager.h"
#include "ElementManager.h"
#include "ScaleManager.h"
#include "NodeManager.h"
#include "NodeEditor.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

int Project::beatsToSamples(float beats) {
    float minutes = beats / tempo;
    return AudioManager::instance()->sampleRate * 60 * minutes;
}

void Project::process(float* input, float* output, int& bufferSize, int& numChannelsIn, int& numChannelsOut, int& sampleRate) {

    nm->process(output, bufferSize, numChannelsOut, sampleRate);
};

void Project::render() {
    ne->tick();
}

void Project::renderPresent() {
    ne->renderPresent();
}

Project::Project() {
    nm = new NodeManager(this);
    ne = nm->ne;

    um = new UndoManager(this);
}

Project::~Project() {
    delete um;
    delete nm;
    delete ne;
}

void Project::load(std::string path) {

    std::filesystem::path folder(path);
    std::filesystem::path file = folder / "save.json";

    std::ifstream inFile(file);
    if (!inFile.is_open()) {
        std::cout<<"file didnt open"<<std::endl;
        return;
    }

    filepath = path;

    json j;
    inFile >> j;

    tempo = j.value("tempo", 120.0f);


    nm->deSerialize(j["nodeManager"]);
    um->deSerialize(j["undoManager"], this);
}

void Project::save() {

    auto save_l = [this] {
        std::filesystem::path folder(this->filepath);
        std::filesystem::create_directories(folder);
        std::filesystem::path file = folder / "save.json";
    
        json j;
        j["tempo"] = this->tempo;
        
        j["nodeManager"] = this->nm->serialize();
        j["undoManager"] = this->um->serialize();
        
        std::ofstream outFile(file);
        if (outFile.is_open()) {
            outFile << j.dump(2);
        }
    };

    if (filepath.empty()) {
        auto ctxMenu = ContextMenu::get();

        ctxMenu->active = true;
        auto window = ne->window;
        ctxMenu->window_id = SDL_GetWindowID(window);
        ctxMenu->renderer = ne->renderer;
        SDL_StartTextInput(window);
    
        ctxMenu->locX = ne->windowWidth / 2;
        ctxMenu->locY = ne->windowHeight / 2;


        ctxMenu->dynamicTick = getTextInputTicker([this, save_l] (std::string text) {
            this->filepath = text;
            save_l();
        });
    
    } else save_l();
}

void Project::createNote(int nodeID, fract start, fract length, float pitch, TuningTable* t, int regionID) {
    auto pa = new CreateNoteAction(this, nodeID, regionID, start, length, pitch, t);
    um->newAction(pa);
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
