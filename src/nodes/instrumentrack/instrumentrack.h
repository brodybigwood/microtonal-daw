#pragma once

#include "Node.h"
#include "Parameter.h"
#include "WindowManager.h"
#include "idManager.h"
#include <string>
#include <vector>
#include <memory>

class InstrumentRackNode;

// Simple insert list for one track, like a Logic channel strip's inserts.
class InstrumentWindow : public ExpandedWindow {
public:
    InstrumentWindow(InstrumentRackNode* node, int trackID);
    ~InstrumentWindow() override;
    void render() override;
    bool handleInput(SDL_Event& e) override;

    InstrumentRackNode* rackNode;
    int trackID;

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

class InstrumentRackNode : public Node {
public:
    InstrumentRackNode(uint16_t, NodeManager*);
    ~InstrumentRackNode() override;

    void process() override;
    void setup() override;
    int reportedLatency() const override { return cachedLatency_; }
    void render(SDL_Renderer* r) override;
    void renderContent(SDL_Renderer*) override;
    bool handleCustomInput(SDL_Event&) override;
    void handleWindowInput(SDL_Event&) override;

    json extraSerialize() override;
    void extraDeSerialize(const json&) override;

    // One rack track: a parallel set of event-driven instruments.
    struct Track {
        int id = -1;
        Connection* inflow = nullptr;  // this track's event input port
        Connection* outflow = nullptr; // this track's audio output port
        std::vector<Node*> nodes;
        Knob pan;
        Bar volume;
        bool solo = false;
        bool mute = false;

        // Processing scratch.
        std::vector<float> buf;          // last output of the track
        std::vector<std::vector<float>> ring; // latency-alignment delay
        int writePos = 0;
        int delay = 0;
        int latency = 0;
        int channels = 0;
        int appliedInCh = -1;

        Track();
    };

    std::vector<std::unique_ptr<Track>> tracks;

    void openTrackWindow(int trackID);
    /** Select an insert row in the open track window (preview target). */
    void selectTrackWindowInsert(int index);
    /** Called from ~InstrumentWindow so the dangling back-pointer is cleared. */
    void detachTrackWindow(InstrumentWindow* w);
    /** Appends an insert (undoable); returns its node id (valid after the replayed
        TrackNodes action recreates the list), or -1. */
    int addNodeToTrack(int trackIndex, NodeType type);
    void removeNodeFromTrack(int trackIndex, int nodeIndex);
    void addTrack(uint16_t inputPortID = 0);
    void removeTrack(int index);

    // Structural operations run on whichever graph this node belongs to; the undo
    // action calls these through requireManager on both copies.
    void addTrackOnGraph(int trackID, uint16_t portID, uint16_t outPortID = 0);
    void removeTrackOnGraph(int trackID);
    void reorderTracksOnGraph(const std::vector<int>& trackIDs);
    json trackJson(int trackID) const;
    void restoreTrack(const json& jc);
    json trackNodesJson(int trackID) const;
    void applyTrackNodes(int trackID, const json& nodes);
    Track* trackByID(int trackID);
    Node* trackNodeByID(int trackID, int nodeID);
    /** Append one insert from its serialized snapshot (id preserved). */
    Node* insertTrackNodeFromSnapshot(int trackID, const json& nodeSnapshot);
    bool removeTrackNodeByID(int trackID, int nodeID);

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
    idManager trackIdPool_;
    idManager nodeIdPool_;

    float scrollOffset_ = 0.f;
    int hoveredTrackIndex_ = -1;
    int draggingTrack_ = -1;
    int dragTargetInsert_ = -1;
    float dragStartX_ = 0.f;
    float dragStartY_ = 0.f;

    int draggingVolumeIdx_ = -1;
    float oldVolume_ = 0.f;

    InstrumentWindow* trackWindow_ = nullptr;
    std::vector<float> silence_;
    int cachedLatency_ = 0;
    int outChannelsTarget_ = 1;

    Connection* addInputPort(uint16_t forcedID = 0);
    void removeInputPort(Connection* c);
    Connection* addOutputPort(uint16_t forcedID = 0);
    void removeOutputPort(Connection* c);

    Node* createNode(NodeType type);
    void destroyNode(Node*);
    void reorderTracks(const std::vector<size_t>& newOrder);
    void closeTrackWindow();
    void syncTrackParams();
};
