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
#include "Settings.h"
#include "Project.h"
#include <iostream>
#include <cstdio>
#include <filesystem>
#include <functional>
#include "Preferences.h"
#include "styles.h"
#include <algorithm>
#include <array>
#include <cmath>

// Root menu bar layout, rendering and menu tree (split from NodeEditor.cpp).

void NodeEditor::resetRootMenuBarLayout() {
    topMargin = 0.f;
    nodeRect = SDL_FRect{0.f, 0.f, canvasW_, canvasH_};
}

void NodeEditor::updateRootMenuBarLayout() {
    resetRootMenuBarLayout();
    if (rootMenuBar_) {
        topMargin = kRootMenuBarStripH;
        nodeRect = SDL_FRect{0.f, topMargin, canvasW_, std::max(1.f, canvasH_ - topMargin)};
        return;
    }
    if (!nm || !menuBarHostNode_) return;
    if (!menuBarHostNode_->nm || !menuBarHostNode_->nm->managerPath.empty()) return;
    topMargin = kRootMenuBarStripH;
    nodeRect = SDL_FRect{0.f, topMargin, canvasW_, std::max(1.f, canvasH_ - topMargin)};
}

bool NodeEditor::isPointerOverMenuBar(float mx, float my) const {
    if (topMargin <= 0.f) return false;
    return mx >= 0.f && mx < canvasW_ && my >= 0.f && my < topMargin;
}

void NodeEditor::renderRootMenuBarSkeleton(SDL_Renderer* ren, const SDL_FRect* surface) {
    auto fill = [ren](uint8_t R, uint8_t G, uint8_t B, const SDL_FRect& r) {
        SDL_SetRenderDrawColor(ren, R, G, B, 255);
        SDL_RenderFillRect(ren, &r);
    };

    fill(38, 38, 40, SDL_FRect{surface->x, surface->y, surface->w, topMargin});

    static constexpr std::array<const char*, 4> kLabels{"File", "Edit", "View", "Window"};
    constexpr float kPadX = 6.f;
    constexpr float kMinItemW = 52.f;
    float x = surface->x + kPadX;
    const float y = surface->y + 2.f;
    const float h = topMargin - 4.f;

    for (size_t i = 0; i < kLabels.size(); ++i) {
        int tw = 0;
        int th = 0;
        SDL_Surface* surf = nullptr;
        if (fonts.mainFont)
            surf = TTF_RenderText_Blended(fonts.mainFont, kLabels[i], 0, SDL_Color{255, 255, 255, 255});
        if (surf) {
            tw = surf->w;
            th = surf->h;
        }
        const float w = std::max(kMinItemW, static_cast<float>(tw) + 16.f);
        const SDL_FRect cell{x, y, w, h};

        // Store for hit-testing in handleInput.
        menuLabelRects_[i] = cell;

        // Highlight the open menu label.
        if (static_cast<int>(i) == menuOpenIndex_)
            fill(58, 58, 64, cell);
        else
            fill(44, 44, 48, cell);

        SDL_SetRenderDrawColor(ren, 20, 20, 22, 255);
        SDL_RenderRect(ren, &cell);

        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
            SDL_DestroySurface(surf);
            if (tex) {
                const float tx = x + (w - static_cast<float>(tw)) * 0.5f;
                const float ty = y + (h - static_cast<float>(th)) * 0.5f;
                SDL_FRect tr{tx, ty, static_cast<float>(tw), static_cast<float>(th)};
                SDL_RenderTexture(ren, tex, nullptr, &tr);
                SDL_DestroyTexture(tex);
            }
        }
        x += w + 4.f;
    }
}

// Synchronous folder picker (zenity), on the calling GUI thread.
static std::string chooseFolderDialog(const char* title) {
    char cmd[512];
    std::snprintf(cmd, sizeof(cmd),
                  "zenity --file-selection --directory --title='%s' 2>/dev/null", title);
    FILE* fp = popen(cmd, "r");
    if (!fp) return std::string();
    std::string out;
    char buf[1024];
    while (fgets(buf, sizeof(buf), fp)) out += buf;
    pclose(fp);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) out.pop_back();
    return out;
}

