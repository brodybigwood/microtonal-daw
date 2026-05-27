#include "vstnode.h"
#include "NodeManager.h"
#include "styles.h"
#include "UndoManager.h"
#include "NodeProcessor.h"
#include <algorithm>
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

    // Count total channels needed
    int totalInChannels = 0, totalOutChannels = 0;
    for (int b = 0; b < numAudioInBuses; ++b)
        totalInChannels += plugin->getAudioInputChannels(b);
    for (int b = 0; b < numAudioOutBuses; ++b)
        totalOutChannels += plugin->getAudioOutputChannels(b);

    auto chLabel = [](const char* prefix, int bus, int ch, int totalCh) {
        if (totalCh <= 2)
            return std::string(prefix) + (ch == 0 ? " L" : " R");
        return std::string(prefix) + " " + std::to_string(bus + 1) + ":" + std::to_string(ch + 1);
    };

    // Grow or reuse audio input connections
    while (static_cast<int>(audioInConns.size()) < totalInChannels) {
        auto* c = new Connection;
        c->type = DataType::Waveform;
        c->dir = Direction::input;
        inputs.addConnection(c);
        audioInConns.push_back(c);
        inChannelMap.push_back({0, 0});
    }

    // Grow or reuse audio output connections
    while (static_cast<int>(audioOutConns.size()) < totalOutChannels) {
        auto* c = new Connection;
        c->type = DataType::Waveform;
        c->dir = Direction::output;
        outputs.addConnection(c);
        audioOutConns.push_back(c);
        outChannelMap.push_back({0, 0});
    }

    // Update channel maps and labels
    int idx = 0;
    inChannelMap.clear();
    for (int b = 0; b < numAudioInBuses; ++b) {
        int channels = plugin->getAudioInputChannels(b);
        for (int ch = 0; ch < channels && idx < static_cast<int>(audioInConns.size()); ++ch) {
            inChannelMap.push_back({b, ch});
            audioInConns[idx]->label = chLabel("In", b, ch, channels);
            ++idx;
        }
    }
    // Hide unused input connections
    for (int i = idx; i < static_cast<int>(audioInConns.size()); ++i)
        audioInConns[i]->label = "";

    idx = 0;
    outChannelMap.clear();
    for (int b = 0; b < numAudioOutBuses; ++b) {
        int channels = plugin->getAudioOutputChannels(b);
        for (int ch = 0; ch < channels && idx < static_cast<int>(audioOutConns.size()); ++ch) {
            outChannelMap.push_back({b, ch});
            audioOutConns[idx]->label = chLabel("Out", b, ch, channels);
            ++idx;
        }
    }
    for (int i = idx; i < static_cast<int>(audioOutConns.size()); ++i)
        audioOutConns[i]->label = "";

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
    static int callCount = 0;
    bool hasOut = false;
    for (size_t ci = 0; ci < outChannelMap.size(); ++ci) {
        if (ci < audioOutConns.size() && audioOutConns[ci] && audioOutConns[ci]->buffer)
            hasOut = true;
    }
    if (!hasOut) {
        if (callCount == 0) std::cout << "[VstNode::process] no outputs, map=" << outChannelMap.size()
            << " conns=" << audioOutConns.size() << " plugin=" << (plugin ? "yes" : "no") << std::endl;
        callCount++;
        return;
    }

    // Silence active outputs
    for (size_t ci = 0; ci < outChannelMap.size(); ++ci) {
        if (ci < audioOutConns.size() && audioOutConns[ci] && audioOutConns[ci]->buffer)
            std::memset(audioOutConns[ci]->buffer, 0, bufferSize * sizeof(float));
    }

    auto localPlugin = plugin; // hold ref across process call
    if (localPlugin && localPlugin->isValid()) {
        bool bypassed = bypass.value > 0.5f;
        localPlugin->setBypassed(bypassed);
        if (!bypassed) {
            // Route audio inputs — interleave mono connections into bus interleaved buffers
            for (size_t ci = 0; ci < inChannelMap.size() && ci < audioInConns.size(); ++ci) {
                auto* c = audioInConns[ci];
                if (!c || !c->is_connected || !c->buffer) continue;
                auto [bus, ch] = inChannelMap[ci];
                if (bus >= static_cast<int>(localPlugin->inputBusData.size())) continue;
                auto& data = localPlugin->inputBusData[bus];
                for (int s = 0; s < bufferSize; ++s)
                    data[ch * bufferSize + s] = c->buffer[s];
            }

            // Convert DAW Events to VST3 EventList with MPE
            HostEventList vstEvents;
            sendMpePitchBendRange(&vstEvents);
            if (midiIn && midiIn->is_connected && midiIn->events) {
                for (auto& ev : *midiIn->events) {
                    Steinberg::int16 base = static_cast<Steinberg::int16>(std::floor(ev.num));
                    float frac = ev.num - base;
                    float cents = frac * 100.0f;

                    if (ev.type == noteOn) {
                        int mpeCh = allocMpeChannel(ev.id, &vstEvents);

                        // Send pitch bend on valid MPE channel BEFORE note-on
                        if (mpeCh > 0) {
                            Steinberg::int32 pb14 = 8192;
                            if (frac != 0.0f) {
                                // pitch bend range: ±2 semitones (MPE standard)
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
                            // Check if this note was evicted — route note-off to its original channel
                            auto eit = evictedNoteChannels.find(ev.id);
                            if (eit != evictedNoteChannels.end()) {
                                mpeCh = eit->second;
                                evictedNoteChannels.erase(eit);
                            }
                        }
                        if (mpeCh < 0) continue; // note already turned off by eviction or never existed

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

            localPlugin->processAudio(bufferSize, &vstEvents);

            // Route audio outputs — deinterleave bus interleaved buffers into mono connections
            static int routLog = 0;
            routLog++;
            for (size_t ci = 0; ci < outChannelMap.size() && ci < audioOutConns.size(); ++ci) {
                auto* c = audioOutConns[ci];
                if (!c || !c->buffer) continue;
                auto [bus, ch] = outChannelMap[ci];
                if (bus >= static_cast<int>(localPlugin->outputBusData.size())) continue;
                auto& data = localPlugin->outputBusData[bus];
                int nch = localPlugin->getAudioOutputChannels(bus);
                for (int s = 0; s < bufferSize; ++s)
                    c->buffer[s] = data[ch * bufferSize + s];
                if (routLog == 1) {
                    float peak = 0;
                    for (int s = 0; s < bufferSize; ++s) peak = std::max(peak, std::abs(c->buffer[s]));
                    std::cout << "[VstNode] output ch=" << ch << " peak=" << peak << " nch=" << nch << std::endl;
                }
            }
        } else {
            // Bypass: passthrough all channels
            for (size_t ci = 0; ci < outChannelMap.size() && ci < audioOutConns.size(); ++ci) {
                auto* outC = audioOutConns[ci];
                if (!outC || !outC->buffer) continue;
                if (ci < audioInConns.size() && audioInConns[ci] && audioInConns[ci]->is_connected)
                    std::memcpy(outC->buffer, audioInConns[ci]->buffer, bufferSize * sizeof(float));
                else
                    std::memset(outC->buffer, 0, bufferSize * sizeof(float));
            }
        }
    } else {
        // No plugin: passthrough all channels
        for (size_t ci = 0; ci < outChannelMap.size() && ci < audioOutConns.size(); ++ci) {
            auto* outC = audioOutConns[ci];
            if (!outC || !outC->buffer) continue;
            if (ci < audioInConns.size() && audioInConns[ci] && audioInConns[ci]->is_connected)
                std::memcpy(outC->buffer, audioInConns[ci]->buffer, bufferSize * sizeof(float));
            else
                std::memset(outC->buffer, 0, bufferSize * sizeof(float));
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
            SDL_FRect tr{100, 200, static_cast<float>(surf->w), static_cast<float>(surf->h)};
            SDL_RenderTexture(renderer, tex, nullptr, &tr);
            SDL_DestroyTexture(tex);
            SDL_DestroySurface(surf);
        }
    }
    return;
#else

    SDL_SetRenderDrawColor(renderer, 24, 24, 28, 255);
    SDL_RenderFillRect(renderer, nullptr);

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

    // Plugin name
    std::string displayName = plugin ? plugin->getName() : "No Plugin Loaded";
    if (displayName.empty()) displayName = "No Plugin Loaded";
    renderText(displayName, nameRect.x, nameRect.y, white);

    // I/O info
    if (plugin && plugin->isValid()) {
        int ai = plugin->getNumAudioInputs();
        int ao = plugin->getNumAudioOutputs();
        int ei = plugin->getNumEventInputs();
        int eo = plugin->getNumEventOutputs();
        std::string ioInfo = "Audio: " + std::to_string(ai) + " in / " + std::to_string(ao) + " out";
        if (ei > 0 || eo > 0)
            ioInfo += "  MIDI: " + std::to_string(ei) + " in / " + std::to_string(eo) + " out";
        renderText(ioInfo, 40, 60, grey);
    }

    // Bypass label
    renderText("Bypass", 38, TEX_H * 0.3f + 50, white);

    // Load button (hidden once a plugin is loaded)
    if (!plugin || !plugin->isValid()) {
        SDL_SetRenderDrawColor(renderer, 50, 50, 55, 255);
        SDL_RenderFillRect(renderer, &loadBtnRect);
        SDL_SetRenderDrawColor(renderer, 100, 100, 105, 255);
        SDL_RenderRect(renderer, &loadBtnRect);
        renderText("Load Plugin...", loadBtnRect.x + 12, loadBtnRect.y + 6, white);
    }

    // Editor toggle button
    if (plugin && plugin->isValid()) {
        SDL_SetRenderDrawColor(renderer, 50, 50, 55, 255);
        SDL_RenderFillRect(renderer, &editorBtnRect);
        SDL_SetRenderDrawColor(renderer, 100, 100, 105, 255);
        SDL_RenderRect(renderer, &editorBtnRect);
        const char* label = plugin->isEditorOpen() ? "Hide Editor" : "Show Editor";
        renderText(label, editorBtnRect.x + 12, editorBtnRect.y + 6, white);
    }

    // Vendor info
    if (plugin && plugin->isValid()) {
        renderText("Vendor: " + plugin->getVendor(), 40, 170, grey);
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
        if (plugin && plugin->isValid()) return false;
        const char* path = e.drop.data;
        if (path) {
            std::cout << "[VstNode] drop file: " << path << std::endl;
            loadPlugin(path, true);
            return true;
        }
    }

    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
        SDL_FPoint pt{msX, msY};

        if (SDL_PointInRectFloat(&pt, &loadBtnRect)) {
            if (!plugin || !plugin->isValid()) {
                std::string path = openFileDialog();
                if (!path.empty()) loadPlugin(path, true);
            }
            return true;
        }

        if (SDL_PointInRectFloat(&pt, &editorBtnRect)) {
            if (plugin && plugin->isValid()) {
                if (plugin->isEditorOpen()) {
                    plugin->hideEditor();
                } else {
                    plugin->showEditor();
                }
            }
            return true;
        }
    }

    return false;
}

// ============================================================================
// loadPlugin / unloadPlugin
// ============================================================================

void VstNode::loadPlugin(const std::string& path, bool createUndo) {
    // Once a plugin is loaded, don't allow replacement via UI
    if (createUndo && plugin && plugin->isValid()) return;

    if (createUndo) {
        // --- GUI thread: create/destroy the shared instance ---
        json oldState = json::object();
        if (plugin && plugin->isValid()) {
            oldState["path"] = loadedPath;
            oldState["bypass"] = bypass.value;
            auto compState = plugin->getComponentState();
            auto ctrlState = plugin->getControllerState();
            oldState["compState"] = compState;
            oldState["ctrlState"] = ctrlState;
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
            // Don't call setup() on GUI — audio thread handles it to avoid races
        } else {
            name = "VST Plugin";
            plugin = nullptr;
            rebuildConnections();
        }

        if (project && project->um) {
            std::vector<int> mgrPath = nm ? nm->managerPath : std::vector<int>{};
            json newState = json::object();
            newState["path"] = loadedPath;
            newState["bypass"] = bypass.value;
            if (plugin && plugin->isValid()) {
                auto compState = plugin->getComponentState();
                auto ctrlState = plugin->getControllerState();
                newState["compState"] = compState;
                newState["ctrlState"] = ctrlState;
            }
            auto* pa = new VstLoadPluginAction(project, std::move(mgrPath), static_cast<int>(id),
                                                std::move(oldState), std::move(newState));
            project->um->newAction(pa);
        }
    } else {
        // --- Undo/audio thread: retrieve existing instance, don't destroy ---
        std::cout << "[VstNode::loadPlugin] sync-path nodeID=" << static_cast<int>(id)
                  << " path=" << path << " ne=" << (ne ? "gui" : "audio") << std::endl;
        loadedPath = path;
        plugin = VstPluginCache::instance().getOrCreatePlugin(static_cast<int>(id), nm ? nm->managerPath : std::vector<int>{}, path);
        if (plugin && plugin->isValid()) {
            name = plugin->getName();
            wirePluginCallbacks();
            rebuildConnections();
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
            return;
        }
    }
    auto* pa = new VstParameterChangeAction(project, std::move(mgrPath), static_cast<int>(id),
                                             static_cast<uint32_t>(paramID), oldValue, newValue);
    project->um->newAction(pa);
}

void VstNode::wirePluginCallbacks() {
    if (!plugin || !plugin->isValid()) return;
    mpeRangeSet = false;
    auto* frame = plugin->getHostFrame();
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
}

// ============================================================================
// Serialization
// ============================================================================

json VstNode::extraSerialize() {
    json j;
    j["bypass"] = bypass.value;
    if (plugin && plugin->isValid()) {
        j["pluginPath"] = loadedPath;
        auto compState = plugin->getComponentState();
        auto ctrlState = plugin->getControllerState();
        j["compState"] = compState;
        j["ctrlState"] = ctrlState;
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
            if (j.contains("compState")) {
                auto& arr = j["compState"];
                std::vector<uint8_t> data(arr.begin(), arr.end());
                plugin->setComponentState(data);
            }
            if (j.contains("ctrlState")) {
                auto& arr = j["ctrlState"];
                std::vector<uint8_t> data(arr.begin(), arr.end());
                plugin->setControllerState(data);
            }
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
