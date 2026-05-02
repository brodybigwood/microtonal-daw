#include "Note.h"
#include <vector>
#include "fract.h"
#include <iostream>
#include <atomic>
#include "UndoManager.h"
#include "Window.h"

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

        int beatsToSamples(float);

        std::string filepath = "";

        float tempo = 120;

        void load(std::string path = "");

        void save(uint32_t triggerWindowID = 0, SDL_Renderer* triggerRenderer = nullptr);

        void createNote(int, fract, fract, float, int, std::vector<int> managerPath = {});

        fract startTime;

        float mouseX;
        float mouseY;

        void togglePlaying();

        void stop();

        uint16_t id_reg = 0;

        void tick();

        void setup();

        fract playHeadStart;
        fract playHeadPos = fract(0,1);

        std::atomic<bool> isPlaying{false};

        std::atomic<double> timeSeconds{0.0};

        std::atomic<double> effectiveTime{0.0};

        void process(float* input, float* output, int& bufferSize, int& numChannelsIn, int& numChannelsOut, int& sampleRate);

        UndoManager* um;
        void undo() { um->undo(); }
        void redo() { um->redo(); }

    private:

};

#endif
