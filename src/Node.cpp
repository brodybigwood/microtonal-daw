#include "Node.h"
#include "NodeManager.h"
#include "SDL_Events.h"
#include "ContextMenu.h"
#include <iostream>
#include "NodeEditor.h"
#include "WindowHandler.h"
#include "Preferences.h"
#include "UndoManager.h"
#include <cstring>
#include <limits>
#include <sstream>
#include <iomanip>
#include "styles.h"

namespace {
std::function<bool(SDL_Event&)> getModulationMatrixTicker(Node* node, Parameter* p) {
    struct State {
        int draggingIndex = -1;
    };
    auto state = std::make_shared<State>();

    auto snapValue = [](float v, float minV, float maxV) {
        const float step = std::max(1e-6f, (maxV - minV) * 0.01f);
        const float snapped = std::round(v / step) * step;
        return std::clamp(snapped, minV, maxV);
    };

    return [node, p, state, snapValue](SDL_Event& e) {
        auto* ctxMenu = ContextMenu::get();
        SDL_Renderer* renderer = ctxMenu->renderer;
        if (!renderer || !p) return false;

        const float panelX = ctxMenu->locX;
        const float panelY = ctxMenu->locY;
        const float panelW = 520.0f;
        const float headerH = 30.0f;
        const float rowH = 34.0f;
        const float footerH = 36.0f;
        const float panelH = headerH + rowH * static_cast<float>(std::max<size_t>(1, p->modulators.size())) + footerH + 12.0f;
        SDL_FRect panel{panelX, panelY, panelW, panelH};

        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT && !MouseOn(&panel)) {
            return false;
        }

        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
            state->draggingIndex = -1;
        }

        SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
        SDL_RenderFillRect(renderer, &panel);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderRect(renderer, &panel);

        if (fonts.mainFont) {
            SDL_Surface* titleSurf = TTF_RenderText_Blended(fonts.mainFont, "Modulation Matrix", 0, SDL_Color{0, 0, 0, 255});
            if (titleSurf) {
                SDL_Texture* titleTex = SDL_CreateTextureFromSurface(renderer, titleSurf);
                if (titleTex) {
                    SDL_FRect tr{panel.x + 8.0f, panel.y + 6.0f, static_cast<float>(titleSurf->w), static_cast<float>(titleSurf->h)};
                    SDL_RenderTexture(renderer, titleTex, nullptr, &tr);
                    SDL_DestroyTexture(titleTex);
                }
                SDL_DestroySurface(titleSurf);
            }
        }

        for (size_t i = 0; i < p->modulators.size(); ++i) {
            auto* m = p->modulators[i];
            if (!m) continue;

            SDL_FRect row{panel.x + 6.0f, panel.y + headerH + static_cast<float>(i) * rowH, panel.w - 12.0f, rowH - 2.0f};
            SDL_SetRenderDrawColor(renderer, 235, 235, 235, 255);
            SDL_RenderFillRect(renderer, &row);
            SDL_SetRenderDrawColor(renderer, 140, 140, 140, 255);
            SDL_RenderRect(renderer, &row);

            SDL_FRect centerBtn{row.x + 6.0f, row.y + 5.0f, 120.0f, row.h - 10.0f};
            SDL_SetRenderDrawColor(renderer, m->centered ? 160 : 205, m->centered ? 235 : 205, 160, 255);
            SDL_RenderFillRect(renderer, &centerBtn);
            SDL_SetRenderDrawColor(renderer, 90, 90, 90, 255);
            SDL_RenderRect(renderer, &centerBtn);

            SDL_FRect removeBtn{row.x + row.w - 84.0f, row.y + 5.0f, 78.0f, row.h - 10.0f};
            SDL_SetRenderDrawColor(renderer, 235, 170, 170, 255);
            SDL_RenderFillRect(renderer, &removeBtn);
            SDL_SetRenderDrawColor(renderer, 90, 90, 90, 255);
            SDL_RenderRect(renderer, &removeBtn);

            SDL_FRect depthValueRect{removeBtn.x - 70.0f, row.y + 5.0f, 62.0f, row.h - 10.0f};
            SDL_SetRenderDrawColor(renderer, 245, 245, 245, 255);
            SDL_RenderFillRect(renderer, &depthValueRect);
            SDL_SetRenderDrawColor(renderer, 120, 120, 120, 255);
            SDL_RenderRect(renderer, &depthValueRect);

            SDL_FRect slider{
                centerBtn.x + centerBtn.w + 10.0f,
                row.y + row.h * 0.5f - 5.0f,
                depthValueRect.x - (centerBtn.x + centerBtn.w + 10.0f) - 10.0f,
                10.0f
            };
            SDL_SetRenderDrawColor(renderer, 190, 190, 190, 255);
            SDL_RenderFillRect(renderer, &slider);
            SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
            SDL_RenderRect(renderer, &slider);

            const float centerX = slider.x + slider.w * 0.5f;
            const float minDepth = m->centered ? -0.5f : 0.0f;
            const float maxDepth = m->centered ? 0.5f : 1.0f;
            const float range = std::max(1e-6f, maxDepth - minDepth);
            const float markerX = m->centered ? centerX : slider.x;
            SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
            SDL_RenderLine(renderer, markerX, slider.y - 5.0f, markerX, slider.y + slider.h + 5.0f);

            m->depth = std::clamp(m->depth, minDepth, maxDepth);
            const float t = (m->depth - minDepth) / range;
            const float knobX = slider.x + t * slider.w;
            SDL_FRect knob{knobX - 4.0f, slider.y - 4.0f, 8.0f, slider.h + 8.0f};
            SDL_SetRenderDrawColor(renderer, 80, 120, 230, 255);
            SDL_RenderFillRect(renderer, &knob);

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                if (MouseOn(&centerBtn)) {
                    m->centered = !m->centered;
                    if (m->centered) {
                        m->depth = std::clamp(m->depth, -0.5f, 0.5f);
                    } else {
                        m->depth = std::clamp(m->depth, 0.0f, 1.0f);
                    }
                } else if (MouseOn(&removeBtn)) {
                    node->removeModSource(p, i);
                    return true;
                } else if (MouseOn(&slider)) {
                    state->draggingIndex = static_cast<int>(i);
                }
            }

