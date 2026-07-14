#include "vstnode.h"
#include "JsonBytes.h"
#include "NodeManager.h"
#include "styles.h"
#include "Settings.h"
#include "UndoManager.h"
#include "NodeProcessor.h"
#include "ContextMenu.h"
#include "AudioManager.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <SDL3_ttf/SDL_ttf.h>

// Synchronous file dialog on the calling (GUI) thread via zenity
static std::string openFileDialog() {
    FILE* fp = popen("zenity --file-selection --title='Load VST3 Plugin' 2>/dev/null", "r");
    if (!fp) return "";
    char buf[4096]{};
    std::string result;
    if (fgets(buf, sizeof(buf), fp)) {
        result = buf;
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();
    }
    pclose(fp);
    return result;
}

// ============================================================================
// VstNode constructor
// ============================================================================

VstNode::VstNode(uint16_t id, NodeManager* nm)
    : Node(id, nm, NodeType::Vst) {

    name = "VST Plugin";
    params.push_back(&bypass);

    // Create default stereo I/O — will be rebuilt when a plugin is loaded.
    rebuildConnections();
}

VstNode::~VstNode() {
    if (plugin) {
        // The shared plugin (and its host frame) can outlive this copy — drop
        // the callbacks bound to `this` before they can fire again.
        if (auto* frame = plugin->getHostFrame()) {
            frame->onBeginEdit = nullptr;
            frame->onPerformEdit = nullptr;
            frame->onStatePoll = nullptr;
        }
        plugin->hideEditor();
    }
    plugin = nullptr;
    VstPluginCache::instance().removePlugin(static_cast<int>(id), nm ? nm->managerPath : std::vector<int>{});
}

// ============================================================================
// rebuildConnections — match plugin bus configuration
// ============================================================================

void VstNode::rebuildConnections() {
    int numAudioInBuses = 0, numAudioOutBuses = 0;
    int numEventIn = 0, numEventOut = 0;

    if (plugin && plugin->isValid()) {
        numAudioInBuses = plugin->getNumAudioInputs();
        numAudioOutBuses = plugin->getNumAudioOutputs();
        numEventIn = plugin->getNumEventInputs();
        numEventOut = plugin->getNumEventOutputs();
    }

    // Grow or reuse audio input bus connections (set numChannels before addConnection)
    for (int b = static_cast<int>(audioInConns.size()); b < numAudioInBuses; ++b) {
        int channels = plugin->getAudioInputChannels(b);
        auto* c = new Connection;
        c->type = DataType::Waveform;
        c->dir = Direction::input;
        c->minChannels = channels;
        c->updateNumChannels();
        inputs.addConnection(c);
        audioInConns.push_back(c);
    }

    // Grow or reuse audio output bus connections (set numChannels before addConnection)
    for (int b = static_cast<int>(audioOutConns.size()); b < numAudioOutBuses; ++b) {
        int channels = plugin->getAudioOutputChannels(b);
        auto* c = new Connection;
        c->type = DataType::Waveform;
        c->dir = Direction::output;
        c->minChannels = channels;
        c->updateNumChannels();
        outputs.addConnection(c);
        audioOutConns.push_back(c);
    }

    // Update labels for existing connections
    for (int b = 0; b < numAudioInBuses && b < static_cast<int>(audioInConns.size()); ++b) {
        int channels = plugin->getAudioInputChannels(b);
        auto* ic = audioInConns[b];
        ic->minChannels = channels;
        ic->numChannels = channels;
        ic->allocChannels = channels;
        ic->label = "In " + std::to_string(b + 1) + " (" + std::to_string(channels) + "ch)";
    }
    for (int b = numAudioInBuses; b < static_cast<int>(audioInConns.size()); ++b)
        audioInConns[b]->label = "";

    for (int b = 0; b < numAudioOutBuses && b < static_cast<int>(audioOutConns.size()); ++b) {
        int channels = plugin->getAudioOutputChannels(b);
        auto* c = audioOutConns[b];
        c->minChannels = channels;
        if (c->numChannels != channels)
            c->allocateBuffer(bufferSize, channels);
        c->label = "Out " + std::to_string(b + 1) + " (" + std::to_string(channels) + "ch)";
    }
    for (int b = numAudioOutBuses; b < static_cast<int>(audioOutConns.size()); ++b)
        audioOutConns[b]->label = "";

    // MIDI connections: create once, reuse
    if (numEventIn > 0 && !midiIn) {
        midiIn = new Connection;
        midiIn->type = DataType::Events;
        midiIn->dir = Direction::input;
        midiIn->label = "MIDI In";
        inputs.addConnection(midiIn);
    }
    if (numEventOut > 0 && !midiOut) {
        midiOut = new Connection;
        midiOut->type = DataType::Events;
        midiOut->dir = Direction::output;
        midiOut->label = "MIDI Out";
        outputs.addConnection(midiOut);
    }
}

// ============================================================================
// setup
// ============================================================================

void VstNode::setup() {
    if (plugin && plugin->isValid()) {
        plugin->setup(sampleRate, bufferSize);
    }
}

// ============================================================================
// process
// ============================================================================

