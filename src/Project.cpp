#include "Project.h"
#include "Region.h"
#include <memory>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <limits>
#include "AudioManager.h"
#include "TrackManager.h"
#include "ElementManager.h"
#include "NodeProcessor.h"
#include "NodeManager.h"
#include "WindowHandler.h"
#include "ContextMenu.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;

void Project::process(float* input, float* output, int& bufferSize, int& numChannelsIn, int& numChannelsOut, int& sampleRate) {
    if (processor) processor->process(input, output, numChannelsIn, numChannelsOut, bufferSize, sampleRate);
};

void Project::render() {
    if (processor) processor->render();
}

void Project::renderPresent() {
    if (processor) processor->renderPresent();
}

Project::Project() : tempoCurve(this, nullptr), dspTempoCurve(this, nullptr) {
    tempoCurve.valueRangeMin = 0.001f;
    tempoCurve.valueRangeMax = 999.f;
    tempoCurve.addPoint({}, 120.f, CurveShape::Single);
    dspTempoCurve.valueRangeMin = 0.001f;
    dspTempoCurve.valueRangeMax = 999.f;
    dspTempoCurve.addPoint({}, 120.f, CurveShape::Single);

    startupCWD = std::filesystem::current_path().string();
    processor = new NodeProcessor(this);
    window = processor ? processor->getHostWindow() : nullptr;
    renderer = processor ? processor->getHostRenderer() : nullptr;

    um = new UndoManager(this);
}

AutomationCurve& Project::activeTempoCurve() {
    if (processor && NodeProcessor::getActiveRoot() == processor->dspGraph)
        return dspTempoCurve;
    return tempoCurve;
}

double& Project::activeBeatPosition() {
    return (processor && NodeProcessor::getActiveRoot() == processor->dspGraph)
        ? dspBeatPosition : beatPosition;
}