            if (state->draggingIndex == static_cast<int>(i) && (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_MOTION)) {
                float mx = 0.0f, my = 0.0f;
                SDL_GetMouseState(&mx, &my);
                const float norm = std::clamp((mx - slider.x) / slider.w, 0.0f, 1.0f);
                const float dMin = m->centered ? -0.5f : 0.0f;
                const float dMax = m->centered ? 0.5f : 1.0f;
                m->depth = snapValue(dMin + norm * (dMax - dMin), dMin, dMax);
            }

            if (fonts.mainFont) {
                const std::string cText = std::string("Center ") + (m->centered ? "ON" : "OFF");
                SDL_Surface* cSurf = TTF_RenderText_Blended(fonts.mainFont, cText.c_str(), 0, SDL_Color{0, 0, 0, 255});
                if (cSurf) {
                    SDL_Texture* cTex = SDL_CreateTextureFromSurface(renderer, cSurf);
                    if (cTex) {
                        const float cScale = std::min(1.0f, (centerBtn.w - 8.0f) / static_cast<float>(cSurf->w));
                        SDL_FRect tr{
                            centerBtn.x + (centerBtn.w - cSurf->w * cScale) * 0.5f,
                            centerBtn.y + (centerBtn.h - cSurf->h * cScale) * 0.5f,
                            cSurf->w * cScale,
                            cSurf->h * cScale
                        };
                        SDL_RenderTexture(renderer, cTex, nullptr, &tr);
                        SDL_DestroyTexture(cTex);
                    }
                    SDL_DestroySurface(cSurf);
                }

                SDL_Surface* rmSurf = TTF_RenderText_Blended(fonts.mainFont, "Remove", 0, SDL_Color{0, 0, 0, 255});
                if (rmSurf) {
                    SDL_Texture* rmTex = SDL_CreateTextureFromSurface(renderer, rmSurf);
                    if (rmTex) {
                        const float rScale = std::min(1.0f, (removeBtn.w - 8.0f) / static_cast<float>(rmSurf->w));
                        SDL_FRect tr{
                            removeBtn.x + (removeBtn.w - rmSurf->w * rScale) * 0.5f,
                            removeBtn.y + (removeBtn.h - rmSurf->h * rScale) * 0.5f,
                            rmSurf->w * rScale,
                            rmSurf->h * rScale
                        };
                        SDL_RenderTexture(renderer, rmTex, nullptr, &tr);
                        SDL_DestroyTexture(rmTex);
                    }
                    SDL_DestroySurface(rmSurf);
                }

                std::ostringstream ds;
                ds << std::fixed << std::setprecision(2) << m->depth;
                const std::string dText = ds.str();
                SDL_Surface* dSurf = TTF_RenderText_Blended(fonts.mainFont, dText.c_str(), 0, SDL_Color{0, 0, 0, 255});
                if (dSurf) {
                    SDL_Texture* dTex = SDL_CreateTextureFromSurface(renderer, dSurf);
                    if (dTex) {
                        const float dScale = std::min(1.0f, (depthValueRect.w - 6.0f) / static_cast<float>(dSurf->w));
                        SDL_FRect tr{
                            depthValueRect.x + (depthValueRect.w - dSurf->w * dScale) * 0.5f,
                            depthValueRect.y + (depthValueRect.h - dSurf->h * dScale) * 0.5f,
                            dSurf->w * dScale,
                            dSurf->h * dScale
                        };
                        SDL_RenderTexture(renderer, dTex, nullptr, &tr);
                        SDL_DestroyTexture(dTex);
                    }
                    SDL_DestroySurface(dSurf);
                }
            }
        }

        SDL_FRect addBtn{
            panel.x + 8.0f,
            panel.y + panel.h - footerH + 6.0f,
            170.0f,
            footerH - 12.0f
        };
        SDL_SetRenderDrawColor(renderer, 180, 215, 255, 255);
        SDL_RenderFillRect(renderer, &addBtn);
        SDL_SetRenderDrawColor(renderer, 90, 90, 90, 255);
        SDL_RenderRect(renderer, &addBtn);
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT && MouseOn(&addBtn)) {
            node->addModSource(p);
            return true;
        }
        if (fonts.mainFont) {
            SDL_Surface* addSurf = TTF_RenderText_Blended(fonts.mainFont, "Add Modulator", 0, SDL_Color{0, 0, 0, 255});
            if (addSurf) {
                SDL_Texture* addTex = SDL_CreateTextureFromSurface(renderer, addSurf);
                if (addTex) {
                    const float aScale = std::min(1.0f, (addBtn.w - 10.0f) / static_cast<float>(addSurf->w));
                    SDL_FRect tr{
                        addBtn.x + (addBtn.w - addSurf->w * aScale) * 0.5f,
                        addBtn.y + (addBtn.h - addSurf->h * aScale) * 0.5f,
                        addSurf->w * aScale,
                        addSurf->h * aScale
                    };
                    SDL_RenderTexture(renderer, addTex, nullptr, &tr);
                    SDL_DestroyTexture(addTex);
                }
                SDL_DestroySurface(addSurf);
            }

            // Theoretical normalized parameter range across all possible modulator source values.
            float minV = p->value;
            float maxV = p->value;
            for (auto* m : p->modulators) {
                if (!m) continue;
                float a = 0.0f;
                float b = 0.0f;
                if (m->centered) {
                    a = -m->depth;
                    b = m->depth;
                } else {
                    a = 0.0f;
                    b = m->depth;
                }
                minV += std::min(a, b);
                maxV += std::max(a, b);
            }
            minV = std::clamp(minV, 0.0f, 1.0f);
            maxV = std::clamp(maxV, 0.0f, 1.0f);

            std::ostringstream rs;
            rs << std::fixed << std::setprecision(2) << minV << " .. " << maxV;
            const std::string rangeText = "Range " + rs.str();
            SDL_Surface* rangeSurf = TTF_RenderText_Blended(fonts.mainFont, rangeText.c_str(), 0, SDL_Color{0, 0, 0, 255});
            if (rangeSurf) {
                SDL_Texture* rangeTex = SDL_CreateTextureFromSurface(renderer, rangeSurf);
                if (rangeTex) {
                    SDL_FRect rangeRect{
                        addBtn.x + addBtn.w + 12.0f,
                        addBtn.y + (addBtn.h - rangeSurf->h) * 0.5f,
                        static_cast<float>(rangeSurf->w),
                        static_cast<float>(rangeSurf->h)
                    };
                    SDL_RenderTexture(renderer, rangeTex, nullptr, &rangeRect);
                    SDL_DestroyTexture(rangeTex);
                }
                SDL_DestroySurface(rangeSurf);
            }
        }

        return true;
    };
}
}

