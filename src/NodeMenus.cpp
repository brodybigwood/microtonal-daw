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


std::shared_ptr<TreeEntry> Node::getNodeMenu() {
    auto t = uTreeEntry();
    t->label = "Node Menu";

    if (this->id) {
        auto remove = uTreeEntry();
        remove->label = "Remove Node";
        remove->click = [this] () { nm->removeNode(this); };
        t->addChild(remove);
    }

    if (!params.empty()) {
        auto mapParam = uTreeEntry();
        mapParam->label = "Map Parameter";
        mapParam->maxListHeight = 300.f;
        for (size_t pi = 0; pi < params.size(); ++pi) {
            auto* p = params[pi];
            if (!p || p->lockMapping) continue;
            std::string label;
            if (auto* k = dynamic_cast<Knob*>(p))
                label = k->label.empty() ? ("Param " + std::to_string(pi)) : k->label;
            else
                label = p->label.empty() ? ("Param " + std::to_string(pi)) : p->label;
            auto item = uTreeEntry();
            bool mapped = (p->mappedConnection != nullptr);
            item->label = (mapped ? "[M] " : "") + label;
            if (mapped)
                item->click = [this, pi]() { unmapParameter(pi); };
            else
                item->click = [this, pi]() { mapParameter(pi); };
            mapParam->addChild(item);
        }
        t->addChild(mapParam);
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

    bool mapped = (p->mappedConnection && p->mappedConnection->is_connected);
    if (!mapped) {
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
    }

    auto copyValue = uTreeEntry();
    copyValue->label = "Copy Value";
    copyValue->click = [p]() {
        std::ostringstream ss;
        ss << p->value;
        SDL_SetClipboardText(ss.str().c_str());
    };
    root->addChild(copyValue);

    if (p->mappedConnection) {
        auto unmap = uTreeEntry();
        unmap->label = "Unmap Parameter";
        size_t pi = path.empty() ? 0 : path[0];
        unmap->click = [this, pi]() { unmapParameter(pi); };
        root->addChild(unmap);
    }

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