void VstNode::process() {
    bool hasOut = false;
    for (auto* c : audioOutConns) {
        if (c && c->buffer) { hasOut = true; break; }
    }

    if (!hasOut) return;

    // Silence active output buses
    for (auto* c : audioOutConns) {
        if (c && c->buffer)
            std::memset(c->buffer, 0, static_cast<size_t>(bufferSize) * static_cast<size_t>(c->numChannels) * sizeof(float));
    }

    auto localPlugin = plugin;
    if (localPlugin && localPlugin->isValid()) {
        bool bypassed = bypass.value > 0.5f;
        if (bypassed && !wasBypassed_ && !noteChannels.empty()) {
            // Deliver releases once before bypass stops normal event processing.
            HostEventList releases;
            for (const auto& [noteId, channel] : noteChannels) {
                Steinberg::Vst::Event off{};
                off.busIndex = 0;
                off.sampleOffset = 0;
                off.flags = 0;
                off.type = Steinberg::Vst::Event::kNoteOffEvent;
                off.noteOff.channel = static_cast<Steinberg::int16>(channel);
                off.noteOff.pitch = 0;
                off.noteOff.tuning = 0;
                off.noteOff.velocity = 0.0f;
                off.noteOff.noteId = noteId;
                releases.addEvent(off);
            }
            localPlugin->setBypassed(false);
            double beat = project ? project->activeBeatPosition() : 0.0;
            double seconds = project ? project->activeTimeSeconds() : 0.0;
            double bpm = project
                ? static_cast<double>(project->activeTempoCurve().evaluateAtX(static_cast<float>(beat)))
                : 120.0;
            double sr = AudioManager::instance()->sampleRate;
            localPlugin->processAudio(bufferSize, &releases, bpm, beat, seconds * sr,
                                      project ? project->dspIsPlaying : true);
            noteChannels.clear();
            evictedNoteChannels.clear();
        }
        wasBypassed_ = bypassed;
        localPlugin->setBypassed(bypassed);
        if (!bypassed) {
            // Deliver mapped VST parameter values from input connections.
            for (auto& [paramID, conn] : mappedVstParams) {
                if (conn && conn->is_connected && conn->buffer) {
                    auto v = static_cast<Steinberg::Vst::ParamValue>(std::clamp(conn->buffer[0] * 0.5f + 0.5f, 0.0f, 1.0f));
                    localPlugin->queueParameterChange(static_cast<Steinberg::Vst::ParamID>(paramID), v);
                    if (auto* ec = localPlugin->getEditController())
                        ec->setParamNormalized(static_cast<Steinberg::Vst::ParamID>(paramID), v);
                }
            }

            // Route audio input buses directly (both sides already planar)
            for (size_t bi = 0; bi < audioInConns.size(); ++bi) {
                auto* c = audioInConns[bi];
                if (!c || !c->is_connected || !c->buffer) continue;
                if (bi >= localPlugin->inputBusData.size()) continue;
                auto& data = localPlugin->inputBusData[bi];
                size_t busSamples = static_cast<size_t>(bufferSize) * static_cast<size_t>(c->numChannels);
                std::memcpy(data.data(), c->buffer, busSamples * sizeof(float));
            }

            // Convert DAW Events to VST3 EventList with MPE
            HostEventList vstEvents;
            sendMpePitchBendRange(&vstEvents);
            const bool midiConnected = midiIn && midiIn->is_connected && midiIn->events;
            if (!midiConnected && !noteChannels.empty()) {
                // The upstream event connection disappeared while voices were
                // active. Release them before forgetting the MPE allocation.
                for (const auto& [noteId, channel] : noteChannels) {
                    Steinberg::Vst::Event off{};
                    off.busIndex = 0;
                    off.sampleOffset = 0;
                    off.flags = 0;
                    off.type = Steinberg::Vst::Event::kNoteOffEvent;
                    off.noteOff.channel = static_cast<Steinberg::int16>(channel);
                    off.noteOff.pitch = 0;
                    off.noteOff.tuning = 0;
                    off.noteOff.velocity = 0.0f;
                    off.noteOff.noteId = noteId;
                    vstEvents.addEvent(off);
                }
                noteChannels.clear();
                evictedNoteChannels.clear();
            }
            if (midiConnected) {
                for (auto& ev : *midiIn->events) {
                    Steinberg::int16 base = static_cast<Steinberg::int16>(std::floor(ev.num));
                    float frac = ev.num - base;

                    if (ev.type == noteOn) {
                        int mpeCh = allocMpeChannel(ev.id, &vstEvents);
                        if (mpeCh > 0) {
                            Steinberg::int32 pb14 = 8192;
                            if (frac != 0.0f) {
                                pb14 = 8192 + static_cast<Steinberg::int32>(frac / 2.0f * 8192.0f);
                                pb14 = std::max(0, std::min(16383, static_cast<int>(pb14)));
                            }
                            Steinberg::Vst::Event pb{};
                            pb.busIndex = 0;
                            pb.sampleOffset = ev.sampleOffset;
                            pb.flags = Steinberg::Vst::Event::kIsLive;
                            pb.type = Steinberg::Vst::Event::kLegacyMIDICCOutEvent;
                            pb.midiCCOut.controlNumber = Steinberg::Vst::kPitchBend;
                            pb.midiCCOut.channel = static_cast<Steinberg::int8>(mpeCh);
                            pb.midiCCOut.value = static_cast<Steinberg::int8>(pb14 & 0x7F);
                            pb.midiCCOut.value2 = static_cast<Steinberg::int8>((pb14 >> 7) & 0x7F);
                            vstEvents.addEvent(pb);
                        }

                        Steinberg::Vst::Event e{};
                        e.busIndex = 0;
                        e.sampleOffset = ev.sampleOffset;
                        e.flags = Steinberg::Vst::Event::kIsLive;
                        e.type = Steinberg::Vst::Event::kNoteOnEvent;
                        e.noteOn.channel = static_cast<Steinberg::int16>(mpeCh);
                        e.noteOn.pitch = base;
                        e.noteOn.tuning = 0;
                        e.noteOn.velocity = 1.0f;
                        e.noteOn.noteId = ev.id;
                        vstEvents.addEvent(e);
                    } else if (ev.type == noteOff) {
                        int mpeCh = -1;
                        auto it = noteChannels.find(ev.id);
                        if (it != noteChannels.end()) {
                            mpeCh = it->second;
                            freeMpeChannel(ev.id);
                        } else {
                            auto eit = evictedNoteChannels.find(ev.id);
                            if (eit != evictedNoteChannels.end()) {
                                mpeCh = eit->second;
                                evictedNoteChannels.erase(eit);
                            }
                        }
                        if (mpeCh < 0) continue;

                        Steinberg::Vst::Event e{};
                        e.busIndex = 0;
                        e.sampleOffset = ev.sampleOffset;
                        e.flags = 0;
                        e.type = Steinberg::Vst::Event::kNoteOffEvent;
                        e.noteOff.channel = static_cast<Steinberg::int16>(mpeCh >= 0 ? mpeCh : 0);
                        e.noteOff.pitch = base;
                        e.noteOff.tuning = 0;
                        e.noteOff.velocity = 0.0f;
                        e.noteOff.noteId = ev.id;
                        vstEvents.addEvent(e);
                    }
                }
            }

            double bpm = 120.0;
            double beatPos = project ? project->activeBeatPosition() : 0.0;
            double samplePos = (project && AudioManager::instance())
                ? project->activeTimeSeconds() * AudioManager::instance()->sampleRate : 0.0;
            if (project) {
                if (project->dspIsPlaying) {
                    double db = project->deltaBeats;
                    double dt = static_cast<double>(bufferSize) / AudioManager::instance()->sampleRate;
                    bpm = (dt > 0.0) ? db * 60.0 / dt : 120.0;
                } else {
                    bpm = static_cast<double>(project->activeTempoCurve().evaluateAtX(
                        static_cast<float>(project->activeBeatPosition())));
                }
            }
            bool playing = project ? project->dspIsPlaying : true;
            localPlugin->processAudio(bufferSize, &vstEvents, bpm, beatPos, samplePos, playing);

            // Route audio output buses directly (both sides planar)
            for (size_t bi = 0; bi < audioOutConns.size(); ++bi) {
                auto* c = audioOutConns[bi];
                if (!c || !c->buffer) continue;
                if (bi >= localPlugin->outputBusData.size()) continue;
                auto& data = localPlugin->outputBusData[bi];
                size_t ch = std::min(static_cast<size_t>(c->numChannels), data.size() / static_cast<size_t>(bufferSize));
                std::memcpy(c->buffer, data.data(), static_cast<size_t>(bufferSize) * ch * sizeof(float));
            }
        } else {
            // Bypass: passthrough per bus
            for (size_t bi = 0; bi < audioOutConns.size(); ++bi) {
                auto* outC = audioOutConns[bi];
                if (!outC || !outC->buffer) continue;
                size_t busSamples = static_cast<size_t>(bufferSize) * static_cast<size_t>(outC->numChannels);
                if (bi < audioInConns.size() && audioInConns[bi] && audioInConns[bi]->is_connected)
                    std::memcpy(outC->buffer, audioInConns[bi]->buffer, busSamples * sizeof(float));
                else
                    std::memset(outC->buffer, 0, busSamples * sizeof(float));
            }
        }
    } else {
        // No plugin: passthrough per bus
        for (size_t bi = 0; bi < audioOutConns.size(); ++bi) {
            auto* outC = audioOutConns[bi];
            if (!outC || !outC->buffer) continue;
            size_t busSamples = static_cast<size_t>(bufferSize) * static_cast<size_t>(outC->numChannels);
            if (bi < audioInConns.size() && audioInConns[bi] && audioInConns[bi]->is_connected)
                std::memcpy(outC->buffer, audioInConns[bi]->buffer, busSamples * sizeof(float));
            else
                std::memset(outC->buffer, 0, busSamples * sizeof(float));
        }
    }
}