namespace {
/** Ports / sever / wire start only on the patcher canvas, never on detached node or child utility windows. */
bool connectionUiOnPatcherCanvas(const Node* n, const SDL_Event& e) {
    if (!n || !n->ne)
        return false;
    const uint32_t wid = getEventWindowID(e);
    return wid != 0 && wid == n->ne->getWindowID();
}
} // namespace

json Node::serialize() {
    json j;

    j["id"] = id;
    j["name"] = name;
    j["zoomRatio"] = zoomRatio;
    j["nodeType"] = nodeType;
    j["x"] = dstRect.x;
    j["y"] = dstRect.y;
    j["extra"] = extraSerialize();
    j["params"] = json::array();
    for (auto* p : params) {
        j["params"].push_back(p ? p->value : 0.0f);
    }
    j["modulators"] = json::array();
    for (size_t pi = 0; pi < params.size(); ++pi) {
        auto* p = params[pi];
        if (!p) continue;
        for (auto* m : p->modulators) {
            if (!m) continue;
            json jm;
            jm["paramIndex"] = pi;
            jm["centered"] = m->centered;
            jm["depth"] = m->depth;
            jm["sourceConnectionID"] = m->sourceConnection ? m->sourceConnection->id : -1;
            jm["sourceLabel"] = (m->sourceConnection && !m->sourceConnection->label.empty())
                ? m->sourceConnection->label
                : "";
            j["modulators"].push_back(jm);
        }
    }

    return j;
}

Node* Node::deSerialize(json j, NodeManager* nm) {
    int id = j["id"];

    Node* n = byType(j["nodeType"].get<NodeType>(), id, nm);

    n->name = j["name"];

    n->zoom(j["zoomRatio"].get<float>()/n->zoomRatio);

    n->move(j["x"], j["y"]);

    n->extraDeSerialize(j["extra"]);

    if (j.contains("params")) {
        const auto& jp = j["params"];
        for (size_t i = 0; i < n->params.size() && i < jp.size(); ++i) {
            n->params[i]->value = jp[i].get<float>();
        }
    }

    if (j.contains("modulators")) {
        for (const auto& jm : j["modulators"]) {
            const size_t paramIndex = jm.value("paramIndex", static_cast<size_t>(std::numeric_limits<size_t>::max()));
            if (paramIndex >= n->params.size()) continue;
            auto* p = n->params[paramIndex];
            if (!p) continue;

            int sourceID = jm.value("sourceConnectionID", -1);
            if (sourceID < 0) continue;

            auto* c = new Connection;
            c->nm = n->inputs.nm;
            c->id = static_cast<uint16_t>(sourceID);
            c->type = DataType::Waveform;
            c->dir = Direction::input;
            c->is_connected = false;
            c->output_connection = c->id;
            c->output_node = n->inputs.nodeID;
            c->input_connection = -1;
            c->input_node = -1;
            c->events = nullptr;
            c->buffer = nullptr;
            c->bufferSize = 0;
            c->label = jm.value("sourceLabel", std::string{});

            n->inputs.connections.push_back(c);
            n->inputs.id_pool.reserveID(c->id);
            n->inputs.ids[c->id] = static_cast<uint16_t>(n->inputs.connections.size() - 1);

            const bool centered = jm.value("centered", true);
            const float depth = jm.value("depth", 0.5f);
            p->addModulator(new Modulator(c->buffer, centered, depth, c));
        }
    }
    n->makeConnectionRects();

    return n;
}

Node::Node(uint16_t id, NodeManager* nm, NodeType nt) : 
    id(id),
    project(nm->project),
    nm(nm),
    nodeType(nt),
    isAltPressed(WindowHandler::instance()->isAltPressed),
    isCtrlPressed(WindowHandler::instance()->isCtrlPressed) {
    outputs.nodeID = id;
    inputs.nodeID = id;
    outputs.nm = nm;
    inputs.nm = nm;
}

Node::~Node() {
    if (detached) {
        if (renderer) {
            SDL_DestroyRenderer(renderer);
            renderer = nullptr;
        }
        if (window) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
        WindowHandler::instance()->removeWindow(this);
        detached = false;
    }
    if (texture_detached) {
        SDL_DestroyTexture(texture_detached);
        texture_detached = nullptr;
    }
    if (texture) SDL_DestroyTexture(texture);
    if (vx) delete[] vx;
    if (vy) delete[] vy;
}

