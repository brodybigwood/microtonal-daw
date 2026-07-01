#pragma once

#include "Node.h"
#include "vstplugin.h"
#include <memory>
#include <vector>
#include <unordered_map>

class VstNode : public Node {
public:
    VstNode(uint16_t, NodeManager*);
    ~VstNode() override;

    void process() override;
    void setup() override;
    void renderContent(SDL_Renderer*) override;
    bool handleCustomInput(SDL_Event&) override;
    void handleWindowInput(SDL_Event&) override;

    json extraSerialize() override;
    void extraDeSerialize(const json&) override;

    // Called by EditorHostFrame when plugin parameter changes
    void onPluginParameterChange(int paramID, float oldValue, float newValue);
    void wirePluginCallbacks();

    // Load a new plugin (with undo support)
    void loadPlugin(const std::string& path, bool createUndo = false);
    void unloadPlugin();

    std::shared_ptr<VstPlugin> plugin; // shared across copies via VstPluginCache

    Knob bypass = Knob(0.0f, 20.0f, NODE_H * 0.3f, 10.0f, "assets/knobs/1.png",
                       -135.0f, 135.0f, "Bypass", SDL_Color{220, 220, 220, 255});

private:
    void rebuildConnections();

    // Audio bus connections — one per bus, each with numChannels = bus channel count
    std::vector<Connection*> audioInConns;
    std::vector<Connection*> audioOutConns;
    // Event bus connections
    Connection* midiIn = nullptr;
    Connection* midiOut = nullptr;

    // Layout rects (updated each frame in renderContent).
    SDL_FRect dropdownRect_{8, 8, NODE_W - 16, 24};
    SDL_FRect editorBtnRect_{8, 38, NODE_W - 16, 24};

    std::string loadedPath; // currently loaded plugin path

    // MPE channel allocator: maps noteId -> channel (1-15)
    std::unordered_map<int, int> noteChannels;
    std::unordered_map<int, int> evictedNoteChannels; // noteId -> channel for evicted notes
    int nextMpeChannel = 1;
    bool mpeRangeSet = false;
    int allocMpeChannel(int noteId, void* eventList); // HostEventList*
    void freeMpeChannel(int noteId);
    void sendMpePitchBendRange(void* eventList); // HostEventList*
};
