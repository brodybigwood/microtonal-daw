#include "Node.h"
#include "NodeManager.h"
#include "SDL_Events.h"
#include "ContextMenu.h"
#include <iostream>
#include "NodeEditor.h"
#include "WindowHandler.h"
#include "Preferences.h"
#include "Settings.h"
#include "UndoManager.h"
#include <cstring>
#include <limits>
#include <sstream>
#include <iomanip>
#include "styles.h"

namespace {
std::function<bool(SDL_Event&, float, float, SDL_Renderer*, std::shared_ptr<TreeEntry>)> createMatrixTicker(Node* node, Parameter* p, std::vector<size_t> path) {
    struct State {
        int draggingIndex = -1;
        float oldDepth = 0.0f;
    };
    auto state = std::make_shared<State>();

    auto snapValue = [](float v, float minV, float maxV) {
        const float step = std::max(1e-6f, (maxV - minV) * 0.01f);
        const float snapped = std::round(v / step) * step;
        return std::clamp(snapped, minV, maxV);
    };

    return [node, p, state, snapValue, path](SDL_Event& e, float panelX, float panelY, SDL_Renderer* renderer, std::shared_ptr<TreeEntry> self) {
        if (!renderer || !p) return false;

        const float panelW = 520.0f;
        const float headerH = 30.0f;
        const float rowH = 34.0f;
        const float footerH = 36.0f;
        const float panelH = headerH + rowH * static_cast<float>(std::max<size_t>(1, p->modulators.size())) + footerH + 12.0f;
        SDL_FRect panel{panelX, panelY, panelW, panelH};

        if (self) self->customHeight = panelH;

        if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
            if (state->draggingIndex >= 0 && state->draggingIndex < static_cast<int>(p->modulators.size())) {
                auto* mod = p->modulators[static_cast<size_t>(state->draggingIndex)];
                if (mod && mod->depth.value != state->oldDepth && node->project && node->project->um) {
                    std::vector<int> mgrPath = node->nm ? node->nm->managerPath : std::vector<int>{};
                    std::vector<size_t> depthPath = path;
                    depthPath.push_back(static_cast<size_t>(state->draggingIndex));
                    auto* pa2 = new SetParamValueUndoAction(node->project, std::move(mgrPath), static_cast<int>(node->id),
                        depthPath, state->oldDepth, mod->depth.value, "Change Mod Depth");
                    node->project->um->newAction(pa2);
                }
            }
            state->draggingIndex = -1;
        }

        SDL_SetRenderDrawColor(renderer, 220, 220, 220, 255);
        SDL_RenderFillRect(renderer, &panel);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderRect(renderer, &panel);

        if (fonts.mainFont) {
            std::string titleStr = path.empty() ? "Modulation Matrix" : ("Modulation Matrix - " + node->parameterPathLabel(path));
            SDL_Surface* titleSurf = TTF_RenderText_Blended(fonts.mainFont, titleStr.c_str(), 0, SDL_Color{0, 0, 0, 255});
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

            m->depth.value = std::clamp(m->depth.value, minDepth, maxDepth);
            const float t = (m->depth.value - minDepth) / range;
            const float knobX = slider.x + t * slider.w;
            SDL_FRect knob{knobX - 4.0f, slider.y - 4.0f, 8.0f, slider.h + 8.0f};
            SDL_SetRenderDrawColor(renderer, 80, 120, 230, 255);
            SDL_RenderFillRect(renderer, &knob);

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                if (e.button.button == SDL_BUTTON_LEFT) {
                    if (MouseOn(&centerBtn)) {
                        bool oldCentered = m->centered;
                        float oldDepth2 = m->depth.value;
                        m->centered = !m->centered;
                        if (m->centered) {
                            m->depth.value = std::clamp(m->depth.value, -0.5f, 0.5f);
                        } else {
                            m->depth.value = std::clamp(m->depth.value, 0.0f, 1.0f);
                        }
                        if (node->project && node->project->um) {
                            std::vector<int> mgrPath = node->nm ? node->nm->managerPath : std::vector<int>{};
                            auto* pa2 = new ToggleModulatorCenteredUndoAction(node->project, std::move(mgrPath), static_cast<int>(node->id),
                                path, i, oldCentered, m->centered, oldDepth2, m->depth.value);
                            node->project->um->newAction(pa2);
                        }
                    } else if (MouseOn(&removeBtn)) {
                        node->removeModSource(path, i);
                        return true;
                    } else if (MouseOn(&slider)) {
                        state->draggingIndex = static_cast<int>(i);
                        state->oldDepth = m->depth.value;
                    }
                } else if (e.button.button == SDL_BUTTON_RIGHT && MouseOn(&slider)) {
                    std::vector<size_t> depthPath = path;
                    depthPath.push_back(i);
                    auto menu = node->getParameterMenu(&m->depth, depthPath);
                    if (self) {
                        self->children.clear();
                        for (auto& child : menu->children) {
                            self->addChild(child);
                        }
                    }
                    return true;
                }
            }

            if (state->draggingIndex == static_cast<int>(i) && (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_MOTION)) {
                float mx = 0.0f, my = 0.0f;
                SDL_GetMouseState(&mx, &my);
                const float norm = std::clamp((mx - slider.x) / slider.w, 0.0f, 1.0f);
                const float dMin = m->centered ? -0.5f : 0.0f;
                const float dMax = m->centered ? 0.5f : 1.0f;
                m->depth.value = snapValue(dMin + norm * (dMax - dMin), dMin, dMax);
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
                ds << std::fixed << std::setprecision(2) << m->depth.value;
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
            node->addModSource(path);
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
                    a = -m->depth.value;
                    b = m->depth.value;
                } else {
                    a = 0.0f;
                    b = m->depth.value;
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
} // namespace

static void serializeModulators(const std::vector<Modulator*>& modulators, json& arr) {
    for (auto* m : modulators) {
        if (!m) continue;
        json jm;
        jm["centered"] = m->centered;
        jm["depth"] = m->depth.value;
        jm["sourceConnectionID"] = m->sourceConnection ? m->sourceConnection->id : -1;
        jm["sourceLabel"] = (m->sourceConnection && !m->sourceConnection->label.empty())
            ? m->sourceConnection->label
            : "";
        if (!m->depth.modulators.empty()) {
            json nested = json::array();
            serializeModulators(m->depth.modulators, nested);
            jm["depthModulators"] = nested;
        }
        arr.push_back(jm);
    }
}

json Node::serialize() {
    json j;

    j["id"] = id;
    j["name"] = name;
    j["zoomRatio"] = zoomRatio;
    j["nodeType"] = nodeType;
    j["x"] = dstRect.x;
    j["y"] = dstRect.y;
    j["ewid"] = EmbeddedWindow::id;
    j["extra"] = extraSerialize();
    j["params"] = json::array();
    for (auto* p : params) {
        j["params"].push_back(p ? p->value : 0.0f);
    }
    j["modulators"] = json::array();
    for (size_t pi = 0; pi < params.size(); ++pi) {
        auto* p = params[pi];
        if (!p) continue;
        json arr = json::array();
        serializeModulators(p->modulators, arr);
        for (auto& jm : arr) {
            jm["paramIndex"] = pi;
            j["modulators"].push_back(jm);
        }
    }

    return j;
}

static void deserializeModulators(Parameter* p, const json& arr, Node* n) {
    for (const auto& jm : arr) {
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
        auto* mod = new Modulator(c->buffer, centered, generateRect(0, 0, 200, 10), depth, c);
        p->addModulator(mod);

        if (jm.contains("depthModulators")) {
            deserializeModulators(&mod->depth, jm["depthModulators"], n);
        }
    }
}

Node* Node::deSerialize(json j, NodeManager* nm) {
    int id = j["id"];

    Node* n = byType(j["nodeType"].get<NodeType>(), id, nm);

    n->name = j["name"];

    n->zoom(j["zoomRatio"].get<float>()/n->zoomRatio);

    n->move(j["x"], j["y"]);
    if (j.contains("ewid")) n->EmbeddedWindow::id = j["ewid"];

    n->extraDeSerialize(j["extra"]);

    if (j.contains("params")) {
        const auto& jp = j["params"];
        for (size_t i = 0; i < n->params.size() && i < jp.size(); ++i) {
            n->params[i]->value = jp[i].get<float>();
        }
    }

    if (j.contains("modulators")) {
        // Group modulators by paramIndex
        std::map<size_t, json> byParam;
        for (const auto& jm : j["modulators"]) {
            size_t pi = jm.value("paramIndex", static_cast<size_t>(0));
            if (!byParam.count(pi)) byParam[pi] = json::array();
            byParam[pi].push_back(jm);
        }
        for (auto& [pi, arr] : byParam) {
            if (pi >= n->params.size()) continue;
            auto* p = n->params[pi];
            if (!p) continue;
            deserializeModulators(p, arr, n);
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
    dstRect = {0, 0, TEX_W * zoomRatio, TEX_H * zoomRatio};
    w = dstRect.w;
    h = dstRect.h;
    outputs.nodeID = id;
    inputs.nodeID = id;
    outputs.nm = nm;
    inputs.nm = nm;
}

Node::~Node() {
    if (ne) {
        ne->unregisterEmbeddedWindow(this);
        ne->clearPointersToEmbeddedWindow(this);
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

bool Node::handleContentInput(SDL_Event& e) {
    // Ctrl+wheel is editor zoom; forward to inner patcher editors.
    // Return true if consumed by a patcher, false otherwise.
    if (e.type == SDL_EVENT_MOUSE_WHEEL && isCtrlPressed) {
        msX = (*mouseX - dstRect.x) / zoomRatio;
        msY = (*mouseY - dstRect.y) / zoomRatio;
        return handleCustomInput(e);
    }

    // File drops: use drop coordinates if valid, else fall back to mouse position.
    if (e.type == SDL_EVENT_DROP_FILE) {
        float dropX = e.drop.x > 0 ? e.drop.x : *mouseX;
        float dropY = e.drop.y > 0 ? e.drop.y : *mouseY;
        msX = (dropX - dstRect.x) / zoomRatio;
        msY = (dropY - dstRect.y) / zoomRatio;
        std::cout << "[Node] drop at screen=" << dropX << "," << dropY
                  << " tex=" << msX << "," << msY << std::endl;
        if (inPolygon(vx, vy, vCount, msX, msY) || captured_) {
            return handleCustomInput(e);
        }
        return false;
    }

    bool handled = false;

    msX = (*mouseX - dstRect.x) / zoomRatio;
    msY = (*mouseY - dstRect.y) / zoomRatio;

    if (inPolygon(vx, vy, vCount, msX, msY) || captured_) {
        handled = true;
        if (!captured_) hoveredConnection = -1;
    } else if (showConnectionPorts()) {
        bool hoverFound = false;

        const float mx = *mouseX, my = *mouseY;
        for (auto conn : inputs.connections) {
            if (mx >= conn->rect.x && mx <= conn->rect.x + conn->rect.w &&
                my >= conn->rect.y && my <= conn->rect.y + conn->rect.h) {
                hoverFound = true;
                hoveredConnection = conn->id;
                hoveredDirection = Direction::input;
                break;
            }
        }

        if (!hoverFound) {
            for (auto conn : outputs.connections) {
                if (mx >= conn->rect.x && mx <= conn->rect.x + conn->rect.w &&
                    my >= conn->rect.y && my <= conn->rect.y + conn->rect.h) {
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
                break;
            default:
                break;
        }
        handleWindowInput(e);
    }

    return handled;
}

void Node::clickMouse(SDL_Event& e) {

    if (e.button.button == SDL_BUTTON_LEFT) {
        if (hoveredConnection != -1 && ne) {
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

    } else if (e.button.button == SDL_BUTTON_RIGHT) {
        auto* ctxMenu = ContextMenu::get();

        Parameter* hoveredParam = nullptr;
        for (auto* p : params) {
            if (inPolygon(p->vx.data(), p->vy.data(), p->vx.size(), msX, msY)) {
                hoveredParam = p;
                break;
            }
        }

        if (hoveredParam) {
            ctxMenu->activate();
            size_t paramIndex = 0;
            for (size_t i = 0; i < params.size(); ++i) {
                if (params[i] == hoveredParam) { paramIndex = i; break; }
            }
            auto t = getParameterMenu(hoveredParam, {paramIndex});
            ctxMenu->dynamicTick = getTreeMenuTicker(t);
        } else if (hoveredConnection != -1) {
            if (!ne) {
                ctxMenu->active = false;
                return;
            }
            ctxMenu->activate();

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
            ctxMenu->activate();

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

Parameter* Node::resolveParameterPath(const std::vector<size_t>& path) {
    if (path.empty() || path[0] >= params.size()) return nullptr;
    Parameter* p = params[path[0]];
    for (size_t i = 1; i < path.size(); ++i) {
        size_t modIdx = path[i];
        if (modIdx >= p->modulators.size()) return nullptr;
        p = &p->modulators[modIdx]->depth;
    }
    return p;
}

std::string Node::parameterPathLabel(const std::vector<size_t>& path) const {
    if (path.empty()) return "Param";
    std::string label;
    Parameter* p = params[path[0]];
    if (auto* k = dynamic_cast<Knob*>(p)) {
        label = k->label.empty() ? "Param" : k->label;
    } else {
        label = "Param";
    }
    for (size_t i = 1; i < path.size(); ++i) {
        label += ".mod" + std::to_string(path[i]) + ".depth";
    }
    return label;
}

void Node::addModSource(Parameter* p) {
    if (!p) return;
    for (size_t i = 0; i < params.size(); ++i) {
        if (params[i] == p) {
            addModSource({i});
            return;
        }
    }
}

void Node::addModSource(const std::vector<size_t>& path) {
    if (!nm || !project || !project->um) return;
    if (path.empty() || path[0] >= params.size()) return;
    std::vector<int> managerPath = nm ? nm->managerPath : std::vector<int>{};
    auto* pa = new AddModSourceUndoAction(project, std::move(managerPath), static_cast<int>(id), path);
    pa->name = "Add Mod Source";
    project->um->newAction(pa);
}

bool Node::addModSourceNow(const std::vector<size_t>& path) {
    if (!nm) return false;
    Parameter* p = resolveParameterPath(path);
    if (!p) return false;

    bool changed = false;
    {
        auto* c = new Connection;
        c->type = DataType::Waveform;
        c->dir = Direction::input;
        inputs.addConnection(c);
        c->label = parameterPathLabel(path);
        p->addModulator(new Modulator(c->buffer, true, generateRect(0, 0, 200, 10), 0.5f, c));
        makeConnectionRects();
        nm->markTopologyDirty();
        changed = true;
    }
    return changed;
}

void Node::removeModSource(Parameter* p, size_t modIndex) {
    if (!p || modIndex >= p->modulators.size()) return;
    for (size_t i = 0; i < params.size(); ++i) {
        if (params[i] == p) {
            removeModSource({i}, modIndex);
            return;
        }
    }
}

void Node::removeModSource(const std::vector<size_t>& path, size_t modIndex) {
    if (!nm || !project || !project->um) return;
    if (path.empty() || path[0] >= params.size()) return;
    std::vector<int> managerPath = nm ? nm->managerPath : std::vector<int>{};
    auto* pa = new RemoveModSourceUndoAction(project, std::move(managerPath), static_cast<int>(id), path, modIndex);
    pa->name = "Remove Mod Source";
    project->um->newAction(pa);
}

bool Node::removeModSourceNow(const std::vector<size_t>& path, size_t modIndex) {
    if (!nm) return false;
    Parameter* p = resolveParameterPath(path);
    if (!p || modIndex >= p->modulators.size()) return false;

    Modulator* m = p->modulators[modIndex];
    if (!m) return false;

    // Sever any wired cables in the modulator tree, then remove.
    std::function<void(Modulator*)> severTree = [&](Modulator* mod) {
        for (auto* nested : mod->depth.modulators) severTree(nested);
        if (mod->sourceConnection && mod->sourceConnection->is_connected) {
            nm->severConnectionNow(
                static_cast<uint16_t>(mod->sourceConnection->output_node),
                static_cast<uint16_t>(mod->sourceConnection->output_connection),
                id, mod->sourceConnection->id);
            mod->sourceConnection->is_connected = false;
        }
    };
    severTree(m);

    // Remove connections from inputs.
    std::function<void(Modulator*)> removeConns = [&](Modulator* mod) {
        for (auto* nested : mod->depth.modulators) removeConns(nested);
        if (mod->sourceConnection) {
            auto it = std::find(inputs.connections.begin(), inputs.connections.end(), mod->sourceConnection);
            if (it != inputs.connections.end()) {
                inputs.id_pool.releaseID(mod->sourceConnection->id);
                inputs.ids.erase(mod->sourceConnection->id);
                delete mod->sourceConnection;
                inputs.connections.erase(it);
            }
        }
    };
    removeConns(m);

    inputs.ids.clear();
    for (size_t i = 0; i < inputs.connections.size(); ++i)
        inputs.ids[inputs.connections[i]->id] = static_cast<uint16_t>(i);
    makeConnectionRects();
    nm->markTopologyDirty();

    delete m;
    p->modulators.erase(p->modulators.begin() + static_cast<ptrdiff_t>(modIndex));
    return true;
}

std::shared_ptr<TreeEntry> Node::getParameterMenu(Parameter* p, const std::vector<size_t>& path) {
    auto root = uTreeEntry();
    root->label = "Parameter Menu";

    auto setValue = uTreeEntry();
    setValue->label = "Set Value";
    setValue->click = [this, p, path]() {
        auto* ctxMenu = ContextMenu::get();
        ctxMenu->activate();
        ctxMenu->dynamicTick = getTextInputTicker([this, p, path](std::string text) {
            try {
                const float v = std::clamp(std::stof(text), 0.0f, 1.0f);
                if (p->value != v && project && project->um) {
                    float oldV = p->value;
                    p->value = v;
                    std::vector<int> mgrPath = nm ? nm->managerPath : std::vector<int>{};
                    auto* pa = new SetParamValueUndoAction(project, std::move(mgrPath), static_cast<int>(id), path,
                        oldV, v, "Set Param Value");
                    project->um->newAction(pa);
                } else {
                    p->value = v;
                }
            } catch (...) {
            }
        });
    };
    root->addChild(setValue);

    auto resetValue = uTreeEntry();
    resetValue->label = "Reset Value";
    resetValue->click = [this, p, path]() {
        if (p->value != p->defaultValue && project && project->um) {
            float oldV = p->value;
            p->value = p->defaultValue;
            std::vector<int> mgrPath = nm ? nm->managerPath : std::vector<int>{};
            auto* pa = new SetParamValueUndoAction(project, std::move(mgrPath), static_cast<int>(id), path,
                oldV, p->defaultValue, "Reset Param Value");
            project->um->newAction(pa);
        } else {
            p->value = p->defaultValue;
        }
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
    modMatrix->customTick = createMatrixTicker(this, p, path);
    modMatrix->customWidth = 520.0f;
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
    const PortDisplayMode mode = static_cast<PortDisplayMode>(Settings::instance().portDisplayMode());
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

void Node::move(float mx, float my) {
    dstRect.x = mx;
    dstRect.y = my;
    x = mx;
    y = my;
    w = dstRect.w;
    h = dstRect.h;
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
    SDL_FRect tRect{0,0,TEX_W,TEX_H};

    auto target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, texture);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
    renderContent(renderer);
    SDL_SetRenderTarget(renderer, target);
    SDL_RenderTexture(renderer, texture, &tRect, &dstRect);
}

void Node::render(SDL_Renderer* renderer) {
    if (!renderer)
        return;

    if (!texture)
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, TEX_W, TEX_H);

    renderContentHelper(renderer);

    // Polygon outline, ports, and tooltip all render onto the same renderer.
    SDL_Renderer* portR = renderer;

    // Polygon outline on the canvas (not the texture), transformed to screen space.
    if (vCount >= 3 && vx && vy) {
        const float s = zoomRatio;
        std::vector<float> svx(vCount);
        std::vector<float> svy(vCount);
        for (size_t i = 0; i < vCount; ++i) {
            svx[i] = dstRect.x + vx[i] * s;
            svy[i] = dstRect.y + vy[i] * s;
        }

        // Glow pass — expand outward, low alpha.
        float cx = 0.f, cy = 0.f;
        for (size_t i = 0; i < vCount; ++i) { cx += svx[i]; cy += svy[i]; }
        cx /= static_cast<float>(vCount);
        cy /= static_cast<float>(vCount);
        constexpr float kGlow = 3.f;
        std::vector<float> gvx(vCount);
        std::vector<float> gvy(vCount);
        for (size_t i = 0; i < vCount; ++i) {
            float dx = svx[i] - cx;
            float dy = svy[i] - cy;
            float len = sqrtf(dx * dx + dy * dy);
            if (len > 0.001f) {
                gvx[i] = svx[i] + dx / len * kGlow;
                gvy[i] = svy[i] + dy / len * kGlow;
            } else {
                gvx[i] = svx[i];
                gvy[i] = svy[i];
            }
        }
        aapolygonRGBA(portR, gvx.data(), gvy.data(), static_cast<int>(vCount), 0, 0, 0, 60);

        // Crisp outline at exact polygon edge.
        aapolygonRGBA(portR, svx.data(), svy.data(), static_cast<int>(vCount), 0, 0, 0, 220);
    }

    if (showConnectionPorts()) {
        for (auto* conn : inputs.connections) {
            if (conn) conn->render(portR, conn->id == hoveredConnection && hoveredDirection == Direction::input);
        }
        for (auto* conn : outputs.connections) {
            if (conn) conn->render(portR, conn->id == hoveredConnection && hoveredDirection == Direction::output);
        }
    }

    // Tooltip after all ports/cables so it draws on top
    if (hoveredConnection != -1 && fonts.mainFont) {
        Connection* hovered = nullptr;
        for (auto* conn : inputs.connections) {
            if (conn && conn->id == hoveredConnection && hoveredDirection == Direction::input) { hovered = conn; break; }
        }
        if (!hovered) {
            for (auto* conn : outputs.connections) {
                if (conn && conn->id == hoveredConnection && hoveredDirection == Direction::output) { hovered = conn; break; }
            }
        }
        if (hovered && ne) {
            if (ne->mouseX < hovered->rect.x || ne->mouseX > hovered->rect.x + hovered->rect.w ||
                ne->mouseY < hovered->rect.y || ne->mouseY > hovered->rect.y + hovered->rect.h)
                hovered = nullptr;
        }
        if (hovered) {
            const std::string tipText = hovered->label.empty()
                ? (std::string(hovered->dir == Direction::input ? "Input " : "Output ") + std::to_string(hovered->id))
                : hovered->label;
            const float mx = ne ? ne->mouseX : 0.f;
            const float my = ne ? ne->mouseY : 0.f;
            SDL_Surface* tipSurf = TTF_RenderText_Blended(fonts.mainFont, tipText.c_str(), 0, SDL_Color{255, 255, 255, 255});
            if (tipSurf) {
                SDL_Texture* tipTex = SDL_CreateTextureFromSurface(portR, tipSurf);
                if (tipTex) {
                    const float pad = 4.0f;
                    SDL_FRect bg{mx + 12.0f, my + 12.0f, static_cast<float>(tipSurf->w) + pad * 2, static_cast<float>(tipSurf->h) + pad * 2};
                    SDL_SetRenderDrawColor(portR, 30, 30, 30, 230);
                    SDL_RenderFillRect(portR, &bg);
                    SDL_SetRenderDrawColor(portR, 180, 180, 180, 255);
                    SDL_RenderRect(portR, &bg);
                    SDL_FRect tr{mx + 12.0f + pad, my + 12.0f + pad, static_cast<float>(tipSurf->w), static_cast<float>(tipSurf->h)};
                    SDL_RenderTexture(portR, tipTex, nullptr, &tr);
                    SDL_DestroyTexture(tipTex);
                }
                SDL_DestroySurface(tipSurf);
            }
        }
    }
}

void Connection::render(SDL_Renderer* renderer, bool hover) {
    if (!renderer) return;

    const PortDisplayMode mode = static_cast<PortDisplayMode>(Settings::instance().portDisplayMode());
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

    if (is_connected && dir == Direction::input) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        auto src = srcRect();
        if (src.w > 0.0f || src.h > 0.0f) {
            SDL_FColor color;
            if (type == DataType::Events) color = {0.5f, 1.0f, 0.5f, 1.0f};
            else color = {1.0f, 0.5f, 0.5f, 1.0f};

            NodeEditor::renderPatchCable(renderer, rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f,
                src.x + src.w * 0.5f, src.y + src.h * 0.5f, color);
        }
    }

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

void Node::clearTextures() {
    if (texture) SDL_DestroyTexture(texture);
    texture = nullptr;
    clearParamTextures();
    clearCustomTextures();
}

void Node::attach() {
    x = dstRect.x;
    y = dstRect.y;
    w = dstRect.w;
    h = dstRect.h;
    ne->registerEmbeddedWindow(this);
    clearParamTextures();
    clearCustomTextures();

    mouseX = &(ne->mouseX);
    mouseY = &(ne->mouseY);
}

void Node::handleWindowInput(SDL_Event& e) {
    for (size_t pi = 0; pi < params.size(); ++pi) {
        auto p = params[pi];
        if (inPolygon(p->vx.data(), p->vy.data(), p->vx.size(), msX, msY)) {
            float oldValue = p->value;
            p->handleInput(e);
            if (p->value != oldValue && project && project->um) {
                std::vector<int> mgrPath = nm ? nm->managerPath : std::vector<int>{};
                // Coalesce rapid wheel events into a single undo action.
                bool merged = false;
                if (project->um->current->type == SetParamValue) {
                    auto* prev = static_cast<SetParamValueUndoAction*>(project->um->current);
                    if (prev->nodeID == static_cast<int>(id) && prev->paramPath == std::vector<size_t>{pi} && prev->managerPath == mgrPath) {
                        prev->newValue = p->value;
                        merged = true;
                        ProjectAction* cap = prev;
                        project->um->enqueueAudioSync([cap]() { cap->doAction(); });
                    }
                }
                if (!merged) {
                    auto* pa = new SetParamValueUndoAction(project, std::move(mgrPath), static_cast<int>(id), {pi},
                        oldValue, p->value, "Knob Change");
                    project->um->newAction(pa);
                }
            }
        }
    }
    handleCustomInput(e);
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
    if (ne) ne->unregisterEmbeddedWindow(this);
    clearParamTextures();
    clearCustomTextures();
    if (texture) {
        SDL_DestroyTexture(texture);
        texture = nullptr;
    }

    ne = nullptr;
    window = nullptr;
    renderer = nullptr;

    resetNEFinal();
}

void Node::buildHitPolygon(std::vector<SDL_FPoint>& out) const {
    if (!vCount || !vx || !vy) {
        EmbeddedWindow::buildHitPolygon(out);
        return;
    }
    out.resize(vCount);
    for (size_t i = 0; i < vCount; ++i)
        out[i] = {dstRect.x + vx[i] * zoomRatio, dstRect.y + vy[i] * zoomRatio};
}

void Node::applyResizeDelta(float dx, float dy) {
    EmbeddedWindow::applyResizeDelta(dx, dy);
    dstRect.x = x;
    dstRect.y = y;
    dstRect.w = w;
    dstRect.h = h;
    resize(0, 0);
    w = dstRect.w;
    h = dstRect.h;
}