connectionSet::~connectionSet() {
    for (auto c : connections) {
        if(c->dir == Direction::output) {
            switch (c->type) {
                case DataType::Events:
                    if (c->events) delete c->events;
                    break;
                case DataType::Waveform:
                    if (c->buffer) delete[] c->buffer;
                    break;
            }
        } // don't delete input connection's data
        delete c;
    }
}

bool Node::handleInput(SDL_Event& e) {
    // Ctrl+wheel is editor-level; nodes must not consume it.
    if (e.type == SDL_EVENT_MOUSE_WHEEL && isCtrlPressed) {
        return false;
    }

    // Detached embedded preview: editor-side interactions only.
    if (detached) {
        msX = (*mouseX - dstRect.x) / zoomRatio;
        msY = (*mouseY - dstRect.y) / zoomRatio;
        const bool insideNode = inPolygon(vx, vy, vCount, msX, msY);
        bool hoverFound = false;
        for (auto conn : inputs.connections) {
            if (MouseOn(&conn->rect)) {
                hoverFound = true;
                hoveredConnection = conn->id;
                hoveredDirection = Direction::input;
                break;
            }
        }
        if (!hoverFound) {
            for (auto conn : outputs.connections) {
                if (MouseOn(&conn->rect)) {
                    hoverFound = true;
                    hoveredConnection = conn->id;
                    hoveredDirection = Direction::output;
                    break;
                }
            }
        }
        if (!hoverFound) hoveredConnection = -1;

        if (!insideNode && !hoverFound) return false;
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
            clickMouse(e);
            return true;
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT && insideNode) {
            clickMouse(e);
            return true;
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT && hoverFound) {
            if (!connectionUiOnPatcherCanvas(this, e))
                return false;
            clickMouse(e);
            return true;
        }
        if (e.type == SDL_EVENT_MOUSE_WHEEL && isAltPressed) {
            zoom(std::pow(1.1, e.wheel.y));
            return true;
        }
        // Do not swallow wheel/other events over embedded preview; editor shortcuts must win.
        return false;
    }

    bool handled = false;

    msX = (*mouseX - dstRect.x) / zoomRatio;
    msY = (*mouseY - dstRect.y) / zoomRatio;

    if (inPolygon(vx, vy, vCount, msX, msY)) {
        handled = true;
        hoveredConnection = -1;
    } else {
        bool hoverFound = false;

        // check if the mouse is hovering over any of the connectors           

        for (auto conn : inputs.connections) {
            if (MouseOn(&conn->rect)) {
                hoverFound = true;
                hoveredConnection = conn->id;
                hoveredDirection = Direction::input;
                break;
            }
        }

        if (!hoverFound) {
            for (auto conn : outputs.connections) {
                if (MouseOn(&conn->rect)) {
                    hoverFound = true;
                    hoveredConnection = conn->id;
                    hoveredDirection = Direction::output;
                    break;
                }
            }
        }

        if (hoverFound) {
            handled = true;
        } else {
            hoveredConnection = -1;
        }
    }
    
    if (handled) {
        switch (e.type) {
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                clickMouse(e);
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                if (isAltPressed) {
                    zoom(std::pow(1.1, e.wheel.y));
                    return true;
                }
                break;
            default:
                break;
        }
        handleWindowInput(e);
    }

    return handled;
}

void Node::clickMouse(SDL_Event& e) {
    SDL_Window* eventWindow = SDL_GetWindowFromID(getEventWindowID(e));
    const bool clickFromDetachedWindow = detached && eventWindow == window;

    if (e.button.button == SDL_BUTTON_LEFT) {
        if (ne && (!detached || !clickFromDetachedWindow)) ne->setMovingNode(this);
        if (hoveredConnection != -1 && connectionUiOnPatcherCanvas(this, e)) {
            switch (hoveredDirection) {
                case Direction::input:
                    if (ne) ne->setDstConn(this, hoveredConnection);
                    break;
                case Direction::output:
                    if (ne) ne->setSrcConn(this, hoveredConnection);
                    break;
            }
        }

        auto time = SDL_GetTicks();
        auto interval = time - lastLeftClick;
        lastLeftClick = time;
        bool overParam = false;
        for (auto* p : params) {
            if (inPolygon(p->vx.data(), p->vy.data(), p->vx.size(), msX, msY)) {
                overParam = true;
                break;
            }
        }
        if(interval < DCT && !overParam && !blocksDoubleClick(msX, msY) && !clickFromDetachedWindow) {
            if (detached) attach();
            else detach();
            if (ne) ne->releaseMovingNode();
            return;
        }
    } else if (e.button.button == SDL_BUTTON_RIGHT) {
        SDL_Window* eventWindow = SDL_GetWindowFromID(getEventWindowID(e));
        const bool clickFromDetachedWindow = detached && eventWindow == window;
        auto* ctxMenu = ContextMenu::get();
        ctxMenu->active = true;
        if (clickFromDetachedWindow) {
            ctxMenu->window_id = SDL_GetWindowID(window);
            ctxMenu->renderer = renderer;
        } else {
            ctxMenu->window_id = SDL_GetWindowID(ne->getWindow());
            ctxMenu->renderer = ne->getRenderer();
        }

        Parameter* hoveredParam = nullptr;
        for (auto* p : params) {
            if (inPolygon(p->vx.data(), p->vy.data(), p->vx.size(), msX, msY)) {
                hoveredParam = p;
                break;
            }
        }

        if (hoveredParam) {
            ctxMenu->locX = clickFromDetachedWindow ? msX : *mouseX;
            ctxMenu->locY = clickFromDetachedWindow ? msY : *mouseY;
            auto t = getParameterMenu(hoveredParam);
            ctxMenu->dynamicTick = getTreeMenuTicker(t);
        } else if (hoveredConnection != -1) {
            if (!connectionUiOnPatcherCanvas(this, e)) {
                ctxMenu->active = false;
                return;
            }
            ctxMenu->locX = clickFromDetachedWindow ? msX : *mouseX;
            ctxMenu->locY = clickFromDetachedWindow ? msY : *mouseY;

            Connection* c;
            switch (hoveredDirection) {
                case Direction::input:
                    c = inputs.getConnection(hoveredConnection);
                    break;
                case Direction::output:
                    c = outputs.getConnection(hoveredConnection);
                    break;
            }

            auto t = getConnectionMenu(c);

            ctxMenu->dynamicTick = getTreeMenuTicker(t);
        } else {
            if (clickFromDetachedWindow) {
                ctxMenu->active = false;
                return;
            }
            ctxMenu->locX = *mouseX;
            ctxMenu->locY = *mouseY;
        
            auto t = getNodeMenu();
            ctxMenu->dynamicTick = getTreeMenuTicker(t);
        }
    }
}

