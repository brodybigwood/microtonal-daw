#pragma once

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>
#include <mutex>
#include <functional>
#include <vector>
using json = nlohmann::json;

class Project;
class NodeManager;
class WindowManager;

class NodeProcessor {
public:
    explicit NodeProcessor(Project*);
    ~NodeProcessor();

    NodeManager* getManager() const { return guiGraph; }

    /** GUI-owned project copy (rendering, editing). */
    NodeManager* guiGraph = nullptr;
    /** Audio-owned project copy (DSP, must not have SDL resources). */
    NodeManager* dspGraph = nullptr;

    /** Set the thread-local active manager. Call once per thread before any action. */
    static void setThreadActiveRoot(NodeManager* r);
    /** Get the thread-local active manager (may be null). */
    static NodeManager* getActiveRoot();

    SDL_Window* getHostWindow() const { return hostWindow; }
    SDL_Renderer* getHostRenderer() const { return hostRenderer; }
    class NodeEditor* getEditor() const { return editor; }
    WindowManager* getWindowManager() const { return windowManager; }

    json serialize() const;
    void deSerialize(const json&);

    void process(float* in, float* out, int numIn, int numOut, int bufferSize, int sampleRate);
    void render();
    void renderPresent();
    void handleWindowInput(SDL_Event& e);

    /** Audio thread enqueues lambdas; GUI thread flushes them before each render frame. */
    void enqueueProcessorAction(std::function<void()> fn);
    void flushProcessorActions();

private:
    Project* project = nullptr;
    class NodeEditor* editor = nullptr;
    SDL_Window* hostWindow = nullptr;
    SDL_Renderer* hostRenderer = nullptr;
    WindowManager* windowManager = nullptr;

    std::mutex processorActionMutex_;
    std::vector<std::function<void()>> pendingProcessorActions_;
};
