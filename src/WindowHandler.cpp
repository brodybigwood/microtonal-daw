#include "WindowHandler.h"
#include "SDL_Events.h"
#include "NodeEditor.h"
#include "Node.h"
#include <sstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <vector>
#include "nodes/nodetypes.h"
using json = nlohmann::json;

static std::vector<int> parseManagerPath(const std::string& s) {
    std::vector<int> out;
    if (s.empty() || s == "-" || s == "root") return out;
    std::string cleaned;
    cleaned.reserve(s.size());
    for (char ch : s) {
        if (ch == '[' || ch == ']' || ch == ' ') continue;
        if (ch == ',') cleaned.push_back('/');
        else cleaned.push_back(ch);
    }
    if (cleaned.empty() || cleaned == "-" || cleaned == "root") return out;
    std::stringstream ss(cleaned);
    std::string part;
    while (std::getline(ss, part, '/')) {
        if (part.empty()) continue;
        out.push_back(std::stoi(part));
    }
    return out;
}

static bool buildActionParamsPositional(const std::string& actionName, std::stringstream& ss, json& params, std::string& error) {
    try {
        if (actionName == "add_node") {
            std::string path; int nodeType; float x, y;
            if (!(ss >> path >> nodeType >> x >> y)) { error = "usage: action add_node <path|-|root> <nodeType> <x> <y>"; return false; }
            params["managerPath"] = parseManagerPath(path);
            params["nodeType"] = nodeType; params["x"] = x; params["y"] = y;
            return true;
        }
        if (actionName == "remove_node") {
            std::string path; int nodeID;
            if (!(ss >> path >> nodeID)) { error = "usage: action remove_node <path|-|root> <nodeID>"; return false; }
            params["managerPath"] = parseManagerPath(path);
            params["nodeID"] = nodeID;
            return true;
        }
        if (actionName == "make_node_connection" || actionName == "sever_node_connection") {
            std::string path; int srcNodeID, srcConID, dstNodeID, dstConID;
            if (!(ss >> path >> srcNodeID >> srcConID >> dstNodeID >> dstConID)) {
                error = "usage: action " + actionName + " <path|-|root> <srcNodeID> <srcConID> <dstNodeID> <dstConID>";
                return false;
            }
            params["managerPath"] = parseManagerPath(path);
            params["srcNodeID"] = srcNodeID; params["srcConID"] = srcConID;
            params["dstNodeID"] = dstNodeID; params["dstConID"] = dstConID;
            return true;
        }
        if (actionName == "move_node") {
            std::string path; int nodeID; float a, b;
            if (!(ss >> path >> nodeID >> a >> b)) {
                error = "usage: action move_node <path|-|root> <nodeID> <toX> <toY>";
                return false;
            }
            params["managerPath"] = parseManagerPath(path);
            params["nodeID"] = nodeID;
            params["toX"] = a;
            params["toY"] = b;
            return true;
        }
        if (actionName == "add_arranger_track") {
            std::string path; int nodeID, trackType;
            if (!(ss >> path >> nodeID >> trackType)) { error = "usage: action add_arranger_track <path|-|root> <nodeID> <trackType>"; return false; }
            params["managerPath"] = parseManagerPath(path);
            params["nodeID"] = nodeID; params["trackType"] = trackType;
            return true;
        }
        if (actionName == "create_note") {
            std::string path; int nodeID, regionID, sNum, sDen, lNum, lDen, scaleID; float pitch;
            if (!(ss >> path >> nodeID >> regionID >> sNum >> sDen >> lNum >> lDen >> pitch >> scaleID)) {
                error = "usage: action create_note <path|-|root> <nodeID> <regionID> <startNum> <startDen> <lenNum> <lenDen> <pitch> <scaleID>";
                return false;
            }
            params["managerPath"] = parseManagerPath(path);
            params["nodeID"] = nodeID; params["regionID"] = regionID;
            params["start"] = fract(sNum, sDen).toJSON();
            params["length"] = fract(lNum, lDen).toJSON();
            params["pitch"] = pitch; params["scaleID"] = scaleID;
            return true;
        }
        error = "unknown action";
        return false;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

WindowHandler::WindowHandler() {

    SDL_SetHint(SDL_HINT_APP_NAME, "EDITOR");
    SDL_SetHint(SDL_HINT_APP_ID, "daw.editor");

    lastTime = SDL_GetTicks();

    ctxMenu = ContextMenu::get();
}

WindowHandler::~WindowHandler() {
}

WindowHandler* WindowHandler::instance() {
    static WindowHandler w;
    return &w;
}

bool WindowHandler::tick() { 

    if (!running) return false;
    processCommands();

    double timeSinceLastFrame = double(SDL_GetTicks())-double(lastTime);
    if(timeSinceLastFrame >= frameTime) {
        lastTime = double(SDL_GetTicks())-frameTime;

        if (ctxMenu->active) project->render(); // need to render behind ctxmenu first
        bool eventHandled = false;
 
        SDL_Event e;
        while(SDL_PollEvent(&e)) {
            eventHandled = true;

            if (e.type == SDL_EVENT_QUIT) {
                running = false;
                break;
            }
            if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                bool handledClose = false;
                SDL_Window* closingWindow = SDL_GetWindowFromID(e.window.windowID);
                if (closingWindow) {
                    for (auto* w : windows) {
                        if (!w || w->window != closingWindow) continue;
                        if (auto* n = dynamic_cast<Node*>(w)) {
                            if (n->detached) {
                                n->attach();
                                handledClose = true;
                            }
                        } else if (project && project->window && closingWindow != project->window) {
                            // Non-host utility windows should close themselves, not quit the app.
                            SDL_HideWindow(closingWindow);
                            handledClose = true;
                        }
                        break;
                    }
                }
                if (handledClose) continue;
                running = false;
                break;
            }


            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                SDL_Window* clickedWindow = SDL_GetWindowFromID(e.button.windowID);
                if (clickedWindow) {
                    SDL_RaiseWindow(clickedWindow);
                }
            }

            if (e.type == SDL_EVENT_KEY_DOWN) {
                if (e.key.scancode == SDL_SCANCODE_SPACE) {
                    project->togglePlaying();
                    continue;
                }

                const SDL_Keymod mods = SDL_GetModState();
                const bool ctrlDown = (mods & SDL_KMOD_CTRL) != 0;
                const bool shiftDown = (mods & SDL_KMOD_SHIFT) != 0;
                if (ctrlDown) {
                    if (e.key.key == SDLK_Z) {
                        if (shiftDown) {
                            project->redo();
                        } else {
                            if (project->um->current == project->um->head) continue;
                            project->undo();
                        }
                        continue;
                    } else if (e.key.key == SDLK_S) {
                        SDL_Renderer* eventRenderer = nullptr;
                        for (auto* w : windows) {
                            if (w && SDL_GetWindowFromID(e.key.windowID) == w->window) {
                                eventRenderer = w->renderer;
                                break;
                            }
                        }
                        project->save(e.key.windowID, eventRenderer);
                        continue;
                    }
                }
            }

            toggleKey(e, SDL_SCANCODE_LSHIFT, isShiftPressed);
            toggleKey(e, SDL_SCANCODE_LCTRL, isCtrlPressed);
            toggleKey(e, SDL_SCANCODE_LALT, isAltPressed);

            if (ctxMenu->active) ctxMenu->tick(e);
            else for (auto w : windows)
                if (SDL_GetWindowFromID(getEventWindowID(e)) == w->window) {
                    w->handleWindowInput(e);
                    break;
                }
        }

        if (!running) return false;

        if (!eventHandled && ctxMenu->active) { // home was already rendered, but ctxMenu hasn't rendered yet. so render on top by triggering with fake event
            e.type = SDL_EVENT_USER;
            e.window.windowID = ctxMenu->window_id;
            ctxMenu->tick(e);
        } else if (!ctxMenu->active) {
            project->render();
        }
        project->renderPresent();
    }
    return running;

}