std::shared_ptr<TreeEntry> Node::getNodeMenu() {
    auto t = uTreeEntry();
    t->label = "Node Menu";

    if (this->id) { // if there is no id then this is the output node which should not be deleted
        auto remove = uTreeEntry();
        remove->label = "Remove Node";
        remove->click = [this] () { nm->removeNode(this); };
        t->addChild(remove);
    }

    return t;
}

std::shared_ptr<TreeEntry> Node::getConnectionMenu(Connection* c) {
    auto t = uTreeEntry();
    t->label = "Connection Menu";
    if (!c) return t;

    auto sever = uTreeEntry();
    sever->label = "Sever Connection";
    sever->click = [c, this]() {this->nm->severConnection(c); };

    t->addChild(sever);

    return t;
}

void Node::addModSource(Parameter* p) {
    if (!p) return;
    if (!nm || !project || !project->um) return;

    size_t paramIndex = std::numeric_limits<size_t>::max();
    for (size_t i = 0; i < params.size(); ++i) {
        if (params[i] == p) {
            paramIndex = i;
            break;
        }
    }
    if (paramIndex == std::numeric_limits<size_t>::max()) return;
    std::vector<int> managerPath = nm ? nm->managerPath : std::vector<int>{};
    auto* pa = new AddModSourceUndoAction(project, std::move(managerPath), static_cast<int>(id), paramIndex);
    pa->name = "Add Mod Source";
    project->um->newAction(pa);
}

bool Node::addModSourceNow(size_t paramIndex) {
    if (!nm) return false;
    if (paramIndex >= params.size()) return false;
    Parameter* p = params[paramIndex];
    if (!p) return false;

    bool changed = false;
    {
        auto* c = new Connection;
        c->type = DataType::Waveform;
        c->dir = Direction::input;
        inputs.addConnection(c);
        std::string paramName = "Param";
        if (auto* k = dynamic_cast<Knob*>(p)) {
            if (!k->label.empty()) paramName = k->label;
        }
        c->label = paramName + " " + std::to_string(c->id);
        p->addModulator(new Modulator(c->buffer, true, 0.5f, c));
        makeConnectionRects();
        nm->markTopologyDirty();
        changed = true;
    }
    return changed;
}

void Node::removeModSource(Parameter* p, size_t modIndex) {
    if (!p || modIndex >= p->modulators.size()) return;
    if (!nm || !project || !project->um) return;

    size_t paramIndex = std::numeric_limits<size_t>::max();
    for (size_t i = 0; i < params.size(); ++i) {
        if (params[i] == p) {
            paramIndex = i;
            break;
        }
    }
    if (paramIndex == std::numeric_limits<size_t>::max()) return;
    std::vector<int> managerPath = nm ? nm->managerPath : std::vector<int>{};
    auto* pa = new RemoveModSourceUndoAction(project, std::move(managerPath), static_cast<int>(id), paramIndex, modIndex);
    pa->name = "Remove Mod Source";
    project->um->newAction(pa);
}

bool Node::removeModSourceNow(size_t paramIndex, size_t modIndex) {
    if (!nm) return false;
    if (paramIndex >= params.size()) return false;
    Parameter* p = params[paramIndex];
    if (!p || modIndex >= p->modulators.size()) return false;

    bool changed = false;
    {
        if (modIndex >= p->modulators.size()) return false;
        Modulator* m = p->modulators[modIndex];
        if (!m) return false;

        Connection* target = m->sourceConnection;
        if (target) {
            if (target->is_connected) return false;

            auto it = std::find(inputs.connections.begin(), inputs.connections.end(), target);
            if (it != inputs.connections.end()) {
                inputs.id_pool.releaseID(target->id);
                inputs.ids.erase(target->id);
                delete target;
                inputs.connections.erase(it);
                inputs.ids.clear();
                for (size_t i = 0; i < inputs.connections.size(); ++i) {
                    inputs.ids[inputs.connections[i]->id] = static_cast<uint16_t>(i);
                }
                makeConnectionRects();
                nm->markTopologyDirty();
            }
        }

        delete m;
        p->modulators.erase(p->modulators.begin() + static_cast<ptrdiff_t>(modIndex));
        changed = true;
    }
    return changed;
}