// ============================================================================
// renderContent
// ============================================================================

void VstNode::renderContent(SDL_Renderer* renderer) {
#ifdef __EMSCRIPTEN__
    if (fonts.mainFont) {
        SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont,
            "VST not supported on browsers", 0, SDL_Color{220, 220, 220, 255});
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_FRect tr{10, 10, static_cast<float>(surf->w), static_cast<float>(surf->h)};
            SDL_RenderTexture(renderer, tex, nullptr, &tr);
            SDL_DestroyTexture(tex);
            SDL_DestroySurface(surf);
        }
    }
    return;
#else

    if (!vCount) {
        vCount = 4;
        vx = new float[vCount];
        vy = new float[vCount];
        vx[0] = 0; vx[1] = NODE_W; vx[2] = NODE_W; vx[3] = 0;
        vy[0] = 0; vy[1] = 0; vy[2] = NODE_H; vy[3] = NODE_H;
    }

    SDL_SetRenderDrawColor(renderer, 24, 24, 28, 255);
    SDL_FRect full{0, 0, NODE_W, NODE_H};
    SDL_RenderFillRect(renderer, &full);

    if (!fonts.mainFont) return;

    auto renderText = [&](const std::string& text, float x, float y, SDL_Color color) {
        SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, text.c_str(), 0, color);
        if (!surf) return;
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_FRect tr{x, y, static_cast<float>(surf->w), static_cast<float>(surf->h)};
        SDL_RenderTexture(renderer, tex, nullptr, &tr);
        SDL_DestroyTexture(tex);
        SDL_DestroySurface(surf);
    };

    SDL_Color white{220, 220, 220, 255};
    SDL_Color grey{150, 150, 155, 255};

    // Dropdown button
    std::string displayName = plugin ? plugin->getName() : "Select VST...";
    if (displayName.empty()) displayName = "(unnamed)";
    if (displayName.size() > 22) displayName = displayName.substr(0, 22) + "...";

    bool ddHover = (msX >= dropdownRect_.x && msX < dropdownRect_.x + dropdownRect_.w &&
                    msY >= dropdownRect_.y && msY < dropdownRect_.y + dropdownRect_.h);
    SDL_SetRenderDrawColor(renderer, ddHover ? 48 : 38, ddHover ? 48 : 38, ddHover ? 52 : 42, 255);
    SDL_RenderFillRect(renderer, &dropdownRect_);
    SDL_SetRenderDrawColor(renderer, 70, 70, 74, 255);
    SDL_RenderRect(renderer, &dropdownRect_);
    renderText(displayName, dropdownRect_.x + 6, dropdownRect_.y + 4, white);

    // Dropdown arrow
    float ax = dropdownRect_.x + dropdownRect_.w - 12.f;
    float ay = dropdownRect_.y + 8.f;
    SDL_SetRenderDrawColor(renderer, 180, 180, 180, 255);
    SDL_RenderLine(renderer, ax, ay, ax + 4, ay + 6);
    SDL_RenderLine(renderer, ax + 4, ay + 6, ax + 8, ay);

    // Editor toggle button
    if (plugin && plugin->isValid()) {
        bool hovered = (msX >= editorBtnRect_.x && msX < editorBtnRect_.x + editorBtnRect_.w &&
                        msY >= editorBtnRect_.y && msY < editorBtnRect_.y + editorBtnRect_.h);
        SDL_SetRenderDrawColor(renderer, hovered ? 65 : 50, hovered ? 65 : 50, hovered ? 70 : 55, 255);
        SDL_RenderFillRect(renderer, &editorBtnRect_);
        SDL_SetRenderDrawColor(renderer, 100, 100, 105, 255);
        SDL_RenderRect(renderer, &editorBtnRect_);
        const char* label = plugin->isEditorOpen() ? "Hide Editor" : "Show Editor";
        renderText(label, editorBtnRect_.x + 6, editorBtnRect_.y + 4, white);
    }

    if (plugin && plugin->isEditorOpen()) {
        plugin->tickEditor();
    }
