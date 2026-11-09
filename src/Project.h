#include "Note.h"
#include "RhythmLattice.h"
#include "RecordedTake.h"
#include <atomic>
#include <utility>
#include <vector>
#include <iostream>
#include <cstdint>
#include <functional>
#include <mutex>
#include <thread>
#ifndef __EMSCRIPTEN__
#include <pthread.h>
#endif
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

// Central rhythm/harmony lines shared by every piano roll. Deliberately separate
// from the SongRoll/Arranger grid, which positions are drawn against.
struct PianoRollSettings {
    int tuningMode = 0; // 0 = harmonic, 1 = EDO
    int tuningAnchorHarmonic = 1;
    std::vector<std::pair<int, int>> tuningHarmonicAnchorVector;
    int tuningEdoSubdivisionSteps = 12;
    std::vector<std::pair<int, int>> tuningEdoLowerVector;
    std::vector<std::pair<int, int>> tuningEdoUpperVector;
    int rhythmEdoSubdivisionSteps = 1;
    std::vector<std::pair<int, int>> rhythmEdoLowerVector;
    std::vector<std::pair<int, int>> rhythmEdoUpperVector;
    std::vector<RhythmLatticeSpan> transportLines;
    std::vector<RhythmLatticeSpan> gridSpans;
};

// Worker thread with a large stack. Project load/save recurse over the undo-tree
// JSON, whose depth equals the undo history length (thousands of actions). The
// default 8 MB thread stack overflows in nlohmann's recursive copy/dump well
// before the tree is unreasonably large.
class BigStackThread {
public:
    BigStackThread() = default;
    ~BigStackThread();
    BigStackThread(const BigStackThread&) = delete;
    BigStackThread& operator=(const BigStackThread&) = delete;

    void start(std::function<void()> fn);
    void join();
    bool joinable() const { return active; }

private:
    bool active = false;
#ifdef __EMSCRIPTEN__
    std::thread thread;
#else
    pthread_t handle{};
#endif
};

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

        // Central piano-roll rhythm/harmony lines, shared by all piano rolls.
        PianoRollSettings pianoRoll;
        json pianoRollToJson() const;
        void pianoRollFromJson(const json& j);

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

        BigStackThread saveThread;
        std::mutex saveThreadMutex;
        BigStackThread loadThread;
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
        /** Folder that holds out-of-line VST state blobs: the project folder,
            or a default folder when the project has never been saved. */
        std::string vstStateFolder() const;
        /** Replace inline compState/ctrlState base64 blobs with compStateFile/
            ctrlStateFile paths. VST state is never kept inside JSON. */
        json externalizeVstState(json state);
        /** Load one state blob, from the sidecar file or (legacy) inline base64. */
        bool readVstStateBlob(const json& state, const char* inlineKey, const char* fileKey,
                              std::vector<uint8_t>& out) const;

        void createNote(int, std::vector<std::pair<int,int>>, std::vector<std::pair<int,int>>, int, std::vector<int> managerPath = {},
                        std::vector<std::pair<int, int>> pitchVector = {});

        void deleteNote(int nodeID, int regionID, int noteID, std::vector<int> managerPath = {});

        float mouseX;
        float mouseY;

        void togglePlaying();

        void stop();

        /** Move the playhead to an absolute beat, converting through the tempo curve. */
        void seekBeat(double beat);

        /** Take over the audio processing loop and stream [startBeat, endBeat)
            to a WAV. Runs on a worker thread so the GUI can draw progress. */
        bool exportRange(double startBeat, double endBeat, const std::string& path);
        std::atomic<bool> exportInProgress{false};
        std::atomic<float> exportProgressValue{0.f};

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
        std::thread exportThread;

};

#endif