std::shared_ptr<TreeEntry> Node::getParameterMenu(Parameter* p) {
    auto root = uTreeEntry();
    root->label = "Parameter Menu";

    auto setValue = uTreeEntry();
    setValue->label = "Set Value";
    setValue->click = [this, p]() {
        auto* ctxMenu = ContextMenu::get();
        ctxMenu->active = true;
        ctxMenu->window_id = SDL_GetWindowID(detached ? window : ne->getWindow());
        ctxMenu->renderer = detached ? renderer : ne->getRenderer();
        ctxMenu->dynamicTick = getTextInputTicker([p](std::string text) {
            try {
                const float v = std::stof(text);
                p->value = std::clamp(v, 0.0f, 1.0f);
            } catch (...) {
            }
        });
    };
    root->addChild(setValue);

    auto resetValue = uTreeEntry();
    resetValue->label = "Reset Value";
    resetValue->click = [p]() {
        p->value = p->defaultValue;
    };
    root->addChild(resetValue);

    auto copyValue = uTreeEntry();
    copyValue->label = "Copy Value";
    copyValue->click = [p]() {
        std::ostringstream ss;
        ss << p->value;
        SDL_SetClipboardText(ss.str().c_str());
    };
    root->addChild(copyValue);

    auto modMatrix = uTreeEntry();
    modMatrix->label = "Edit Modulation Matrix";
    modMatrix->click = [this, p]() {
        auto* ctxMenu = ContextMenu::get();
        ctxMenu->keepOpenOnNextTreeLeafClick = true;
        ctxMenu->active = true;
        ctxMenu->window_id = SDL_GetWindowID(detached ? window : ne->getWindow());
        ctxMenu->renderer = detached ? renderer : ne->getRenderer();
        ctxMenu->dynamicTick = getModulationMatrixTicker(this, p);
    };
    root->addChild(modMatrix);

    return root;
}

bool inside(float& mouseX, float& mouseY, SDL_FRect* rect) {
    return (
        mouseX > rect->x &&
        mouseX < rect->x + rect->w &&
        mouseY > rect->y &&
        mouseY < rect->y + rect->h
    );
}

Node* Node::getNodeInput(Connection* con) {
    if (!con->is_connected) return nullptr;
    auto n = nm->getNode(con->input_node);
    return n;
}

uint16_t connectionSet::getIndex(uint16_t id) {
    auto it = ids.find(id);
    if (it == ids.end()) return std::numeric_limits<uint16_t>::max();
    return it->second;
}

Connection* connectionSet::getConnection(uint16_t id) {
    auto index = getIndex(id);
    if (index == std::numeric_limits<uint16_t>::max()) return nullptr;
    if (index >= connections.size()) return nullptr;
    auto con = connections[index];
    return con;
}

void connectionSet::addConnection(Connection* c) {
    c->nm = nm;
    c->id = id_pool.newID();
    connections.push_back(c);
    ids[c->id] = connections.size() -1;

    if (c->dir == Direction::input) {
        c->output_connection = c->id;
        c->output_node = nodeID;
        c->events = nullptr;
        c->buffer = nullptr;
    } else {
        if (c->type == DataType::Events) {
            c->events = new std::vector<Event>;
        } else {
            c->buffer = new float[bufferSize]; 
            c->bufferSize = bufferSize;
            if (bufferSize > 0) {
                std::memset(c->buffer, 0, static_cast<size_t>(bufferSize) * sizeof(float));
            }
        }
    }
}

void Node::resize(float rx, float ry) {

    float new_w = dstRect.w + rx;
    float new_h = dstRect.h + ry;

    if (new_w > new_h) {
        zoom((new_w / TEX_W)/zoomRatio);
    } else {
        zoom((new_h / TEX_H)/zoomRatio);

    } 

    zoom(1.0f);
}

bool Node::canZoom(float amount) {
    return zoomRatio * amount >= 0.01;
}

void Node::zoom(float amount) {
 
    if (!canZoom(amount)) return;

    zoomRatio *= amount;

    dstRect.w = TEX_W * zoomRatio;
    dstRect.h = TEX_H * zoomRatio;
    
    makeConnectionRects();
}

void Node::makeConnectionRects() {
    const PortDisplayMode mode = nm ? nm->portDisplayMode : PortDisplayMode::RectLabels;
    float dy = 2;
    float w = (mode == PortDisplayMode::RectLabels) ? 18.0f : 12.0f;
    float h = (mode == PortDisplayMode::RectLabels) ? 64.0f : 12.0f;

    SDL_FRect connRect{
        dstRect.x + dstRect.w / 2 - inputs.connections.size() * w/2, dstRect.y - h - dy,
        w, h
    };

    int inIndex = 0;
    for (auto conn : inputs.connections) {
        conn->rect = connRect;
        conn->displayIndex = inIndex++;
        connRect.x += w;
    }

    connRect.x = dstRect.x + dstRect.w / 2 - outputs.connections.size() * w/2,
    connRect.y = dstRect.y + dstRect.h + dy;

    int outIndex = 0;
    for (auto conn : outputs.connections) {
        conn->rect = connRect;
        conn->displayIndex = outIndex++;
        connRect.x += w;
    }
}

void Node::move(float x, float y) {
    dstRect.x = x;
    dstRect.y = y;
    resize(0, 0);
}

SDL_FRect Connection::srcRect() {
    if (!nm) return SDL_FRect{0, 0, 0, 0};
    Node* n = nm->getNode(input_node);
    if (!n) return SDL_FRect{0, 0, 0, 0};
    connectionSet& outputs = n->outputs;
    auto conn = outputs.getConnection(input_connection);
    if (!conn) return SDL_FRect{0, 0, 0, 0};
    return conn->rect;
}

void Node::renderContent(SDL_Renderer* renderer) {
    if (!vCount) {
        vCount = 4;
        vx = new float[vCount];
        vy = new float[vCount];

        vx[0] = 0;
        vx[1] = TEX_W;
        vx[2] = TEX_W-300;
        vx[3] = 300;

        vy[0] = 0;
        vy[1] = 0;
        vy[2] = TEX_H;
        vy[3] = TEX_H;
    }

    filledPolygonRGBA(renderer, vx, vy, vCount, 255, 255, 255, 255);
    aapolygonRGBA(renderer, vx, vy, vCount, 0, 0, 0, 255);

    renderParams(renderer);
}