#endif
}

// ============================================================================
// handleWindowInput
// ============================================================================

void VstNode::handleWindowInput(SDL_Event& e) {
    Node::handleWindowInput(e);
    if (plugin && plugin->isEditorOpen()) {
        plugin->tickEditor();
    }
}

// ============================================================================
// handleCustomInput
// ============================================================================

bool VstNode::handleCustomInput(SDL_Event& e) {
#ifdef __EMSCRIPTEN__
    return false;
#endif

    if (e.type == SDL_EVENT_DROP_FILE) {
        const char* path = e.drop.data;
        if (path) {
            std::cout << "[VstNode] drop file: " << path << std::endl;
            loadPlugin(path, true);
            return true;
        }
    }

    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        SDL_FPoint pt{msX, msY};

        // Editor button click
        if (SDL_PointInRectFloat(&pt, &editorBtnRect_)) {
            if (plugin && plugin->isValid()) {
                if (plugin->isEditorOpen()) {
                    // Catch edits made since the last poll before the editor goes away.
                    pollVstStateForUndo(true);
                    plugin->hideEditor();
                } else {
                    plugin->showEditor();
                    wirePluginCallbacks();
                }
            }
            return true;
        }

        // Dropdown: open context menu with history
        if (SDL_PointInRectFloat(&pt, &dropdownRect_)) {
            auto* ctxMenu = ContextMenu::get();
            ctxMenu->activate();

            auto root = uTreeEntry();
            root->label = "VST Plugin";

            auto history = Settings::instance().getVstHistory();
            for (const auto& p : history) {
                std::string name = p;
                size_t slash = name.find_last_of("/\\");
                if (slash != std::string::npos) name = name.substr(slash + 1);
                size_t dot = name.find_last_of('.');
                if (dot != std::string::npos) name = name.substr(0, dot);
                if (name.size() > 35) name = name.substr(0, 35) + "...";

                auto item = uTreeEntry();
                item->label = name;
                item->click = [this, p]() { loadPlugin(p, true); };
                root->addChild(item);
            }

            auto loadFile = uTreeEntry();
            loadFile->label = "Load from file...";
            loadFile->click = [this]() {
                std::string path = openFileDialog();
                if (!path.empty()) loadPlugin(path, true);
            };
            root->addChild(loadFile);

            ctxMenu->dynamicTick = getTreeMenuTicker(root);
            return true;
        }
    }

    return false;
}