// Ask before a load discards unsaved work. Three ways out: Save, Discard,
// or the dialog's Cancel which aborts the load.
static bool promptSaveBeforeLoad(Project* p) {
    if (!p || !p->dirty.load()) return true;
    FILE* fp = popen(
        "zenity --list --radiolist --title='Unsaved changes' "
        "--text='Save changes before loading another project?' "
        "--column='Sel' --column='Action' --hide-column=1 --print-column=2 "
        "TRUE 'Save' FALSE 'Discard' 2>/dev/null", "r");
    if (!fp) return true;
    std::string choice;
    char buf[128];
    while (fgets(buf, sizeof(buf), fp)) choice += buf;
    const int rc = pclose(fp);
    while (!choice.empty() && (choice.back() == '\n' || choice.back() == '\r')) choice.pop_back();
    if (rc != 0) return false;            // dialog cancelled: don't load
    if (choice == "Discard") return true; // load without saving
    if (p->filepath.empty()) {
        const std::string folder = chooseFolderDialog("Save Project As");
        if (folder.empty()) return false;
        p->filepath = folder;
        p->save();
        Settings::instance().addRecentProject(folder);
    } else {
        p->save();
    }
    return true;
}

static void saveProjectAs(Project* p) {
    if (!p) return;
    const std::string folder = chooseFolderDialog("Save Project As");
    if (folder.empty()) return;
    p->filepath = folder;
    p->save();
    Settings::instance().addRecentProject(folder);
}

std::shared_ptr<TreeEntry> NodeEditor::buildMenuTree(int menuIndex) {
    auto root = uTreeEntry();
    switch (menuIndex) {
        case 0: { // File
            root->label = "File";
            auto add = [&](const char* label, std::function<void()> fn) {
                auto item = uTreeEntry();
                item->label = label;
                item->click = std::move(fn);
                root->addChild(item);
            };

            add("Save", [this]() {
                Project* p = (nm) ? nm->project : nullptr;
                if (!p) return;
                if (p->filepath.empty()) saveProjectAs(p);
                else p->save();
            });
            add("Save As...", [this]() {
                saveProjectAs(nm ? nm->project : nullptr);
            });
            add("Load From...", [this]() {
                Project* p = (nm) ? nm->project : nullptr;
                if (!p) return;
                const std::string folder = chooseFolderDialog("Open Project");
                if (!folder.empty() && promptSaveBeforeLoad(p)) p->startAsyncLoad(folder);
            });

            auto recent = uTreeEntry();
            recent->label = "Load Recent";
            const auto paths = Settings::instance().getRecentProjects();
            if (paths.empty()) {
                auto none = uTreeEntry();
                none->label = "(none)";
                recent->addChild(none);
            } else {
                for (const auto& path : paths) {
                    auto item = uTreeEntry();
                    item->label = std::filesystem::path(path).filename().string();
                    item->click = [this, path]() {
                        Project* p = (nm) ? nm->project : nullptr;
                        if (p && promptSaveBeforeLoad(p)) p->startAsyncLoad(path);
                    };
                    recent->addChild(item);
                }
            }
            root->addChild(recent);
            break;
        }
        case 1: { // Edit
            root->label = "Edit";
            {
                auto item = uTreeEntry();
                item->label = "Preferences...";
                item->click = [this]() {
                    if (!nm || !nm->project || !nm->project->processor) return;
                    auto* wm = nm->project->processor->getWindowManager();
                    if (!wm) return;
                    auto* existing = wm->findByTitle("Preferences");
                    if (existing) {
                        existing->show();
                        SDL_RaiseWindow(existing->window);
                        return;
                    }
                    auto pw = std::make_unique<PreferencesExpandedWindow>();
                    ExpandedWindow* ew = wm->addWindow(std::move(pw), 500, 500, "Preferences");
                    if (ew) {
                        ew->show();
                        ew->setPosition(200, 150);
                    }
                };
                root->addChild(item);
            }
            {
                auto item = uTreeEntry();
                item->label = "Undo Tree";
                item->click = [this]() {
                    if (!nm || !nm->project || !nm->project->processor) return;
                    auto* wm = nm->project->processor->getWindowManager();
                    if (!wm) return;
                    auto* existing = wm->findByTitle("Undo Tree");
                    if (existing) {
                        existing->show();
                        SDL_RaiseWindow(existing->window);
                        return;
                    }
                    auto uw = std::make_unique<UndoTreeExpandedWindow>(nm->project);
                    ExpandedWindow* ew = wm->addWindow(std::move(uw), 480, 640, "Undo Tree");
                    if (ew) {
                        ew->show();
                        ew->setPosition(200, 150);
                    }
                };
                root->addChild(item);
            }
            break;
        }
        default: break;
    }
    return root;
}

