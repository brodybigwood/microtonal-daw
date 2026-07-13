#include "Note.h"
#include <utility>
#include <vector>
#include <iostream>
#include <cstdint>
#include "UndoManager.h"
#include "Window.h"
#include "AutomationCurve.h"

#ifndef PROJECT_H
#define PROJECT_H

namespace DAW {

    class Region;
}

class TrackManager;
class ElementManager;
class NodeProcessor;
class Project : public Window {
    public:
    Project();
        ~Project();

        void render();
        void renderPresent();

        bool processing = false;

        void handleWindowInput(SDL_Event&) override;

        NodeProcessor* processor;

        int sampleTime = 0;

        std::string filepath = "";
        std::string startupCWD = "";  // captured before VST plugins can chdir()

        // Tempo automation: beat -> BPM curve (GUI and DSP copies)
        AutomationCurve tempoCurve;
        AutomationCurve dspTempoCurve;
        AutomationCurve& activeTempoCurve();
        double deltaBeats = 0.0;

        // Per-thread transport state (same model as tempoCurve)
        double beatPosition = 0.0;
        double timeSeconds = 0.0;
        double effectiveTime = 0.0;
        double effectiveBeatPosition = 0.0;
        double dspBeatPosition = 0.0;
        double dspTimeSeconds = 0.0;
        uint64_t dspTransportGeneration = 0;
        int dspNextVoiceId = 1;

        double& activeBeatPosition();
        double& activeTimeSeconds();

        void advanceBeatPosition(double dtSec);
        void syncTransportToAudio(bool playing, double beat, double seconds);
        int allocateDspVoiceId();

        void load(std::string path = "");

        void save(uint32_t triggerWindowID = 0, SDL_Renderer* triggerRenderer = nullptr);

        void createNote(int, std::vector<std::pair<int,int>>, std::vector<std::pair<int,int>>, int, std::vector<int> managerPath = {},
                        std::vector<std::pair<int, int>> pitchVector = {});

        void deleteNote(int nodeID, int regionID, int noteID, std::vector<int> managerPath = {});

        float mouseX;
        float mouseY;

        void togglePlaying();

        void stop();

        uint16_t id_reg = 0;

        void tick();

        void setup();

        double playHeadBeat = 0.0;
        double playHeadStart = 0.0;

        bool isPlaying = false;
        bool dspIsPlaying = false;
        bool loading = false;
        bool dspLoading = false;

        void process(float* input, float* output, int& bufferSize, int& numChannelsIn, int& numChannelsOut, int& sampleRate);

        UndoManager* um;
        void undo() { um->undo(); }
        void redo() { um->redo(); }

    private:

};

#endif
