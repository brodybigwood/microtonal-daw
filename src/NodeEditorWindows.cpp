#include "NodeEditor.h"
#include "NodeManager.h"
#include "Node.h"
#include "ContextMenu.h"
#include "WindowHandler.h"
#include "PianoRoll.h"
#include "PianoRollWindow.h"
#include "UndoTreeExpandedWindow.h"
#include "PreferencesExpandedWindow.h"
#include "WindowManager.h"
#include "NodeProcessor.h"
#include "nodes/arranger/arranger.h"
#ifndef __EMSCRIPTEN__
#include "nodes/vst/vstplugin.h"
#endif
#include "UndoManager.h"
#include <iostream>
#include "Preferences.h"
#include "styles.h"
#include <algorithm>
#include <array>
#include <cmath>

// Embedded-window ownership, event routing and open-piano-roll persistence (split from NodeEditor.cpp).

EmbeddedWindow* NodeEditor::addEmbeddedWindow(std::unique_ptr<EmbeddedWindow> w) {
    if (!w) return nullptr;
    int maxZ = 0;
    for (auto& ew : embeddedWindows_)
        if (ew->id != 0 && ew->id != 1 && ew->zOrder > maxZ) maxZ = ew->zOrder;
    if (w->id == 0 || w->id == 1) {
        for (auto& ew : embeddedWindows_)
            if (ew->zOrder > maxZ) maxZ = ew->zOrder;
    }
    w->zOrder = maxZ + 1;
    registerEmbeddedWindow(w.get());
    EmbeddedWindow* ptr = w.get();
    embeddedWindows_.push_back(std::move(w));
    return ptr;
}

void NodeEditor::clearPointersToEmbeddedWindow(EmbeddedWindow* w) {
    if (!w) return;
    if (capturedEmbeddedWindow_ == w) capturedEmbeddedWindow_ = nullptr;
    if (focusedEmbeddedWindow_ == w) focusedEmbeddedWindow_ = nullptr;
}

void NodeEditor::removeEmbeddedWindow(EmbeddedWindow* w) {
    if (!w) return;
    clearPointersToEmbeddedWindow(w);
    unregisterEmbeddedWindow(w);
    for (auto it = embeddedWindows_.begin(); it != embeddedWindows_.end(); ++it) {
        if (it->get() == w) {
            embeddedWindows_.erase(it);
            return;
        }
    }
}

void NodeEditor::renderEmbeddedWindows(SDL_Renderer* r) {
    std::vector<EmbeddedWindow*> sorted;
    sorted.reserve(embeddedWindows_.size());
    for (auto& ew : embeddedWindows_)
        if (ew->visible) sorted.push_back(ew.get());
    std::sort(sorted.begin(), sorted.end(),
              [](EmbeddedWindow* a, EmbeddedWindow* b) { return a->zOrder < b->zOrder; });
    for (auto* ew : sorted)
        ew->render(r);
}