// ============================================================================
// loadPlugin / unloadPlugin
// ============================================================================

void VstNode::loadPlugin(const std::string& path, bool createUndo) {

    if (createUndo) {
        // --- GUI thread: create/destroy the shared instance ---
        json oldState = json::object();
        if (plugin && plugin->isValid()) {
            oldState["path"] = loadedPath;
            oldState["bypass"] = bypass.value;
            oldState["compState"] = jsonBytesEncode(plugin->getComponentState());
            oldState["ctrlState"] = jsonBytesEncode(plugin->getControllerState());
        }

        if (plugin) {
            if (ne != nullptr) plugin->hideEditor();
            plugin = nullptr;
        }
        VstPluginCache::instance().removePlugin(static_cast<int>(id), nm ? nm->managerPath : std::vector<int>{});

        loadedPath = path;
        plugin = VstPluginCache::instance().getOrCreatePlugin(static_cast<int>(id), nm ? nm->managerPath : std::vector<int>{}, path);
        if (plugin && plugin->isValid()) {
            name = plugin->getName();
            wirePluginCallbacks();
            rebuildConnections();
            nm->markTopologyDirty(); // force setup() via update() on next audio cycle
            Settings::instance().addVstToHistory(path);
            // Don't call setup() on GUI — audio thread handles it to avoid races
        } else {
            name = "VST Plugin";
            plugin = nullptr;
            rebuildConnections();
            nm->markTopologyDirty();
        }

        if (project && project->um) {
            std::vector<int> mgrPath = nm ? nm->managerPath : std::vector<int>{};
            json newState = json::object();
            newState["path"] = loadedPath;
            newState["bypass"] = bypass.value;
            if (plugin && plugin->isValid()) {
                newState["compState"] = jsonBytesEncode(plugin->getComponentState());
                newState["ctrlState"] = jsonBytesEncode(plugin->getControllerState());
            }
            auto* pa = new VstLoadPluginAction(project, std::move(mgrPath), static_cast<int>(id),
                                                std::move(oldState), std::move(newState));
            project->um->newAction(pa);
        }
    } else {
        // --- Undo/audio thread: retrieve existing instance, don't destroy ---
        std::cout << "[VstNode::loadPlugin] sync-path nodeID=" << static_cast<int>(id)
                  << " path=" << path << " ne=" << (ne ? "gui" : "audio") << std::endl;
        if (plugin) plugin->hideEditor();
        loadedPath = path;
        plugin = VstPluginCache::instance().getOrCreatePlugin(static_cast<int>(id), nm ? nm->managerPath : std::vector<int>{}, path);
        if (plugin && plugin->isValid()) {
            name = plugin->getName();
            wirePluginCallbacks();
            rebuildConnections();
            Settings::instance().addVstToHistory(path);
            int sr = sampleRate > 0 ? sampleRate : 48000;
            int bs = bufferSize > 0 ? bufferSize : 1024;
            plugin->setup(sr, bs);
        } else {
            name = "VST Plugin";
            plugin = nullptr;
            rebuildConnections();
        }
    }
}

void VstNode::unloadPlugin() {
    if (plugin) {
        plugin->hideEditor();
        plugin = nullptr;
    }
    VstPluginCache::instance().removePlugin(static_cast<int>(id), nm ? nm->managerPath : std::vector<int>{});
    loadedPath.clear();
    name = "VST Plugin";
    rebuildConnections();
}