void Node::renderContentHelper(SDL_Renderer* renderer) {
    if (!ne || !ne->renderer) return;
    SDL_FRect tRect{0,0,TEX_W,TEX_H};

    if (!detached) {
        if (!texture) return;
        auto target = SDL_GetRenderTarget(renderer);
        SDL_SetRenderTarget(renderer, texture);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
        renderContent(renderer);
        SDL_SetRenderTarget(renderer, target);
        SDL_RenderTexture(ne->renderer, texture, &tRect, &dstRect);
        return;
    }

    // Detached mode: render live content to detached texture/window.
    if (texture_detached) {
        auto target = SDL_GetRenderTarget(renderer);
        SDL_SetRenderTarget(renderer, texture_detached);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
        renderContent(renderer);
        SDL_SetRenderTarget(renderer, target);
        SDL_RenderTexture(renderer, texture_detached, &tRect, NULL);
    }

    // Embedded view: keep drawing frozen snapshot from the last attached frame.
    if (texture) {
        SDL_RenderTexture(ne->renderer, texture, &tRect, &dstRect);
    } else {
        SDL_SetRenderDrawColor(ne->renderer, 80, 80, 80, 255);
        SDL_RenderFillRect(ne->renderer, &dstRect);
    }
}

void Node::render() {
    // RTT/content: prefer the node's renderer (owns textures with the embedding context); fallback to editor.
    SDL_Renderer* texR = detached ? renderer : (renderer ? renderer : ((ne && ne->renderer) ? ne->renderer : nullptr));
    if (!texR)
        return;

    if (!detached && !texture) {
        texture = SDL_CreateTexture(texR, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, TEX_W, TEX_H);
    } else if (detached && !texture_detached) {
        if (!renderer) return;
        texture_detached = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, TEX_W, TEX_H);
    }

    renderContentHelper(texR);

    // Ports and connected-socket cable previews always use the NodeEditor for this node's graph — e.g. a patcher's
    // mainEditor window for inner nodes, root editor for top-level nodes — never the node's own detached window/renderer.
    SDL_Renderer* portR = nullptr;
    if (nm && nm->ne && nm->ne->renderer)
        portR = nm->ne->renderer;
    else if (ne && ne->renderer)
        portR = ne->renderer;
    else
        portR = texR;
    if (!portR)
        return;

    for (auto* conn : inputs.connections) {
        if (conn) conn->render(portR, conn->id == hoveredConnection);
    }
    for (auto* conn : outputs.connections) {
        if (conn) conn->render(portR, conn->id == hoveredConnection);
    }
}

void Connection::render(SDL_Renderer* renderer, bool hover) {
    if (!renderer) return;

    const PortDisplayMode mode = nm ? nm->portDisplayMode : PortDisplayMode::RectLabels;
    SDL_Color c{128, 128, 128, 255};

    switch (type) {
        case DataType::Events:
            if (is_connected) c = {160, 255, 160, 255};
            else c = {120, 255, 120, 255};
            break;
        case DataType::Waveform:
            if (is_connected) c = {255, 160, 160, 255};
            else c = {255, 120, 120, 255};
            break;
        default:
            break;
    }

    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);

    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, 120,120,120,255);
    SDL_RenderRect(renderer, &rect);

    if (fonts.mainFont) {
        std::string text;
        if (mode == PortDisplayMode::SquareIDs) {
            text = std::to_string(id);
        } else {
            const std::string fallback = std::string(dir == Direction::input ? "Input " : "Output ") + std::to_string(id);
            text = label.empty() ? fallback : label;
            if (text.size() > 8) {
                text = text.substr(0, 8);
            }
        }
        if (!text.empty()) {
            SDL_Color textColor{10, 10, 10, 255};
            SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, text.c_str(), 0, textColor);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
                if (tex) {
                    const float maxW = rect.w * 0.86f;
                    const float maxH = rect.h * 0.86f;
                    const float baseScale = std::min(
                        1.0f,
                        std::min(maxW / static_cast<float>(surf->w),
                                 maxH / static_cast<float>(surf->h))
                    );
                    const float scale = (mode == PortDisplayMode::SquareIDs)
                        ? std::max(0.1f, baseScale)
                        : std::max(0.1f, baseScale * labelScale);
                    const float drawW = static_cast<float>(surf->w) * scale;
                    const float drawH = static_cast<float>(surf->h) * scale;
                    const float cx = rect.x + rect.w * 0.5f;
                    const float cy = rect.y + rect.h * 0.5f;
                    SDL_FRect textRect{cx - drawW * 0.5f, cy - drawH * 0.5f, drawW, drawH};
                    if (mode == PortDisplayMode::RectLabels) {
                        SDL_FPoint center{drawW * 0.5f, drawH * 0.5f};
                        SDL_RenderTextureRotated(renderer, tex, nullptr, &textRect, -90.0, &center, SDL_FLIP_NONE);
                    } else {
                        SDL_RenderTexture(renderer, tex, nullptr, &textRect);
                    }
                    SDL_DestroyTexture(tex);
                }
                SDL_DestroySurface(surf);
            }
        }
    }

    if (!is_connected || dir == Direction::output) return;

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    auto src = srcRect();
    if (src.w == 0.0f && src.h == 0.0f) return;

    SDL_FColor color;
    if (type == DataType::Events) color = {0.5f, 1.0f, 0.5f, 1.0f};
    else color = {1.0f, 0.5f, 0.5f, 1.0f};

    NodeEditor::renderPatchCable(renderer, rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f,
        src.x + src.w * 0.5f, src.y + src.h * 0.5f, color);
}

void Node::renderParams(SDL_Renderer* renderer) {
    for (auto p : params) p->render(renderer);
}

void Node::setup() {}