double& Project::activeTimeSeconds() {
    return (processor && NodeProcessor::getActiveRoot() == processor->dspGraph)
        ? dspTimeSeconds : timeSeconds;
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

    loading = true;
    if (um) {
        um->enqueueAudioSync([this]() {
            dspLoading = true;
            dspIsPlaying = false;
        });
    }

    filepath = path;

    json j;
    inFile >> j;

    // Restore tempo curve from JSON (or fall back to legacy float, or default)
    if (j.contains("tempoCurve") && j["tempoCurve"].is_object()) {
        auto& tcj = j["tempoCurve"];
        tempoCurve.valueRangeMin = tcj.value("valueRangeMin", 0.f);
        tempoCurve.valueRangeMax = tcj.value("valueRangeMax", 999.f);
        tempoCurve.points.clear();
        if (tcj.contains("points") && tcj["points"].is_array()) {
            for (const auto& ptj : tcj["points"]) {
                CurvePoint pt;
                pt.v = ptj.value("v", 120.f);
                pt.shape.type = static_cast<CurveShape::Type>(ptj.value("shape", static_cast<int>(CurveShape::Single)));
                pt.shape.param = ptj.value("shapeParam", 0.f);
                if (ptj.contains("timeVec") && ptj["timeVec"].is_array()) {
                    for (const auto& pr : ptj["timeVec"])
                        if (pr.is_array() && pr.size() >= 2)
                            pt.timeVec.push_back({pr[0].get<int>(), pr[1].get<int>()});
                }
                tempoCurve.points.push_back(std::move(pt));
            }
        }
        if (tempoCurve.points.empty())
            tempoCurve.addPoint({}, 120.f, CurveShape::Single);
    } else {
        // Legacy: flat tempo from float field
        float legacyTempo = j.value("tempo", 120.0f);
        tempoCurve.points.clear();
        tempoCurve.addPoint({}, legacyTempo, CurveShape::Single);
    }

    // Restore undo tree first (walks to version)
    um->deSerialize(j["undoManager"], this);

    // Restore NM state from current action's snapshot (falls back to legacy top-level)
    if (processor) {
        if (!um->current->savedMainManager.is_null())
            processor->deSerialize(um->current->savedMainManager);
        else if (j.contains("nodeManager"))
            processor->deSerialize(j["nodeManager"]);
    }

    loading = false;
    const float tempoMin = tempoCurve.valueRangeMin;
    const float tempoMax = tempoCurve.valueRangeMax;
    const auto tempoPoints = tempoCurve.points;
    if (um) {
        um->enqueueAudioSync([this, tempoMin, tempoMax, tempoPoints]() {
            dspTempoCurve.valueRangeMin = tempoMin;
            dspTempoCurve.valueRangeMax = tempoMax;
            dspTempoCurve.points = tempoPoints;
            dspLoading = false;
        });
    }
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
        // Serialize tempo curve
        json tcj;
        tcj["valueRangeMin"] = tempoCurve.valueRangeMin;
        tcj["valueRangeMax"] = tempoCurve.valueRangeMax;
        tcj["points"] = json::array();
        for (const auto& pt : tempoCurve.points) {
            json jpt;
            jpt["timeVec"] = json::array();
            for (const auto& pr : pt.timeVec)
                jpt["timeVec"].push_back(json::array({pr.first, pr.second}));
            jpt["v"] = pt.v;
            jpt["shape"] = static_cast<int>(pt.shape.type);
            jpt["shapeParam"] = pt.shape.param;
            tcj["points"].push_back(jpt);
        }
        j["tempoCurve"] = std::move(tcj);

        // Snapshot NM state onto current action before serializing the tree
        if (this->processor && this->um && this->um->current)
            this->um->current->savedMainManager = this->processor->serialize();

        j["undoManager"] = this->um->serialize();

        std::ofstream outFile(file);
        if (outFile.is_open()) {
            outFile << j.dump();
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

void Project::createNote(int nodeID, std::vector<std::pair<int,int>> startPairs, std::vector<std::pair<int,int>> endPairs, int regionID, std::vector<int> managerPath,
                         std::vector<std::pair<int, int>> pitchVector) {
    auto pa = new CreateNoteAction(this, std::move(managerPath), nodeID, regionID, std::move(startPairs), std::move(endPairs),
                                   std::move(pitchVector));
    um->newAction(pa);
}

void Project::deleteNote(int nodeID, int regionID, int noteID, std::vector<int> managerPath) {
    auto pa = new DeleteNoteAction(this, std::move(managerPath), nodeID, regionID, noteID);
    um->newAction(pa);
}

void Project::advanceBeatPosition(double dtSec) {
    if (dtSec <= 0.0) {
        deltaBeats = 0.0;
        return;
    }

    double b = activeBeatPosition();
    auto& tc = activeTempoCurve();

    // Binary search db such that secondsForBeats(b, b+db) == dtSec
    double lo = 0.0, hi = dtSec * 999.0 / 60.0;
    for (int iter = 0; iter < 20; iter++) {
        double mid = (lo + hi) * 0.5;
        float secs = tc.secondsForBeats(
            static_cast<float>(b), static_cast<float>(b + mid));
        if (static_cast<double>(secs) < dtSec) lo = mid;
        else hi = mid;
    }
    deltaBeats = hi;
}

void Project::syncTransportToAudio(bool playing, double beat, double seconds) {
    if (!um) return;
    um->enqueueAudioSync([this, playing, beat, seconds]() {
        dspIsPlaying = playing;
        dspBeatPosition = beat;
        dspTimeSeconds = seconds;
        ++dspTransportGeneration;
    });
}

int Project::allocateDspVoiceId() {
    if (dspNextVoiceId <= 0) dspNextVoiceId = 1;
    const int id = dspNextVoiceId;
    dspNextVoiceId = (dspNextVoiceId == std::numeric_limits<int>::max())
        ? 1 : dspNextVoiceId + 1;
    return id;
}

void Project::togglePlaying() {
    if (!isPlaying) {
        const double epsilon = static_cast<double>(AudioManager::instance()->latency) / AudioManager::instance()->sampleRate;
        const double exactTime = static_cast<double>(tempoCurve.secondsForBeats(0.f, static_cast<float>(playHeadBeat)));
        const double pulledBackTime = exactTime - epsilon;

        // Binary search for the beat at the pulled-back time
        const double maxBeatsPerSec = 999.0 / 60.0;
        double lo = std::min(0.0, playHeadBeat);
        if (pulledBackTime < 0.0)
            lo = std::min(lo, pulledBackTime * maxBeatsPerSec);
        double hi = std::max(0.0, playHeadBeat);
        for (int iter = 0; iter < 20; iter++) {
            double mid = (lo + hi) * 0.5;
            float t = tempoCurve.secondsForBeats(0.f, static_cast<float>(mid));
            if (static_cast<double>(t) < pulledBackTime) lo = mid;
            else hi = mid;
        }
        beatPosition = hi;
        timeSeconds = pulledBackTime;
        isPlaying = true;
        syncTransportToAudio(true, hi, pulledBackTime);
    } else {
        isPlaying = false;
        beatPosition = playHeadBeat;
        double sec = static_cast<double>(tempoCurve.secondsForBeats(0.f, static_cast<float>(playHeadBeat)));
        timeSeconds = sec;
        syncTransportToAudio(false, playHeadBeat, sec);
    }
}


void Project::stop() {
    if(isPlaying) {
        isPlaying = false;
        beatPosition = playHeadBeat;
        double sec = static_cast<double>(tempoCurve.secondsForBeats(0.f, static_cast<float>(playHeadBeat)));
        timeSeconds = sec;
        syncTransportToAudio(false, playHeadBeat, sec);
    }
}

void Project::tick() {

}

void Project::setup() {
}
