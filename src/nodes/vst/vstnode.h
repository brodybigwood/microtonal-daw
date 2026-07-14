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

    bool restoringState = false;
    /** State-diff undo: next poll re-snapshots the baseline without creating an
        action. Set after any host-initiated state write (param actions,
        undo/redo replays, plugin load). */
    bool vstStateBaselineDirty = true;
    /** Ask the poller to run on the next editor tick instead of waiting out the
        interval — call alongside vstStateBaselineDirty to keep the absorb
        window at ~one frame. */
    void requestStatePollSoon();

    // Load a new plugin (with undo support)
    void loadPlugin(const std::string& path, bool createUndo = false);
    void unloadPlugin();

    // Map/unmap VST parameters to input connections.
    void mapVstParameter(int paramID);
    void unmapVstParameter(int paramID);
    bool mapVstParameterNow(int paramID);
    bool unmapVstParameterNow(int paramID);

    std::shared_ptr<TreeEntry> getNodeMenu() override;

    // Mapped VST parameters: paramID -> input Connection (public for undo actions).
    std::unordered_map<int, Connection*> mappedVstParams;

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
    bool wasBypassed_ = false;
    int allocMpeChannel(int noteId, void* eventList); // HostEventList*
    void freeMpeChannel(int noteId);
    void sendMpePitchBendRange(void* eventList); // HostEventList*

    // State-diff undo — catches plugin-internal edits (mod routing, toggles,
    // preset browsing) that never arrive via IComponentHandler::performEdit.
    // GUI thread only, driven from VstPlugin::tickEditor via onStatePoll.
    void pollVstStateForUndo(bool force);
    std::vector<uint8_t> stateBaselineComp_;
    std::vector<uint8_t> stateBaselineCtrl_;
    /** Mapped param values at baseline capture — a moved value explains a blob
        diff as connection-driven, so it re-baselines instead of acting. */
    std::unordered_map<int, float> mappedBaselineValues_;
    bool stateBaselineValid_ = false;
    uint64_t lastStatePollMs_ = 0;
};