void Node::update(int bufferSize, int sampleRate) {
    this->bufferSize = bufferSize;
    this->sampleRate = sampleRate;

    outputs.bufferSize = bufferSize;
    for(auto c : outputs.connections) {
        if (c->type == DataType::Waveform) {
            if(c->buffer != nullptr) {
                delete[] c->buffer;
            }
            c->buffer = new float[bufferSize];
            c->bufferSize = bufferSize;
            if (bufferSize > 0) {
                std::memset(c->buffer, 0, static_cast<size_t>(bufferSize) * sizeof(float));
            }
        }
    }

    inputs.bufferSize = bufferSize;
    setup();
}

void Node::relinkInputs() {
    inputs.bufferSize = bufferSize;
    for (auto c : inputs.connections) {
        if (!c->is_connected) {
            if (c->type == DataType::Waveform) {
                c->buffer = nullptr;
                c->bufferSize = bufferSize;
            } else {
                c->events = nullptr;
            }
            continue;
        }

        auto n = nm->getNode(c->input_node);
        if (!n) {
            c->is_connected = false;
            if (c->type == DataType::Waveform) {
                c->buffer = nullptr;
                c->bufferSize = bufferSize;
            } else {
                c->events = nullptr;
            }
            continue;
        }

        auto ci = n->outputs.getConnection(c->input_connection);
        if (!ci) {
            c->is_connected = false;
            if (c->type == DataType::Waveform) {
                c->buffer = nullptr;
                c->bufferSize = bufferSize;
            } else {
                c->events = nullptr;
            }
            continue;
        }

        if (c->type == DataType::Waveform) {
            c->buffer = ci->buffer;
            c->bufferSize = bufferSize;
        } else {
            c->events = ci->events;
        }
    }
}

bool Node::depends(Node* d) {
    if (d == this) return true;
    for (Connection * c : inputs.connections)
        if (Node* n = getNodeInput(c))
            if (n == d || n->depends(d)) return true;
    return false;
}

void Node::processTree() {
    if (isProcessed) return;
    // process prerequisites
    for (Connection * c : inputs.connections) {
        Node* n = getNodeInput(c);
        if (n) n->processTree();
    }

    // process final
    process();
    isProcessed = true;
}

void Node::resetProcessTree() {
    if (!isProcessed) return;

    for (Connection * c : inputs.connections) {
        Node* n = getNodeInput(c);
        if (n) n->resetProcessTree();
    }

    isProcessed = false;
}

void Node::detach() {
    if (!detached) {
        window = SDL_CreateWindow(name.c_str(), TEX_W, TEX_H, SDL_WINDOW_RESIZABLE | SDL_WINDOW_UTILITY);
        if (ne && ne->window) {
            const SDL_WindowFlags hostFlags = SDL_GetWindowFlags(ne->window);
            if ((hostFlags & SDL_WINDOW_HIDDEN) == 0) {
                SDL_SetWindowParent(window, ne->window);
            }
        }
        renderer = SDL_CreateRenderer(window, NULL);
        WindowHandler::instance()->addWindow(this);
    }
    detached = true;

    clearParamTextures();
    clearCustomTextures();

    detachFinal();
}

void Node::clearTextures() {
    if (texture_detached) SDL_DestroyTexture(texture_detached);
    if (texture) SDL_DestroyTexture(texture);
    texture_detached = nullptr;
    texture = nullptr;
    clearParamTextures();
    clearCustomTextures();
}

void Node::attach() {
    if (detached) {
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        WindowHandler::instance()->removeWindow(this);
    }
    if (texture_detached) SDL_DestroyTexture(texture_detached);

    window = ne->window;
    renderer = ne->renderer;
    texture_detached = nullptr;
    clearParamTextures();
    clearCustomTextures();

    detached = false;

    mouseX = &(ne->mouseX);
    mouseY = &(ne->mouseY);

    attachFinal();
}

void Node::handleWindowInput(SDL_Event& e) {
    if (!detached) {
        for (auto p : params) {
            if (inPolygon(p->vx.data(), p->vy.data(), p->vx.size(), msX, msY)) {
                p->handleInput(e);
            }
        }
        handleCustomInput(e);
        return;
    }

    if (detached && SDL_GetWindowFromID(getEventWindowID(e)) == window) {
        float gx, gy;
        SDL_GetGlobalMouseState(&gx, &gy);
        int wx, wy;
        SDL_GetWindowPosition(window, &wx, &wy);
        msX = gx - wx;
        msY = gy - wy;

        hoveredConnection = -1;
        bool handled = inPolygon(vx, vy, vCount, msX, msY);

        for (auto p : params) {
            if (inPolygon(p->vx.data(), p->vy.data(), p->vx.size(), msX, msY)) {
                p->handleInput(e);
                handled = true;
            }
        }

        if (handled && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            clickMouse(e);
        }

        handleCustomInput(e);
    }

}

void Node::clearParamTextures() {
    for (auto p : params) p->clearTextures();
}

void Node::setNE(NodeEditor* ne) {
    this->ne = ne;
    mouseX = &(ne->mouseX);
    mouseY = &(ne->mouseY);

    attach();

    setNEFinal();
}

void Node::resetNE() {
    if (detached) {
        if (renderer) {
            SDL_DestroyRenderer(renderer);
            renderer = nullptr;
        }
        if (window) {
            SDL_DestroyWindow(window);
            window = nullptr;
        }
        WindowHandler::instance()->removeWindow(this);
    }
    if (texture_detached) {
        SDL_DestroyTexture(texture_detached);
        texture_detached = nullptr;
    }
    clearParamTextures();
    clearCustomTextures();
    if (texture) {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }

    ne = nullptr;
    // Borrowed from NodeEditor while attached; once ne is cleared they must not be used for rendering.
    if (!detached) {
        window = nullptr;
        renderer = nullptr;
    }

    resetNEFinal();
}

