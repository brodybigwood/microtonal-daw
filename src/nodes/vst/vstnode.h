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
    void extraDeSerialize(json) override;

    // Called by EditorHostFrame when plugin parameter changes
    void onPluginParameterChange(int paramID, float newValue);

    // Load a new plugin (with undo support)
    void loadPlugin(const std::string& path, bool createUndo = false);
    void unloadPlugin();

    VstPlugin* plugin = nullptr; // owned by VstPluginCache, shared across copies

    Knob bypass = Knob(0.0f, 80.0f, TEX_H * 0.3f, 24.0f, "assets/knobs/1.png",
                       -135.0f, 135.0f, "Bypass", SDL_Color{220, 220, 220, 255});

private:
    void rebuildConnections();

    // Audio channel connections — one per mono channel
    std::vector<Connection*> audioInConns;
    std::vector<Connection*> audioOutConns;
    // Maps each connection to (busIndex, channelIndex)
    std::vector<std::pair<int,int>> inChannelMap;
    std::vector<std::pair<int,int>> outChannelMap;
    // Event bus connections
    Connection* midiIn = nullptr;
    Connection* midiOut = nullptr;

    SDL_FRect loadBtnRect{160, 80, 160, 30};
    SDL_FRect editorBtnRect{160, 120, 160, 30};
    SDL_FRect nameRect{40, 40, 400, 30};

    std::string loadedPath; // currently loaded plugin path

    // MPE channel allocator: maps noteId -> channel (1-15)
    std::unordered_map<int, int> noteChannels;
    int nextMpeChannel = 1;
    bool mpeRangeSet = false;
    int allocMpeChannel(int noteId);
    void freeMpeChannel(int noteId);
    void sendMpePitchBendRange(void* eventList); // HostEventList*
};
