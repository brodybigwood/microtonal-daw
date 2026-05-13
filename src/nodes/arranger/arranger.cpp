#include "arranger.h"
#include <iostream>
#include "NodeManager.h"
#include "NodeEditor.h"
#include "PianoRoll.h"
#include "TrackManager.h"
#include "ElementManager.h"
#include <cstring>

ArrangerNode::ArrangerNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Arranger) {
    slRect = new SDL_FRect{0, 0, TEX_W, TEX_H};
    runtimeTracks = new TrackManager(this);
    runtimeElements = new ElementManager(project, runtimeTracks, this);
}

ArrangerNode::~ArrangerNode() {
    if (sl) {
        delete sl;
        sl = nullptr;
    }
    if (slRect) {
        delete slRect;
        slRect = nullptr;
    }
    if (runtimeElements) {
        delete runtimeElements;
        runtimeElements = nullptr;
    }
    if (runtimeTracks) {
        delete runtimeTracks;
        runtimeTracks = nullptr;
    }
}

TrackManager* ArrangerNode::activeTrackManager() {
    return sl ? sl->tracks : runtimeTracks;
}

ElementManager* ArrangerNode::activeElementManager() {
    return sl ? sl->em : runtimeElements;
}

void ArrangerNode::process() {
    TrackManager* tracks = activeTrackManager();
    ElementManager* elements = activeElementManager();
    if (!tracks || !elements) return;
    tracks->process(nullptr, bufferSize);
    elements->process(bufferSize);
}

void ArrangerNode::setup() {
}

bool ArrangerNode::handleCustomInput(SDL_Event& e) {
    ensureSongRoll();
    if (!sl) return false;
    syncSongRollContext();
    sl->mouseX = msX - slRect->x;
    sl->mouseY = msY - slRect->y;

    return sl->handleInput(e);
}

void ArrangerNode::renderContent(SDL_Renderer* renderer) {
    ensureSongRoll();
    if (!sl) return;
    syncSongRollContext();
    if (!vCount) {
        vCount = 4;
        vx = new float[vCount];
        vy = new float[vCount];

        vx[0] = slRect->x;
        vx[1] = slRect->x + slRect->w;
        vx[2] = slRect->x + slRect->w;
        vx[3] = slRect->x;

        vy[0] = slRect->y;
        vy[1] = slRect->y;
        vy[2] = slRect->y + slRect->h;
        vy[3] = slRect->y + slRect->h;
    }

    sl->tick();
    renderParams(renderer);
}      

void ArrangerNode::clearCustomTextures() {
    if (!sl) return;
    sl->clearTextures();
    sl->renderer = renderer;
    sl->window = window;
    sl->generateTextures();
}

void ArrangerNode::renderPresent() {
    if (!sl) return;
    if (detached) SDL_RenderPresent(renderer);
    if (sl->pianoRoll && sl->pianoRollDetached) SDL_RenderPresent(sl->pianoRoll->renderer);
}

json ArrangerNode::extraSerialize() {
    json j;
    if (sl) {
        j["TrackManager"] = sl->tracks->toJSON();
        j["ElementManager"] = sl->em->toJSON();
    } else if (hasPendingExtraState) {
        j = pendingExtraState;
    } else if (runtimeTracks && runtimeElements) {
        j["TrackManager"] = runtimeTracks->toJSON();
        j["ElementManager"] = runtimeElements->toJSON();
    } else {
        j["TrackManager"] = json::object();
        j["ElementManager"] = json::object();
    }

    json o = json::array();
    for (auto c : outputs.connections) {
        json jc;
        jc["id"] = c->id;
        jc["type"] = c->type;
        o.push_back(jc);
    }
    j["outputs"] = o;

    return j;
}