bool NodeEditor::routeEmbeddedWindowEvent(SDL_Event& e, float mouseX, float mouseY) {
    EmbeddedWindow* target = nullptr;
    bool targetIsResize = false;
    EmbeddedWindow* prevCapture = capturedEmbeddedWindow_;

    // --- Mouseup: route to captured window, release after processing ---
    bool releaseCaptureAfter = false;
    if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) {
        target = capturedEmbeddedWindow_;
        targetIsResize = (captureKind_ == CaptureKind::Resize);
        releaseCaptureAfter = (capturedEmbeddedWindow_ != nullptr);
    }

    // --- Motion / other events during capture: route to same window ---
    if (!target && capturedEmbeddedWindow_) {
        target = capturedEmbeddedWindow_;
        targetIsResize = (captureKind_ == CaptureKind::Resize);
    }

    // --- No target: search non-node windows first (always on top), then nodes ---
    if (!target) {
        auto searchInList = [&](std::vector<EmbeddedWindow*>& list) -> bool {
            std::sort(list.begin(), list.end(),
                      [](EmbeddedWindow* a, EmbeddedWindow* b) { return a->zOrder > b->zOrder; });
            for (auto* ew : list) {
                if (ew->getResizeZone(mouseX, mouseY) != EmbeddedWindow::ResizeZone::None) {
                    target = ew;
                    targetIsResize = true;
                    captureKind_ = CaptureKind::Resize;
                    return true;
                }
                if (ew->hitTest(mouseX, mouseY)) {
                    target = ew;
                    captureKind_ = CaptureKind::Content;
                    return true;
                }
                Node* node = dynamic_cast<Node*>(ew);
                if (node && node->showConnectionPorts()) {
                    for (auto* c : node->inputs.connections) {
                        if (c && mouseX >= c->rect.x && mouseX <= c->rect.x + c->rect.w &&
                            mouseY >= c->rect.y && mouseY <= c->rect.y + c->rect.h) { target = node; break; }
                    }
                    if (!target) {
                        for (auto* c : node->outputs.connections) {
                            if (c && mouseX >= c->rect.x && mouseX <= c->rect.x + c->rect.w &&
                                mouseY >= c->rect.y && mouseY <= c->rect.y + c->rect.h) { target = node; break; }
                        }
                    }
                    if (target) {
                        captureKind_ = CaptureKind::Connection;
                        return true;
                    }
                }
            }
            return false;
        };

        // Non-node windows first (undo tree, prefs — rendered on top, get input priority).
        {
            std::vector<EmbeddedWindow*> winList;
            for (auto& ew : embeddedWindows_)
                if (ew->visible) winList.push_back(ew.get());
            if (searchInList(winList)) goto found;
        }
        // Nodes second.
        if (nm) {
            std::vector<EmbeddedWindow*> nodeList;
            for (auto n : nm->getNodes())
                if (n->visible) nodeList.push_back(n);
            if (nm->inNode && nm->inNode->visible) nodeList.push_back(nm->inNode);
            if (nm->outNode && nm->outNode->visible) nodeList.push_back(nm->outNode);
            if (searchInList(nodeList)) goto found;
        }
        found:;

        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && target) {
            // Release previous capture if any
            if (capturedEmbeddedWindow_ && capturedEmbeddedWindow_ != target) {
                capturedEmbeddedWindow_->captured_ = false;
            }
            capturedEmbeddedWindow_ = target;
            target->captured_ = true;
            focusedEmbeddedWindow_ = target;
            int maxZ = 0;
            bool targetIsSystem = (target->id == 0 || target->id == 1);
            for (auto& ewin : embeddedWindows_) {
                if (!targetIsSystem && (ewin->id == 0 || ewin->id == 1)) continue;
                if (ewin->zOrder > maxZ) maxZ = ewin->zOrder;
            }
            if (nm) {
                for (auto n : nm->getNodes()) if (n->zOrder > maxZ) maxZ = n->zOrder;
                if (nm->inNode && nm->inNode->zOrder > maxZ) maxZ = nm->inNode->zOrder;
                if (nm->outNode && nm->outNode->zOrder > maxZ) maxZ = nm->outNode->zOrder;
            }
            target->zOrder = maxZ + 1;
        }
    }

    // --- Cursor ---
    {
        SDL_SystemCursor cur = SDL_SYSTEM_CURSOR_DEFAULT;
        if (target && targetIsResize) {
            auto zone = target->getResizeZone(mouseX, mouseY);
            switch (zone) {
                case EmbeddedWindow::ResizeZone::N:  cur = SDL_SYSTEM_CURSOR_N_RESIZE;  break;
                case EmbeddedWindow::ResizeZone::S:  cur = SDL_SYSTEM_CURSOR_S_RESIZE;  break;
                case EmbeddedWindow::ResizeZone::E:  cur = SDL_SYSTEM_CURSOR_E_RESIZE;  break;
                case EmbeddedWindow::ResizeZone::W:  cur = SDL_SYSTEM_CURSOR_W_RESIZE;  break;
                case EmbeddedWindow::ResizeZone::NE: cur = SDL_SYSTEM_CURSOR_NE_RESIZE; break;
                case EmbeddedWindow::ResizeZone::NW: cur = SDL_SYSTEM_CURSOR_NW_RESIZE; break;
                case EmbeddedWindow::ResizeZone::SE: cur = SDL_SYSTEM_CURSOR_SE_RESIZE; break;
                case EmbeddedWindow::ResizeZone::SW: cur = SDL_SYSTEM_CURSOR_SW_RESIZE; break;
                default: break;
            }
        }
        if (currentCursor_) SDL_DestroyCursor(currentCursor_);
        currentCursor_ = SDL_CreateSystemCursor(cur);
        SDL_SetCursor(currentCursor_);
    }

    // --- Resize path ---
    if (target && targetIsResize && target->handleResizeInput(e, mouseX, mouseY, isShiftPressed)) {
        if (!undoCaptured_ && e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            undoBeforeX_ = target->x;
            undoBeforeY_ = target->y;
            undoBeforeW_ = target->w;
            undoBeforeH_ = target->h;
            undoIsResize_ = !isShiftPressed;
            undoCaptured_ = true;
        }
        if (undoCaptured_ && e.type == SDL_EVENT_MOUSE_BUTTON_UP && prevCapture) {
            float afterX = prevCapture->x, afterY = prevCapture->y;
            float afterW = prevCapture->w, afterH = prevCapture->h;
            int ewid = prevCapture->id;
            std::vector<int> mgrPath = nm ? nm->managerPath : std::vector<int>{};
            Project* proj = nm ? nm->project : nullptr;
            if (proj && proj->um && prevCapture->trackMoveResizeInUndo()) {
                if (undoIsResize_) {
                    if (undoBeforeX_ != afterX || undoBeforeY_ != afterY ||
                        undoBeforeW_ != afterW || undoBeforeH_ != afterH) {
                        auto* action = new ResizeEmbeddedWindowAction(proj, mgrPath, ewid,
                            undoBeforeX_, undoBeforeY_, undoBeforeW_, undoBeforeH_,
                            afterX, afterY, afterW, afterH);
                        proj->um->newAction(action);
                    }
                } else {
                    if (undoBeforeX_ != afterX || undoBeforeY_ != afterY) {
                        auto* action = new MoveEmbeddedWindowAction(proj, mgrPath, ewid,
                            undoBeforeX_, undoBeforeY_, afterX, afterY);
                        proj->um->newAction(action);
                    }
                }
            }
            undoCaptured_ = false;
        }
        if (releaseCaptureAfter) {
            capturedEmbeddedWindow_->captured_ = false;
            capturedEmbeddedWindow_ = nullptr;
            captureKind_ = CaptureKind::None;
        }
        return true;
    }

    // --- Content / connection path ---
    if (target && !targetIsResize && target->EmbeddedWindow::handleInput(e)) {
        if (releaseCaptureAfter) {
            capturedEmbeddedWindow_->captured_ = false;
            capturedEmbeddedWindow_ = nullptr;
            captureKind_ = CaptureKind::None;
        }
        return true;
    }

    if (releaseCaptureAfter) {
        capturedEmbeddedWindow_->captured_ = false;
        capturedEmbeddedWindow_ = nullptr;
        captureKind_ = CaptureKind::None;
    }

    // --- Cleanup stale pointers ---
    if (capturedEmbeddedWindow_ && !capturedEmbeddedWindow_->visible) {
        capturedEmbeddedWindow_->captured_ = false;
        capturedEmbeddedWindow_ = nullptr;
        captureKind_ = CaptureKind::None;
    }
    if (focusedEmbeddedWindow_ && !focusedEmbeddedWindow_->visible)
        focusedEmbeddedWindow_ = nullptr;

    if (e.type == SDL_EVENT_KEY_DOWN && focusedEmbeddedWindow_) {
        if (!focusedEmbeddedWindow_->visible)
            focusedEmbeddedWindow_ = nullptr;
        else if (focusedEmbeddedWindow_->handleKeyboard(e))
            return true;
    }

    return false;
}

