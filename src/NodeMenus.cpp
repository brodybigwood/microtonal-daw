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

// Node context menus: node/connection/parameter menus and the mod-matrix ticker (split from Node.cpp).

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

