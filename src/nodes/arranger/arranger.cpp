#include "arranger.h"
#include <iostream>
#include "NodeManager.h"
#include "NodeEditor.h"
#include "PianoRoll.h"
#include "TrackManager.h"
#include "ElementManager.h"
#include "ArrangerExpandedWindow.h"
#include "WindowManager.h"
#include "NodeProcessor.h"
#include <cstring>

ArrangerNode::ArrangerNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Arranger) {
    slRect = new SDL_FRect{0, 0, NODE_W, NODE_H};
    tracks = new TrackManager(this);
    elements = new ElementManager(project, tracks, this);
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
    if (elements) {
        delete elements;
        elements = nullptr;
    }
    if (tracks) {
        delete tracks;
        tracks = nullptr;
    }
}

void ArrangerNode::process() {
    if (!tracks || !elements) return;
    tracks->process(nullptr, bufferSize);
    elements->process(bufferSize);
}

void ArrangerNode::setup() {
}

// Layout rects for arranger node buttons.
static const SDL_FRect kOpenBtnRect{10.f, 8.f, NODE_W - 20.f, 36.f};
static const SDL_FRect kAddEventBtnRect{10.f, 50.f, (NODE_W - 30.f) * 0.5f, 28.f};
static const SDL_FRect kAddWaveBtnRect{kAddEventBtnRect.x + kAddEventBtnRect.w + 10.f, 50.f, (NODE_W - 30.f) * 0.5f, 28.f};

static bool inRect(float mx, float my, const SDL_FRect& r) {
    return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
}

bool ArrangerNode::handleCustomInput(SDL_Event& e) {
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
        if (inRect(msX, msY, kOpenBtnRect)) {
            if (project && project->processor) {
                auto* wm = project->processor->getWindowManager();
                if (wm) {
                    std::string title = "Arranger##" + std::to_string(id);
                    auto* existing = wm->findByTitle(title.c_str());
                    if (existing) {
                        existing->show();
                        SDL_RaiseWindow(existing->window);
                    } else {
                        auto aw = std::make_unique<ArrangerExpandedWindow>(this, project);
                        ExpandedWindow* ew = wm->addWindow(std::move(aw), 1200, 800, title.c_str());
                        if (ew) ew->show();
                    }
                }
            }
            return true;
        }
        if (inRect(msX, msY, kAddEventBtnRect)) {
            if (tracks) tracks->addTrack(TrackType::Notes);
            return true;
        }
        if (inRect(msX, msY, kAddWaveBtnRect)) {
            if (tracks) tracks->addTrack(TrackType::Audio);
            return true;
        }
    }
    return false;
}

static void renderBtn(SDL_Renderer* r, const SDL_FRect& rect, const char* label, bool hovered) {
    SDL_SetRenderDrawColor(r, hovered ? 75 : 60, hovered ? 75 : 60, hovered ? 83 : 68, 255);
    SDL_RenderFillRect(r, &rect);
    SDL_SetRenderDrawColor(r, 100, 100, 110, 255);
    SDL_RenderRect(r, &rect);
    if (fonts.mainFont) {
        SDL_Surface* s = TTF_RenderText_Blended(fonts.mainFont, label, 0, SDL_Color{220, 220, 220, 255});
        if (s) {
            SDL_Texture* t = SDL_CreateTextureFromSurface(r, s);
            if (t) {
                float tw = static_cast<float>(s->w), th = static_cast<float>(s->h);
                SDL_FRect dst{rect.x + rect.w * 0.5f - tw * 0.5f, rect.y + rect.h * 0.5f - th * 0.5f, tw, th};
                SDL_RenderTexture(r, t, nullptr, &dst);
                SDL_DestroyTexture(t);
            }
            SDL_DestroySurface(s);
        }
    }
}

void ArrangerNode::renderContent(SDL_Renderer* renderer) {
    if (!vCount) {
        vCount = 4;
        vx = new float[vCount];
        vy = new float[vCount];
        vx[0] = 0; vx[1] = NODE_W; vx[2] = NODE_W; vx[3] = 0;
        vy[0] = 0; vy[1] = 0; vy[2] = NODE_H; vy[3] = NODE_H;
    }

    bool hOpen = inRect(msX, msY, kOpenBtnRect);
    bool hEv = inRect(msX, msY, kAddEventBtnRect);
    bool hWv = inRect(msX, msY, kAddWaveBtnRect);

    renderBtn(renderer, kOpenBtnRect, "Open", hOpen);
    renderBtn(renderer, kAddEventBtnRect, "+Event", hEv);
    renderBtn(renderer, kAddWaveBtnRect, "+Wave", hWv);

    renderParams(renderer);
}

void ArrangerNode::clearCustomTextures() {
    if (!sl) return;
    sl->clearTextures();
}


json ArrangerNode::extraSerialize() {
    json j;
    j["TrackManager"] = tracks ? tracks->toJSON() : json::object();
    j["ElementManager"] = elements ? elements->toJSON() : json::object();

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

void ArrangerNode::extraDeSerialize(const json& j) {
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

    rebuildState(j);
    std::cout << "[DBG_DESER]  arranger rebuildState done node=" << id << std::endl;

    if (sl) {
        sl->tracks = tracks;
        sl->em = elements;
    }
}

void ArrangerNode::ensureSongRoll() {
    if (sl || !ne) return;
    sl = new SongRoll(slRect, project, project, this);
    std::cout << "[DBG_DESER] ArrangerNode::ensureSongRoll node=" << id << " created" << std::endl;
}

void ArrangerNode::rebuildState(json j) {
    std::cout << "[DBG_DESER]  rebuildState begin node=" << id << std::endl;
    if (elements) {
        delete elements;
        elements = nullptr;
    }
    if (tracks) {
        delete tracks;
        tracks = nullptr;
    }
    tracks = new TrackManager(this);
    elements = new ElementManager(project, tracks, this);
    if (j.contains("TrackManager")) {
        std::cout << "[DBG_DESER]  rebuildState TrackManager begin" << std::endl;
        tracks->fromJSON(j["TrackManager"]);
        std::cout << "[DBG_DESER]  rebuildState TrackManager done" << std::endl;
    }
    if (j.contains("ElementManager")) {
        std::cout << "[DBG_DESER]  rebuildState ElementManager begin" << std::endl;
        elements->fromJSON(j["ElementManager"]);
        std::cout << "[DBG_DESER]  rebuildState ElementManager done" << std::endl;
    }
    std::cout << "[DBG_DESER]  rebuildState end node=" << id << std::endl;
}

void ArrangerNode::syncSongRollContext() {
    if (!sl) return;
}
