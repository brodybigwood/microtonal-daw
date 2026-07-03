#include "Project.h"
#include "Region.h"
#include <memory>
#include <filesystem>
#include <fstream>
#include <stdexcept>
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
    startupCWD = std::filesystem::current_path().string();
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
    if (folder.is_relative())
        folder = std::filesystem::path(startupCWD) / folder;
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

    // Restore undo tree first (walks to version)
    um->deSerialize(j["undoManager"], this);

    // Restore NM state from current action's snapshot (falls back to legacy top-level)
    if (processor) {
        if (!um->current->savedMainManager.is_null())
            processor->deSerialize(um->current->savedMainManager);
        else if (j.contains("nodeManager"))
            processor->deSerialize(j["nodeManager"]);
    }

    loading.store(false);
}

void Project::save(uint32_t triggerWindowID, SDL_Renderer* triggerRenderer) {

    auto save_l = [this] {
        std::filesystem::path folder(this->filepath);
        if (folder.is_relative())
            folder = std::filesystem::path(this->startupCWD) / folder;
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        if (ec) {
            std::cerr << "[save] create_directories failed: " << ec.message() << " path=" << folder << std::endl;
            return;
        }
        std::filesystem::path file = folder / "save.json";

        json j;
        j["tempo"] = this->tempo;

        // Snapshot NM state onto current action before serializing the tree
        if (this->processor && this->um && this->um->current)
            this->um->current->savedMainManager = this->processor->serialize();

        j["undoManager"] = this->um->serialize();

        std::ofstream outFile(file);
        if (outFile.is_open()) {
            outFile << j.dump(2);
            outFile.flush();
            if (outFile.fail()) {
                std::cerr << "[save] WRITE FAILED for " << file << std::endl;
            } else {
                std::cout << "[save] wrote " << file << std::endl;
            }
        } else {
            std::cerr << "[save] failed to open " << file << std::endl;
        }
    };

    const bool uiSaveRequested = (triggerWindowID != 0) || (triggerRenderer != nullptr);
    if (filepath.empty() && !uiSaveRequested) {
        std::cout << "[save] filepath is empty; use Ctrl+S in a node window to choose a save path." << std::endl;
        return;
    }

    if (filepath.empty()) {
        auto ctxMenu = ContextMenu::get();
        if (this->window) {
            SDL_RaiseWindow(this->window);
            SDL_SetWindowKeyboardGrab(this->window, true);
        }
        ctxMenu->activate(SDL_GetRenderer(this->window), SDL_GetWindowID(this->window));
        if (this->window) SDL_StartTextInput(this->window);
        int w = 0, h = 0;
        if (this->window) SDL_GetWindowSize(this->window, &w, &h);
        ctxMenu->locX = w * 0.5f;
        ctxMenu->locY = h * 0.5f;


        ctxMenu->dynamicTick = getTextInputTicker(
            [this, save_l] (std::string text) {
                std::filesystem::path p(text);
                if (p.is_relative())
                    p = std::filesystem::path(this->startupCWD) / p;
                this->filepath = p.string();
                save_l();
            },
            [this] () {
                if (this->window) SDL_SetWindowKeyboardGrab(this->window, false);
            }
        );
    
    } else save_l();
}

void Project::createNote(int nodeID, std::vector<std::pair<int,int>> rhythmPairs, float durationSeconds, int regionID, std::vector<int> managerPath,
                         std::vector<std::pair<int, int>> pitchIntegerPairs) {
    auto pa = new CreateNoteAction(this, std::move(managerPath), nodeID, regionID, std::move(rhythmPairs), durationSeconds,
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