json NodeEditor::serializeOpenPianoRolls(const std::vector<int>& managerPath) const {
    json arr = json::array();
    for (auto& ew : embeddedWindows_) {
        auto* pr = dynamic_cast<PianoRoll*>(ew.get());
        if (!pr || !pr->region) continue;
        auto* arrNode = dynamic_cast<ArrangerNode*>(pr->region->parentNode);
        if (!arrNode) continue;
        json j;
        j["managerPath"] = managerPath;
        j["arrangerNodeID"] = static_cast<int>(arrNode->id);
        j["regionID"] = static_cast<int>(pr->region->id);
        j["ewID"] = ew->id;
        j["x"] = ew->x;
        j["y"] = ew->y;
        j["w"] = ew->w;
        j["h"] = ew->h;
        j["zOrder"] = ew->zOrder;
        arr.push_back(j);
    }
    return arr;
}

void NodeEditor::restoreOpenPianoRolls(const json& arr) {
    for (auto& j : arr) {
        const auto& mgrPath = j["managerPath"].get<std::vector<int>>();
        int arrangerNodeID = j["arrangerNodeID"].get<int>();
        int regionID = j["regionID"].get<int>();
        if (!nm || nm->managerPath != mgrPath) continue; // skip if not this manager
        auto* node = nm->getNode(static_cast<uint16_t>(arrangerNodeID));
        auto* arrNode = dynamic_cast<ArrangerNode*>(node);
        if (!arrNode) continue;
        arrNode->ensureSongRoll();
        if (!arrNode->sl) continue;
        auto* region = dynamic_cast<Region*>(arrNode->elements->getElement(static_cast<uint16_t>(regionID)));
        if (!region) continue;
        arrNode->sl->createPianoRoll(region, false, j.value("ewID", -1));
        if (arrNode->sl->pianoRollWindows.empty()) continue;
        auto* prw = arrNode->sl->pianoRollWindows.back();
        if (prw) {
            int px = j.contains("x") ? static_cast<int>(j["x"].get<float>()) : 100;
            int py = j.contains("y") ? static_cast<int>(j["y"].get<float>()) : 100;
            int pw = j.contains("w") ? static_cast<int>(j["w"].get<float>()) : 800;
            int ph = j.contains("h") ? static_cast<int>(j["h"].get<float>()) : 600;
            prw->setPosition(px, py);
            prw->setSize(pw, ph);
        }
    }
}