void VstNode::onPluginParameterChange(int paramID, float oldValue, float newValue) {
    if (!project || !project->um || !plugin) return;
    if (restoringState) return;
    // Suppress undo when a mapped connection owns the parameter.
    auto it = mappedVstParams.find(paramID);
    if (it != mappedVstParams.end() && it->second && it->second->is_connected)
        return;
    std::vector<int> mgrPath = nm ? nm->managerPath : std::vector<int>{};
    // Coalesce rapid edits of the same parameter (drag)
    if (project->um->current && project->um->current->type == VstParameterChange) {
        auto* prev = static_cast<VstParameterChangeAction*>(project->um->current);
        if (prev->nodeID == static_cast<int>(id)
            && prev->paramID == static_cast<uint32_t>(paramID)
            && prev->managerPath == mgrPath) {
            prev->newValue = newValue;
            ProjectAction* cap = prev;
            project->um->enqueueAudioSync([cap]() { cap->doAction(); });
            vstStateBaselineDirty = true;
            return;
        }
    }
    // No usable old value (plugin skipped beginEdit) — leave the change to the
    // state-diff poller instead of recording a no-op action that would also
    // suppress the diff.
    if (oldValue == newValue) return;
    auto* pa = new VstParameterChangeAction(project, std::move(mgrPath), static_cast<int>(id),
                                             static_cast<uint32_t>(paramID), oldValue, newValue);
    project->um->newAction(pa);
    vstStateBaselineDirty = true;
    requestStatePollSoon();
}

void VstNode::wirePluginCallbacks() {
    if (!plugin || !plugin->isValid()) return;
    auto* frame = plugin->getHostFrame();
    if (!frame) return;
    mpeRangeSet = false;
    // onBeginEdit: snapshot the current parameter value BEFORE the edit
    frame->onBeginEdit = [this](Steinberg::Vst::ParamID id) {
        if (plugin) {
            float cur = plugin->getParameterValue(static_cast<int>(id));
            plugin->getHostFrame()->capturePreEditValue(id, static_cast<Steinberg::Vst::ParamValue>(cur));
        }
    };
    // onPerformEdit: receives (paramID, oldValue, newValue)
    frame->onPerformEdit = [this](Steinberg::Vst::ParamID paramID,
                                    Steinberg::Vst::ParamValue oldVal,
                                    Steinberg::Vst::ParamValue newVal) {
        onPluginParameterChange(static_cast<int>(paramID),
                                static_cast<float>(oldVal), static_cast<float>(newVal));
    };
    // onStatePoll: state-diff undo for edits that never reach performEdit
    frame->onStatePoll = [this](bool force) { pollVstStateForUndo(force); };
    frame->activeGestureCount = 0;
    stateBaselineValid_ = false;
    vstStateBaselineDirty = true;
}

// ============================================================================
// State-diff undo
// ============================================================================
//
// Anything the user changes inside the plugin GUI that is not an exposed
// parameter edit (Vital's LFO-to-knob drags, mod-matrix routing, internal
// toggles that skip beginEdit, preset browsing) only shows up in the
// component/controller state blobs. While the editor is open we snapshot those
// blobs at an interval and turn any unexplained difference into an undoable
// VstStateChangeAction.

void VstNode::pollVstStateForUndo(bool force) {
    if (!plugin || !plugin->isValid() || restoringState) return;
    if (!project || !project->um) return;

    // Mid-gesture (knob drag): the blob is churning with the dragged value,
    // which the param action already records. endEdit forces a poll when done.
    auto* frame = plugin->getHostFrame();
    if (frame && frame->activeGestureCount > 0) return;

    uint64_t now = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    if (!force && now - lastStatePollMs_ < 500) return;
    lastStatePollMs_ = now;

    // A mapped, driven parameter rewrites plugin state continuously, and a
    // byte diff can't separate its movement from user edits. Only when one has
    // actually moved since the baseline snapshot, re-baseline silently; idle
    // mappings don't block state undo.
    bool mappedMoved = false;
    for (auto& [pid, conn] : mappedVstParams) {
        if (!conn || !conn->is_connected) continue;
        auto it = mappedBaselineValues_.find(pid);
        if (it == mappedBaselineValues_.end() || it->second != plugin->getParameterValue(pid)) {
            mappedMoved = true;
            break;
        }
    }

    auto comp = plugin->getComponentState();
    auto ctrl = plugin->getControllerState();
    if (comp.empty() && ctrl.empty()) return;

    if (mappedMoved || vstStateBaselineDirty || !stateBaselineValid_) {
        stateBaselineComp_ = std::move(comp);
        stateBaselineCtrl_ = std::move(ctrl);
        stateBaselineValid_ = true;
        vstStateBaselineDirty = false;
        mappedBaselineValues_.clear();
        for (auto& [pid, conn] : mappedVstParams)
            if (conn && conn->is_connected)
                mappedBaselineValues_[pid] = plugin->getParameterValue(pid);
        return;
    }

    if (comp == stateBaselineComp_ && ctrl == stateBaselineCtrl_) return;

    // No coalescing here: mutating an already-visible action would rewrite
    // history (redo would replay a later edit). Continuous drags don't spam
    // actions because polling waits for pointer-button release.
    json newState = json::object();
    newState["compState"] = jsonBytesEncode(comp);
    newState["ctrlState"] = jsonBytesEncode(ctrl);
    std::vector<int> mgrPath = nm ? nm->managerPath : std::vector<int>{};

    json oldState = json::object();
    oldState["compState"] = jsonBytesEncode(stateBaselineComp_);
    oldState["ctrlState"] = jsonBytesEncode(stateBaselineCtrl_);
    auto* pa = new VstStateChangeAction(project, std::move(mgrPath), static_cast<int>(id),
                                        std::move(oldState), std::move(newState), true);
    project->um->newAction(pa);
    stateBaselineComp_ = std::move(comp);
    stateBaselineCtrl_ = std::move(ctrl);
}

