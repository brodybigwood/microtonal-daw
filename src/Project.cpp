#include "Project.h"
#include "Region.h"
#include <memory>
#include <filesystem>
#include <fstream>
#include "AudioManager.h"
#include "TrackManager.h"
#include "ElementManager.h"
#include "NodeProcessor.h"
#include "WindowHandler.h"
#include "ContextMenu.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

int Project::beatsToSamples(float beats) {
    float minutes = beats / tempo;
    return AudioManager::instance()->sampleRate * 60 * minutes;
}

void Project::process(float* input, float* output, int& bufferSize, int& numChannelsIn, int& numChannelsOut, int& sampleRate) {
    if (processor) processor->process(input, output, numChannelsIn, numChannelsOut, bufferSize, sampleRate);
};

void Project::render() {
    if (processor) processor->render();
}

void Project::renderPresent() {
    if (processor) processor->renderPresent();
}

Project::Project() {
    processor = new NodeProcessor(this);
    window = processor ? processor->getHostWindow() : nullptr;
    renderer = processor ? processor->getHostRenderer() : nullptr;

    um = new UndoManager(this);
}

void Project::handleWindowInput(SDL_Event& e) {
    if (processor) processor->handleWindowInput(e);
}

Project::~Project() {
    delete um;
    delete processor;
}

void Project::load(std::string path) {

    std::filesystem::path folder(path);
    std::filesystem::path file = folder / "save.json";

    std::ifstream inFile(file);
    if (!inFile.is_open()) {
        std::cout<<"file didnt open"<<std::endl;
        return;
    }

    loading.store(true);

    filepath = path;

    json j;
    inFile >> j;

    tempo = j.value("tempo", 120.0f);


    if (processor && j.contains("nodeManager")) processor->deSerialize(j["nodeManager"]);
    um->deSerialize(j["undoManager"], this);

    loading.store(false);
}

void Project::save(uint32_t triggerWindowID, SDL_Renderer* triggerRenderer) {

    auto save_l = [this] {
        std::filesystem::path folder(this->filepath);
        std::filesystem::create_directories(folder);
        std::filesystem::path file = folder / "save.json";
    
        json j;
        j["tempo"] = this->tempo;
        
        if (this->processor) j["nodeManager"] = this->processor->serialize();
        else j["nodeManager"] = json::object();
        j["undoManager"] = this->um->serialize();
        
        std::ofstream outFile(file);
        if (outFile.is_open()) {
            outFile << j.dump(2);
        }
    };

    const bool uiSaveRequested = (triggerWindowID != 0) || (triggerRenderer != nullptr);
    if (filepath.empty() && !uiSaveRequested) {
        std::cout << "[save] filepath is empty; use Ctrl+S in a node window to choose a save path." << std::endl;
        return;
    }

    if (filepath.empty()) {
        auto ctxMenu = ContextMenu::get();

        ctxMenu->active = true;
        SDL_Window* dialogWindow = this->window;
        if (triggerWindowID != 0) {
            if (SDL_Window* eventWindow = SDL_GetWindowFromID(triggerWindowID)) {
                dialogWindow = eventWindow;
            }
        }
        SDL_Renderer* dialogRenderer = triggerRenderer ? triggerRenderer : this->renderer;
        ctxMenu->window_id = dialogWindow ? SDL_GetWindowID(dialogWindow) : 0;
        ctxMenu->renderer = dialogRenderer;
        if (dialogWindow) SDL_StartTextInput(dialogWindow);
        int w = 0, h = 0;
        if (dialogWindow) SDL_GetWindowSize(dialogWindow, &w, &h);
        ctxMenu->locX = w * 0.5f;
        ctxMenu->locY = h * 0.5f;


        ctxMenu->dynamicTick = getTextInputTicker([this, save_l] (std::string text) {
            this->filepath = text;
            save_l();
        });
    
    } else save_l();
}

void Project::createNote(int nodeID, fract start, fract length, float pitch, int regionID, std::vector<int> managerPath,
                         std::vector<std::pair<int, int>> pitchIntegerPairs) {
    auto pa = new CreateNoteAction(this, std::move(managerPath), nodeID, regionID, start, length, pitch,
                                   std::move(pitchIntegerPairs));
    um->newAction(pa);
}

void Project::deleteNote(int nodeID, int regionID, int noteID, std::vector<int> managerPath) {
    auto pa = new DeleteNoteAction(this, std::move(managerPath), nodeID, regionID, noteID);
    um->newAction(pa);
}

void Project::togglePlaying() {
    if (!isPlaying.load()) {
        const double epsilon = static_cast<double>(AudioManager::instance()->latency) / AudioManager::instance()->sampleRate;
        timeSeconds.store(playHeadStart - epsilon);
        isPlaying.store(true);
    } else {
        isPlaying.store(false);
        timeSeconds.store(playHeadStart);
    }
}


void Project::stop() {
    if(isPlaying.load()) {
        isPlaying.store(false);
        timeSeconds.store(playHeadStart);
    }
}

void Project::tick() {

}

void Project::setup() {
}
