#pragma once

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
using json = nlohmann::json;

class Project;
class NodeManager;

class NodeProcessor {
public:
    explicit NodeProcessor(Project*);
    ~NodeProcessor();

    NodeManager* getManager() const { return guiManager; }

    /** GUI-owned project copy (rendering, editing). */
    NodeManager* guiManager = nullptr;
    /** Audio-owned project copy (DSP, must not have SDL resources). */
    NodeManager* audioManager = nullptr;

    /** Set the thread-local active manager. Call once per thread before any action. */
    static void setThreadActiveRoot(NodeManager* r);

    SDL_Window* getHostWindow() const;
    SDL_Renderer* getHostRenderer() const;

    json serialize() const;
    void deSerialize(const json&);

    void process(float* in, float* out, int numIn, int numOut, int bufferSize, int sampleRate);
    void render();
    void renderPresent();
    void handleWindowInput(SDL_Event& e);

private:
    Project* project = nullptr;
    class NodeEditor* editor = nullptr;
};