void VstNode::requestStatePollSoon() {
#ifndef __EMSCRIPTEN__
    if (plugin)
        if (auto* frame = plugin->getHostFrame())
            frame->statePollRequested = true;
#endif
}

// ============================================================================
// Serialization
// ============================================================================

json VstNode::extraSerialize() {
    json j;
    j["bypass"] = bypass.value;
    if (plugin && plugin->isValid()) {
        j["pluginPath"] = loadedPath;
        j["compState"] = jsonBytesEncode(plugin->getComponentState());
        j["ctrlState"] = jsonBytesEncode(plugin->getControllerState());
    }
    if (!mappedVstParams.empty()) {
        j["mappedVstParams"] = json::object();
        for (auto& [paramID, conn] : mappedVstParams)
            j["mappedVstParams"][std::to_string(paramID)] = conn->id;
    }
    return j;
}

void VstNode::extraDeSerialize(const json& j) {
    bypass.value = j.value("bypass", 0.0f);
    if (j.contains("pluginPath")) {
        loadedPath = j["pluginPath"].get<std::string>();
        plugin = VstPluginCache::instance().getOrCreatePlugin(static_cast<int>(id), nm ? nm->managerPath : std::vector<int>{}, loadedPath);
        if (plugin && plugin->isValid()) {
            name = plugin->getName();
            wirePluginCallbacks();
            restoringState = true;
            if (j.contains("compState"))
                plugin->setComponentState(jsonBytesDecode(j["compState"]));
            if (j.contains("ctrlState"))
                plugin->setControllerState(jsonBytesDecode(j["ctrlState"]));
            restoringState = false;
            rebuildConnections();
            int sr = sampleRate > 0 ? sampleRate : 48000;
            int bs = bufferSize > 0 ? bufferSize : 1024;
            plugin->setup(sr, bs);
        } else {
            plugin = nullptr;
            rebuildConnections();
        }
    } else {
        loadedPath.clear();
    }

    if (j.contains("mappedVstParams")) {
        for (auto& [key, val] : j["mappedVstParams"].items()) {
            int paramID = std::stoi(key);
            uint16_t connID = val;
            auto* c = new Connection;
            c->nm = inputs.nm;
            c->id = connID;
            c->type = DataType::Waveform;
            c->dir = Direction::input;
            c->is_connected = false;
            c->output_connection = c->id;
            c->output_node = inputs.nodeID;
            c->input_connection = -1;
            c->input_node = -1;
            c->events = nullptr;
            c->buffer = nullptr;
            c->bufferSize = 0;
            c->label = plugin ? plugin->getParameterNameByID(paramID) : ("VST Param " + std::to_string(paramID));
            inputs.connections.push_back(c);
            inputs.id_pool.reserveID(c->id);
            inputs.ids[c->id] = static_cast<uint16_t>(inputs.connections.size() - 1);
            mappedVstParams[paramID] = c;
        }
    }
}

// ============================================================================
// MPE channel allocator
// ============================================================================

int VstNode::allocMpeChannel(int noteId, void* eventList) {
    auto it = noteChannels.find(noteId);
    if (it != noteChannels.end()) return it->second;
    bool used[16] = {};
    for (auto& [id, ch] : noteChannels) used[ch] = true;
    int ch = nextMpeChannel;
    bool found = false;
    for (int tries = 0; tries < 15; tries++) {
        if (ch == 0) ch = 1;
        if (!used[ch]) { found = true; break; }
        if (++ch > 15) ch = 1;
    }
    if (!found) {
        // All 15 MPE channels in use — evict the first entry and send note-off
        if (!noteChannels.empty()) {
            auto oldest = noteChannels.begin();
            ch = oldest->second;
            int evictedId = oldest->first;
            noteChannels.erase(oldest);
            evictedNoteChannels[evictedId] = ch;

            // Send note-off for the evicted note so it doesn't ring forever
            auto& out = *static_cast<HostEventList*>(eventList);
            Steinberg::Vst::Event off{};
            off.busIndex = 0;
            off.sampleOffset = 0;
            off.flags = 0;
            off.type = Steinberg::Vst::Event::kNoteOffEvent;
            off.noteOff.channel = static_cast<Steinberg::int16>(ch);
            off.noteOff.pitch = 0;
            off.noteOff.tuning = 0;
            off.noteOff.velocity = 0.0f;
            off.noteOff.noteId = evictedId;
            out.addEvent(off);
        } else {
            ch = 1;
        }
    }
    nextMpeChannel = (ch % 15) + 1;
    noteChannels[noteId] = ch;
    return ch;
}

void VstNode::freeMpeChannel(int noteId) {
    noteChannels.erase(noteId);
}

