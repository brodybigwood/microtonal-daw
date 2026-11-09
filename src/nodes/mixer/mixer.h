#pragma once

#include "Node.h"
#include "Parameter.h"
#include "WindowManager.h"
#include "idManager.h"
#include <string>
#include <vector>
#include <memory>

class MixerNode;

// Simple insert list for one chain, like a Logic channel strip's inserts.
class MixerChainWindow : public ExpandedWindow {
public:
    MixerChainWindow(MixerNode* node, int chainID);
    ~MixerChainWindow() override;
    void render() override;
    bool handleInput(SDL_Event& e) override;

    MixerNode* mixerNode;
    int chainID;

    void setSelectedRow(int row) { selectedRow_ = row; }

private:
    int draggingNode_ = -1;
    int dragTargetInsert_ = -1;

    // Selected insert, shown in the node-sized preview on the right.
    int selectedRow_ = 0;
    bool previewDragging_ = false;
    SDL_Renderer* previewRenderer_ = nullptr;
    SDL_Texture* previewTex_ = nullptr;

    void renderPreview(SDL_Renderer*, Node* selected);
};

class MixerNode : public Node {
public:
    MixerNode(uint16_t, NodeManager*);
    ~MixerNode() override;

    void process() override;
    void setup() override;
    /** The graph's width pass calls this after propagating input widths and
        before relinking downstream: compute every chain's outflow width here so
        the relink sees the mixer's real width. */
    void syncWidthsFromInputs() override;
    int reportedLatency() const override { return cachedLatency_; }
    void render(SDL_Renderer* r) override;
    void renderContent(SDL_Renderer*) override;
    bool handleCustomInput(SDL_Event&) override;
    void handleWindowInput(SDL_Event&) override;

    json extraSerialize() override;
    void extraDeSerialize(const json&) override;

    // One mixer column: a serial list of nodes (top -> bottom).
    struct Chain {
        int id = -1;
        Connection* inflow = nullptr;  // this chain's multichannel input port
        Connection* outflow = nullptr; // this chain's own output (post pan/vol), besides the master
        std::vector<Node*> nodes;
        Knob pan;
        Bar volume;
        bool solo = false;
        bool mute = false;

        // Processing scratch.
        std::vector<float> buf;          // last output of the chain
        std::vector<std::vector<float>> ring; // latency-alignment delay
        int writePos = 0;
        int delay = 0;
        int latency = 0;
        int channels = 0;
        int appliedInCh = -1;

        Chain();
    };

    std::vector<std::unique_ptr<Chain>> chains;

    void openChainWindow(int chainID);
    /** Select an insert row in the open chain window (preview target). */
    void selectChainWindowInsert(int index);
    /** Called from ~MixerChainWindow so the dangling back-pointer is cleared. */
    void detachChainWindow(MixerChainWindow* w);
    /** Appends an insert (undoable); returns its node id (valid after the replayed
        ChainNodes action recreates the list), or -1. */
    int addNodeToChain(int chainIndex, NodeType type);
    void removeNodeFromChain(int chainIndex, int nodeIndex);
    void addChain(uint16_t inputPortID = 0);
    void removeChain(int index);

    // Structural operations run on whichever graph this node belongs to; the undo
    // action calls these through requireManager on both copies.
    void addChainOnGraph(int chainID, uint16_t portID);
    void removeChainOnGraph(int chainID);
    /** Create the chain's own output port (one per chain, after the master output).
        forcedID != 0 restores a saved port id so existing cables stay attached. */
    void createChainOutput(Chain& c, uint16_t forcedID = 0);
    void reorderChainsOnGraph(const std::vector<int>& chainIDs);
    json chainJson(int chainID) const;
    void restoreChain(const json& jc);
    json chainNodesJson(int chainID) const;
    void applyChainNodes(int chainID, const json& nodes);
    Chain* chainByID(int chainID);
    Node* chainNodeByID(int chainID, int nodeID);
    /** Append one insert from its serialized snapshot (id preserved). */
    Node* insertChainNodeFromSnapshot(int chainID, const json& nodeSnapshot);
    bool removeChainNodeByID(int chainID, int nodeID);
    bool moveChainNode(int chainID, int nodeID, int newIndex);

    static constexpr float kColW = 22.f;
    static constexpr float kColPad = 4.f;
    static constexpr float kPanH = 30.f;
    static constexpr float kRowH = 14.f;
    static constexpr float kBtnSz = 12.f;
    static constexpr float kSliderH = 40.f;
    static constexpr float kFooterH = 24.f;
    static constexpr float kListTop = 4.f;
    static constexpr float kNODE_H = 180.f;

private:
    Connection* out = nullptr;
    idManager chainIdPool_;
    idManager nodeIdPool_;

    float scrollOffset_ = 0.f;
    int hoveredChainIndex_ = -1;
    int draggingChain_ = -1;
    int dragTargetInsert_ = -1;
    float dragStartX_ = 0.f;
    float dragStartY_ = 0.f;

    int draggingVolumeIdx_ = -1;
    float oldVolume_ = 0.f;

    MixerChainWindow* chainWindow_ = nullptr;
    std::vector<float> silence_;
    int cachedLatency_ = 0;
    int outChannelsTarget_ = 1;

    Connection* addInputPort(uint16_t forcedID = 0);
    void removeInputPort(Connection* c);

    Node* createNode(NodeType type);
    void destroyNode(Node*);
    void reorderChains(const std::vector<size_t>& newOrder);
    void closeChainWindow();
    void syncChainParams();
};
