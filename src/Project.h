#include "Note.h"
#include "RecordedTake.h"
#include <atomic>
#include <utility>
#include <vector>
#include <iostream>
#include <cstdint>
#include <mutex>
#include <thread>
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
        /** Reset to an empty project (no filepath, fresh graphs and history). */
        void newProject();

        // Background project load with the animated loading screen. The GUI
        // thread renders while a worker rebuilds the graphs.
        void startAsyncLoad(const std::string& path);
        bool asyncLoadRunning() const { return loadInProgress.load(); }
        bool asyncLoadFinished() const { return loadFinished.load(); }
        void completeAsyncLoad();
        void waitForLoadThread();
        std::atomic<uint64_t> loadStartMs{0};

        void save(uint32_t triggerWindowID = 0, SDL_Renderer* triggerRenderer = nullptr);

        /** Block until the background save worker has finished. Call before process exit. */
        void waitForSave();

        std::thread saveThread;
        std::mutex saveThreadMutex;
        std::thread loadThread;
        std::mutex loadThreadMutex;
        std::atomic<bool> loadInProgress{false};
        std::atomic<bool> loadFinished{false};

        // Finished recording takes, DSP thread -> GUI thread.
        void pushRecordedTake(RecordedTake&& take);
        void drainRecordedTakes();
        std::mutex takeQueueMutex;
        std::vector<RecordedTake> pendingTakes;
        /** Resolved project folder, or empty when the project has never been saved. */
        std::string projectFolder() const;

        void createNote(int, std::vector<std::pair<int,int>>, std::vector<std::pair<int,int>>, int, std::vector<int> managerPath = {},
                        std::vector<std::pair<int, int>> pitchVector = {});

        void deleteNote(int nodeID, int regionID, int noteID, std::vector<int> managerPath = {});

        float mouseX;
        float mouseY;

        void togglePlaying();

        void stop();

        /** Move the playhead to an absolute beat, converting through the tempo curve. */
        void seekBeat(double beat);

        uint16_t id_reg = 0;

        void tick();

        void setup();

        double playHeadBeat = 0.0;
        double playHeadStart = 0.0;

        bool isPlaying = false;
        bool dspIsPlaying = false;

        // Global record arm (FL style): armed tracks record their inputs while playing.
        bool isRecording = false;
        bool dspRecording = false;
        void setRecording(bool on);
        bool loading = false;
        bool dspLoading = false;
        /** Set by any edit/undo, cleared by save/load; drives the load prompt. */
        std::atomic<bool> dirty{false};

        // Progress for the startup loading screen (written by Project::load).
        std::atomic<float> loadProgressValue{0.f};
        std::atomic<float> loadGraphBase{0.30f};
        std::atomic<float> loadGraphSpan{0.40f};
        std::atomic<const char*> loadStatusText{nullptr};

        void process(float* input, float* output, int& bufferSize, int& numChannelsIn, int& numChannelsOut, int& sampleRate);

        UndoManager* um;
        void undo() { um->undo(); }
        void redo() { um->redo(); }

    private:

};

#endif
