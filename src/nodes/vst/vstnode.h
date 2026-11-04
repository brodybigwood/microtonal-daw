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
    int reportedLatency() const override { return plugin ? plugin->latencySamples() : 0; }
    void renderContent(SDL_Renderer*) override;
    bool handleCustomInput(SDL_Event&) override;
    void handleWindowInput(SDL_Event&) override;

    json extraSerialize() override;
    void extraDeSerialize(const json&) override;

    // Called by EditorHostFrame when plugin parameter changes. gestureID is the
    // beginEdit..endEdit gesture the change belongs to; only edits of the same
    // gesture coalesce into one undo step.
    void onPluginParameterChange(int paramID, float oldValue, float newValue, uint64_t gestureID);
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

    // Load a new plugin (with undo support). classIndex selects a class inside a
    // multi-class bundle; SIZE_MAX keeps the current/zero index.
    void loadPlugin(const std::string& path, bool createUndo = false,
                    size_t classIndex = static_cast<size_t>(-1));
    void unloadPlugin();

    /** Open a menu of the classes inside a bundle, then load the chosen one. */
    void openPluginClassMenu(const std::string& path, bool createUndo);
    size_t getClassIndex() const { return classIndex_; }
    const std::string& getClassName() const { return className_; }

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
    /** Ports changed: the owning mixer chain must re-run its per-node port setup. */
    void invalidateOwnerChain();
    /** Apply state that arrived before the plugin was set up. */
    void applyPendingVstState();
    /** Record the post-load state as the undo baseline (GUI copy, editor closed). */
    void captureStateBaseline();

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
    size_t classIndex_ = 0; // class inside the bundle
    std::string className_;
    // State restored from JSON before the plugin was set up; applied once setup runs.
    std::vector<uint8_t> pendingCompState_;
    std::vector<uint8_t> pendingCtrlState_;

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
    /** The plugin reported a preset load / internal state change; the next poll
        diffs even though parameter edits made the baseline dirty. */
    bool stateResetPending_ = false;
    uint64_t lastStatePollMs_ = 0;

    /** Last latency the plugin reported. When it changes (often only after the
        first process block), the graph recomputes compensation. */
    int lastReportedLatency_ = -1;
    uint64_t lastProcessLogMs_ = 0;
};