void WindowHandler::enqueueCommand(const std::string& cmd) {
    std::lock_guard<std::mutex> lock(commandMutex);
    pendingCommands.push(cmd);
}

void WindowHandler::processCommands() {
    std::queue<std::string> commands;
    {
        std::lock_guard<std::mutex> lock(commandMutex);
        commands.swap(pendingCommands);
    }

    while (!commands.empty()) {
        std::string cmd = commands.front();
        commands.pop();
        std::stringstream ss(cmd);
        std::string op;
        ss >> op;

        if (op == "play") {
            if (project && !project->isPlaying.load()) project->togglePlaying();
        } else if (op == "stop") {
            if (project) project->stop();
        } else if (op == "undo") {
            if (project && project->um->current != project->um->head) project->undo();
        } else if (op == "redo") {
            if (project) project->redo();
        } else if (op == "save") {
            if (project) project->save();
        } else if (op == "cycle") {
            int n = 1;
            ss >> n;
            if (n < 1) n = 1;
            for (int i = 0; i < n; ++i) {
                if (project && project->um->current != project->um->head) project->undo();
                if (project) project->redo();
            }
        } else if (op == "quit") {
            running = false;
            return;
        } else if (op == "actions") {
            std::cout << "[cmd] actions:";
            for (const auto& [name, _] : UndoManager::actionRegistry()) {
                std::cout << " " << name;
            }
            std::cout << std::endl;
        } else if (op == "node_types") {
            for (int i = 0; i < NodeType::Count; ++i) {
                std::cout << "[cmd] node_type " << i << ": " << NodeTypeStr[i] << std::endl;
            }
        } else if (op == "action_schema") {
            std::string actionName;
            ss >> actionName;
            if (actionName.empty()) {
                std::cout << "[cmd] usage: action_schema <name>" << std::endl;
                continue;
            }
            std::cout << "[cmd] schema " << actionName << ": " << UndoManager::actionSchema(actionName) << std::endl;
        } else if (op == "action_schema_all") {
            for (const auto& [name, _] : UndoManager::actionRegistry()) {
                std::cout << "[cmd] schema " << name << ": " << UndoManager::actionSchema(name) << std::endl;
            }
        } else if (op == "action") {
            std::string actionName;
            ss >> actionName;
            if (actionName.empty()) {
                std::cout << "[cmd] usage: action <name> <positional args...> OR action <name> {json...}" << std::endl;
                continue;
            }
            std::string rest;
            std::getline(ss, rest);
            json params = json::object();
            if (!rest.empty() && rest.find('{') != std::string::npos) {
                try {
                    params = json::parse(rest);
                } catch (const std::exception& e) {
                    std::cout << "[cmd] invalid json: " << e.what() << std::endl;
                    continue;
                }
            } else {
                std::stringstream argss(rest);
                std::string perr;
                if (!buildActionParamsPositional(actionName, argss, params, perr)) {
                    std::cout << "[cmd] action failed: " << perr << std::endl;
                    continue;
                }
            }
            if (!project || !project->um) {
                std::cout << "[cmd] action failed: project/undo manager unavailable" << std::endl;
                continue;
            }
            std::string error;
            if (!project->um->runRegisteredAction(actionName, params, error)) {
                std::cout << "[cmd] action failed: " << error << std::endl;
            }
        } else if (!op.empty()) {
            std::cout << "[cmd] unknown: " << op << std::endl;
            std::cout << "[cmd] available: play, stop, undo, redo, save, cycle N, actions, node_types, action_schema <name>, action_schema_all, action <name> <json>, quit" << std::endl;
        }
    }
}

void WindowHandler::toggleKey(SDL_Event& e, SDL_Scancode keycode, bool& keyVar) {

    if (e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_KEY_UP) {
        if (e.key.scancode == keycode) {
            if (e.type == SDL_EVENT_KEY_DOWN) {
                keyVar = true;
            } else if (e.type == SDL_EVENT_KEY_UP) {
                keyVar = false;
            }
        }
    }
}

void WindowHandler::addWindow(Window* w) {
    windows.push_back(w);
}

void WindowHandler::removeWindow(Window* w) {
    auto it = std::find(windows.begin(), windows.end(), w);
    if (it != windows.end()) {
        windows.erase(it);
    }
}