void VstNode::sendMpePitchBendRange(void* eventList) {
    if (mpeRangeSet) return;
    mpeRangeSet = true;
    auto& out = *static_cast<HostEventList*>(eventList);

    auto rpn = [&](int ch, int cc, int val) {
        Steinberg::Vst::Event ev{};
        ev.busIndex = 0;
        ev.type = Steinberg::Vst::Event::kLegacyMIDICCOutEvent;
        ev.midiCCOut.controlNumber = static_cast<Steinberg::uint8>(cc);
        ev.midiCCOut.channel = static_cast<Steinberg::int8>(ch);
        ev.midiCCOut.value = static_cast<Steinberg::int8>(val);
        ev.midiCCOut.value2 = 0;
        out.addEvent(ev);
    };

    // RPN 6 (MPE Configuration): lower zone, master ch 0, 15 member channels
    rpn(0, 101, 0); rpn(0, 100, 6);
    rpn(0, 6, 15);  rpn(0, 38, 0);
    rpn(0, 101, 127); rpn(0, 100, 127);

    // RPN 0 (Pitch Bend Sensitivity) = ±2 semitones (MPE standard)
    for (int ch = 0; ch <= 15; ++ch) {
        rpn(ch, 101, 0); rpn(ch, 100, 0);
        rpn(ch, 6, 2);   rpn(ch, 38, 0);
        rpn(ch, 101, 127); rpn(ch, 100, 127);
    }
}

// ============================================================================
// VST parameter mapping
// ============================================================================

void VstNode::mapVstParameter(int paramID) {
    if (!project || !project->um) return;
    std::vector<int> mgrPath = nm ? nm->managerPath : std::vector<int>{};
    auto* pa = new MapParameterUndoAction(project, std::move(mgrPath), static_cast<int>(id), 0, paramID);
    project->um->newAction(pa);
}

void VstNode::unmapVstParameter(int paramID) {
    if (!project || !project->um) return;
    std::vector<int> mgrPath = nm ? nm->managerPath : std::vector<int>{};
    auto* pa = new UnmapParameterUndoAction(project, std::move(mgrPath), static_cast<int>(id), 0, paramID);
    project->um->newAction(pa);
}

bool VstNode::mapVstParameterNow(int paramID) {
    if (!nm || mappedVstParams.count(paramID)) return false;

    auto* c = new Connection;
    c->type = DataType::Waveform;
    c->dir = Direction::input;
    inputs.addConnection(c);
    c->label = plugin ? plugin->getParameterNameByID(paramID) : ("VST Param " + std::to_string(paramID));

    mappedVstParams[paramID] = c;
    makeConnectionRects();
    nm->markTopologyDirty();
    return true;
}

bool VstNode::unmapVstParameterNow(int paramID) {
    auto it = mappedVstParams.find(paramID);
    if (it == mappedVstParams.end()) return false;

    Connection* mc = it->second;
    if (mc->is_connected)
        nm->severConnectionNow(static_cast<uint16_t>(mc->input_node),
                               static_cast<uint16_t>(mc->input_connection),
                               id, mc->id);

    auto cit = std::find(inputs.connections.begin(), inputs.connections.end(), mc);
    if (cit != inputs.connections.end()) {
        inputs.id_pool.releaseID(mc->id);
        inputs.ids.erase(mc->id);
        inputs.connections.erase(cit);
    }
    delete mc;
    mappedVstParams.erase(it);

    inputs.ids.clear();
    for (size_t i = 0; i < inputs.connections.size(); ++i)
        inputs.ids[inputs.connections[i]->id] = static_cast<uint16_t>(i);

    makeConnectionRects();
    nm->markTopologyDirty();
    return true;
}

std::shared_ptr<TreeEntry> VstNode::getNodeMenu() {
    auto t = uTreeEntry();
    t->label = "Node Menu";

    if (this->id) {
        auto remove = uTreeEntry();
        remove->label = "Remove Node";
        remove->click = [this] () { nm->removeNode(this); };
        t->addChild(remove);
    }

    auto mapParam = uTreeEntry();
    mapParam->label = "Map Parameter";
    mapParam->maxListHeight = 300.f;

    // Regular node params (bypass)
    for (size_t pi = 0; pi < params.size(); ++pi) {
        auto* p = params[pi];
        if (!p) continue;
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

    // VST plugin params
    if (plugin && plugin->isValid()) {
        int count = plugin->getParameterCount();
        for (int i = 0; i < count; ++i) {
            // Map by the parameter's stable ParamID, not its list index — the
            // ID is what queueParameterChange/setParamNormalized expect.
            int pid = plugin->getParameterID(i);
            if (pid == -1) continue;
            auto item = uTreeEntry();
            bool mapped = (mappedVstParams.count(pid) > 0);
            item->label = (mapped ? "[M] " : "") + plugin->getParameterName(i);
            if (mapped)
                item->click = [this, pid]() { unmapVstParameter(pid); };
            else
                item->click = [this, pid]() { mapVstParameter(pid); };
            mapParam->addChild(item);
        }
    }

    if (mapParam->children.size() > 0)
        t->addChild(mapParam);

    return t;
}