void ArrangerNode::extraDeSerialize(json j) {
    std::cout << "[DBG_DESER] ArrangerNode::extraDeSerialize node=" << id << " begin" << std::endl;
    for (auto* c : outputs.connections) {
        delete c;
    }
    outputs.connections.clear();
    outputs.ids.clear();
    outputs.id_pool = idManager();

    if (j.contains("outputs")) {
        for (auto jc : j["outputs"]) {
            auto c = new Connection;
            c->nm = outputs.nm;
            c->id = jc["id"];
            c->dir = Direction::output;
            c->type = jc["type"];
            c->is_connected = false;
            c->output_node = -1;
            c->output_connection = -1;
            c->input_node = id;
            c->input_connection = c->id;

            if (c->type == DataType::Events) {
                c->events = new std::vector<Event>;
                c->buffer = nullptr;
            } else {
                c->buffer = nullptr;
                c->bufferSize = 0;
                c->events = nullptr;
            }

            outputs.connections.push_back(c);
            outputs.id_pool.reserveID(c->id);
            outputs.ids[c->id] = outputs.connections.size() - 1;
            std::cout << "[DBG_DESER]  arranger out restored id=" << c->id << " type=" << static_cast<int>(c->type) << std::endl;
        }
    }
    makeConnectionRects();
    std::cout << "[DBG_DESER]  arranger outputs count=" << outputs.connections.size() << std::endl;

    rebuildRuntimeState(j);

    if (!sl) {
        pendingExtraState = json::object();
        if (j.contains("TrackManager")) pendingExtraState["TrackManager"] = j["TrackManager"];
        if (j.contains("ElementManager")) pendingExtraState["ElementManager"] = j["ElementManager"];
        hasPendingExtraState = true;
        std::cout << "[DBG_DESER]  arranger deferred track/element payload" << std::endl;
        return;
    }

    if (j.contains("TrackManager")) sl->tracks->fromJSON(j["TrackManager"]);
    if (j.contains("ElementManager")) sl->em->fromJSON(j["ElementManager"]);
    std::cout << "[DBG_DESER] ArrangerNode::extraDeSerialize node=" << id << " applied track/element now" << std::endl;
}

void ArrangerNode::ensureSongRoll() {
    if (sl || !ne || !ne->window || !ne->renderer) return;
    sl = new SongRoll(slRect, &slDetached, ne, project, this);
    std::cout << "[DBG_DESER] ArrangerNode::ensureSongRoll node=" << id << " created" << std::endl;
    /* Seed the UI from the live runtime graph: undo/redo (and AddArrangerTrack, etc.) may have run while
       SongRoll did not exist; pendingExtraState is only the deserialize snapshot and would clobber that. */
    if (runtimeTracks && runtimeElements) {
        sl->tracks->fromJSON(runtimeTracks->toJSON());
        sl->em->fromJSON(runtimeElements->toJSON());
        std::cout << "[DBG_DESER] ArrangerNode::ensureSongRoll node=" << id << " copied runtime track/element -> SongRoll"
                  << std::endl;
    } else if (hasPendingExtraState) {
        const auto j = pendingExtraState;
        if (j.contains("TrackManager")) sl->tracks->fromJSON(j["TrackManager"]);
        if (j.contains("ElementManager")) sl->em->fromJSON(j["ElementManager"]);
        std::cout << "[DBG_DESER] ArrangerNode::ensureSongRoll node=" << id << " applied pending track/element fallback"
                  << std::endl;
    }
    hasPendingExtraState = false;
    pendingExtraState = json{};
}

void ArrangerNode::rebuildRuntimeState(json j) {
    if (runtimeElements) {
        delete runtimeElements;
        runtimeElements = nullptr;
    }
    if (runtimeTracks) {
        delete runtimeTracks;
        runtimeTracks = nullptr;
    }
    runtimeTracks = new TrackManager(this);
    runtimeElements = new ElementManager(project, runtimeTracks, this);
    if (j.contains("TrackManager")) runtimeTracks->fromJSON(j["TrackManager"]);
    if (j.contains("ElementManager")) runtimeElements->fromJSON(j["ElementManager"]);
}

void ArrangerNode::syncSongRollContext() {
    if (!sl) return;
    if (sl->renderer == renderer && sl->window == window) return;
    sl->clearTextures();
    sl->renderer = renderer;
    sl->window = window;
    sl->generateTextures();
}
