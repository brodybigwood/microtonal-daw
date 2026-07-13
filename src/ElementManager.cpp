#include "ElementManager.h"
#include "Region.h"
#include "AutomationCurve.h"
#include <algorithm>
#include <cmath>
#include "UndoManager.h"
#include <SDL3/SDL_events.h>
#include <string>
#include "styles.h"
#include "Project.h"
#include "AudioManager.h"
#include "TrackManager.h"
#include "AudioClip.h"
#include "Note.h"
#include "NodeManager.h"
#include "nodes/arranger/arranger.h"
#include <unordered_set>

ElementManager::~ElementManager() {
    for (auto e : elements) {
        delete e;
    }
    elements.clear();
}

void ElementManager::clearTextures() {
    for (auto element : elements) {
        if (element->texture) {
            SDL_DestroyTexture(element->texture);
            element->texture = nullptr;
        }
    }
}

json ElementManager::toJSON() {
    json j;
    j["id_pool"] = id_pool.toJSON();
    j["position_id_pool"] = position_id_pool.toJSON();

    j["elements"] = json::array();

    for(auto e : elements) {
        json je = e->toJSON();
        j["elements"].push_back(je);
    }

    return j;
}

void ElementManager::process(int bufferSize) {
    float epsilon = 1e-3f;  // ~0.5ms at 120 BPM

    float beatWindow = static_cast<float>(project->deltaBeats);
    float currentBeat = static_cast<float>(project->activeBeatPosition());
    float sampleRate = static_cast<float>(AudioManager::instance()->sampleRate);

    auto eraseTrackVoice = [](Track* track, int voiceId) {
        track->dispatched.erase(
            std::remove_if(track->dispatched.begin(), track->dispatched.end(),
                [voiceId](const ActiveNote& active) { return active.voiceId == voiceId; }),
            track->dispatched.end());
    };
    auto emitNoteOff = [](Track* track, const ActiveNote& active, int offset) {
        if (!track || !active.note) return;
        track->addEvent(Event{noteEventType::noteOff, active.sourcePitch,
                              active.voiceId, offset, active.sourceChannel});
    };
    auto flushAllActiveNotes = [&]() {
        for (auto& t : tm->tracks) {
            for (const auto& active : t->dispatched)
                emitNoteOff(t, active, 0);
            t->dispatched.clear();
        }
        for (auto* element : elements)
            for (auto* pos : element->positions)
                pos->dispatched.clear();
    };

    // A seek is a discontinuity. Release old voices, then schedule only notes
    // whose starts occur in the new block (no MIDI note chasing).
    if (lastTransportGeneration != project->dspTransportGeneration) {
        flushAllActiveNotes();
        lastTransportGeneration = project->dspTransportGeneration;
    }

    if (!project->dspIsPlaying) {
        flushAllActiveNotes();
        return;
    }

    // Reconcile DSP edits. An active source note may have been deleted, or its
    // entire position/region may have disappeared between blocks.
    std::unordered_set<int> liveVoiceIds;
    for (auto* element : elements) {
        if (element->type != ElementType::region) continue;
        auto* region = static_cast<Region*>(element);
        for (auto* pos : element->positions) {
            Track* track = tm->getTrack(pos->trackID);
            if (!track) continue;
            auto& activeNotes = pos->dispatched;
            for (auto it = activeNotes.begin(); it != activeNotes.end();) {
                bool sourceStillExists = it->note &&
                    std::find(region->notes.begin(), region->notes.end(), it->note) != region->notes.end();
                if (sourceStillExists) {
                    sourceStillExists =
                        std::fabs(it->sourceStartBeat - it->note->startBeats()) < 1e-6f &&
                        std::fabs(it->sourceEndBeat - it->note->endBeats()) < 1e-6f &&
                        std::fabs(it->sourcePitch - it->note->num) < 1e-6f &&
                        it->sourceChannel == it->note->channel;
                }
                if (!sourceStillExists) {
                    emitNoteOff(track, *it, 0);
                    eraseTrackVoice(track, it->voiceId);
                    it = activeNotes.erase(it);
                } else {
                    liveVoiceIds.insert(it->voiceId);
                    ++it;
                }
            }
        }
    }
    for (Track* track : tm->tracks) {
        for (auto it = track->dispatched.begin(); it != track->dispatched.end();) {
            if (!liveVoiceIds.contains(it->voiceId)) {
                emitNoteOff(track, *it, 0);
                it = track->dispatched.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (auto* element : elements)
        for (auto position : element->positions) {
            auto& pos = *position;
            const float posStartBeat = Note::beatsFromVector(pos.rhythmVector);
            const float posEndBeat = Note::beatsFromVector(pos.rhythmEndVector);
            if (posStartBeat > currentBeat + beatWindow + 1e-5f) continue;

            Track* track = tm->getTrack(pos.trackID);
            if (!track) continue;
            auto& dispatched = pos.dispatched;
            switch (element->type) {
                case ElementType::region:
                    {
                        auto* region = static_cast<Region*>(element);
                        const float trim = Note::beatsFromVector(pos.startOffsetPairs);
                        for (auto& note : region->notes) {
                            float start = note->startBeats() + posStartBeat - trim;
                            if (start > posEndBeat) continue;
                            float end = note->endBeats() + posStartBeat - trim;

                            auto activeIt = std::find_if(dispatched.begin(), dispatched.end(),
                                [&note](const ActiveNote& active) { return active.note == note; });
                            const bool wasDispatched = activeIt != dispatched.end();

                            if (!wasDispatched && start < currentBeat + beatWindow && start + epsilon >= currentBeat) {
                                float secs = project->activeTempoCurve().secondsForBeats(currentBeat, start);
                                int offset = std::max(0, std::min(static_cast<int>(secs * sampleRate), bufferSize - 1));
                                ActiveNote active;
                                active.note = note;
                                active.voiceId = project->allocateDspVoiceId();
                                active.positionId = pos.id;
                                active.sourceStartBeat = note->startBeats();
                                active.sourceEndBeat = note->endBeats();
                                active.sourcePitch = note->num;
                                active.sourceChannel = note->channel;
                                Event event{noteEventType::noteOn, note->num, active.voiceId, offset, note->channel};
                                track->addEvent(event);
                                dispatched.push_back(active);
                                track->dispatched.push_back(active);
                                activeIt = std::prev(dispatched.end());
                            }

                            activeIt = std::find_if(dispatched.begin(), dispatched.end(),
                                [&note](const ActiveNote& active) { return active.note == note; });
                            const bool isDispatched = activeIt != dispatched.end();
                            if (isDispatched && end < currentBeat) {
                                emitNoteOff(track, *activeIt, 0);
                                eraseTrackVoice(track, activeIt->voiceId);
                                dispatched.erase(activeIt);
                            } else if (isDispatched && end < currentBeat + beatWindow && end + epsilon >= currentBeat) {
                                float secs = project->activeTempoCurve().secondsForBeats(currentBeat, end);
                                int offset = std::max(0, std::min(static_cast<int>(secs * sampleRate), bufferSize - 1));
                                emitNoteOff(track, *activeIt, offset);
                                eraseTrackVoice(track, activeIt->voiceId);
                                dispatched.erase(activeIt);
                            }
                        }
                        // Flush notes still active when the position ends.
                        if (posEndBeat < currentBeat + beatWindow) {
                            for (const auto& active : dispatched) {
                                int off = posEndBeat < currentBeat ? 0
                                    : std::min(static_cast<int>(project->activeTempoCurve().secondsForBeats(
                                        currentBeat, posEndBeat) * sampleRate), bufferSize - 1);
                                emitNoteOff(track, active, off);
                                eraseTrackVoice(track, active.voiceId);
                            }
                            dispatched.clear();
                        }
                    }
                    break;
                case ElementType::audioClip:
                    {
                        if (!project->dspIsPlaying) break;
                        if (!(*track->buffer)) break;
                        AudioClip* ac = static_cast<AudioClip*>(element);
                        if (!ac->buffer) break;

                        // Actual elapsed seconds from beat positions
                        float posStartSec = project->activeTempoCurve().secondsForBeats(0.f, posStartBeat);
                        float posEndSec = project->activeTempoCurve().secondsForBeats(0.f, posEndBeat);
                        float offBeats = Note::beatsFromVector(pos.startOffsetPairs);
                        float offSec = project->activeTempoCurve().secondsForBeats(
                            posStartBeat, posStartBeat + offBeats);
                        double latSec = static_cast<double>(
                            project->activeTempoCurve().secondsForBeats(0.f, currentBeat) - posStartSec);
                        const double fileSec = latSec + static_cast<double>(offSec);
                        const double sr = AudioManager::instance()->sampleRate;
                        int readIdx = static_cast<int>(fileSec * sr);
                        int chans = std::min({ac->numChannels, track->connection->numChannels, track->connection->allocChannels});
                        int stride = track->connection ? track->connection->bufferSize : bufferSize;
                        for (int ch = 0; ch < chans; ++ch) {
                            float* rbuf = ac->buffer + static_cast<size_t>(ch) * ac->num_samples;
                            float* wbuf = *(track->buffer) + static_cast<size_t>(ch) * static_cast<size_t>(stride);
                            int ri = readIdx;
                            for (size_t i = 0; i < bufferSize; ++i) {
                                float beat = currentBeat + (static_cast<float>(i) / bufferSize) * beatWindow;
                                if (beat >= posStartBeat && beat <= posEndBeat) {
                                    if (ri >= 0 && ri < static_cast<int>(ac->num_samples))
                                        wbuf[i] += rbuf[ri];
                                }
                                ri++;
                            }
                        }
                    }
                    break;
                case ElementType::automationCurve:
                    {
                        if (!project->dspIsPlaying) break;
                        if (!(*track->buffer)) break;
                        AutomationCurve* ac = static_cast<AutomationCurve*>(element);
                        if (ac->points.empty()) break;
                        float offBeat = Note::beatsFromVector(pos.startOffsetPairs);
                        float* wbuf = *(track->buffer);
                        for (size_t i = 0; i < bufferSize; ++i) {
                            float beat = currentBeat + (static_cast<float>(i) / bufferSize) * beatWindow;
                            if (beat < posStartBeat || beat > posEndBeat + 0.001f) continue;
                            float v = ac->evaluateAtX(beat - posStartBeat + offBeat);
                            wbuf[i] += v * 2.f - 1.f;  // 0..1 → -1..1
                        }
                    }
                    break;
                default:
                    break;
            }
        }

    // Sort events by sample offset so they're consumed in time order.
    for (auto& t : tm->tracks) {
        if (!t->events || !(*t->events)) continue;
        std::sort((*t->events)->begin(), (*t->events)->end(),
            [](const Event& a, const Event& b) {
                if (a.sampleOffset != b.sampleOffset)
                    return a.sampleOffset < b.sampleOffset;
                return a.type == noteOff && b.type == noteOn;
            });
    }
}

GridElement* ElementManager::getElement(uint16_t id) {
    auto index = getIndex(id);
    return elements[index];
}

void ElementManager::fromJSON(json j) {
    for (auto* e : elements) {
        delete e;
    }
    elements.clear();
    ids.clear();
    id_pool = idManager();
    position_id_pool = idManager();

    id_pool.fromJSON(j["id_pool"]);
    if (j.contains("position_id_pool"))
        position_id_pool.fromJSON(j["position_id_pool"]);

    for(json e : j["elements"]) {
        GridElement* ge;

        switch (e["type"].get<int>()) {
            case ElementType::region:
                ge = new Region(project, parentNode);
                break;
            case ElementType::audioClip:
                ge = new AudioClip(project, parentNode);
                break;
            case ElementType::automationCurve:
                ge = new AutomationCurve(project, parentNode);
                break;
            default:
                return; // unknown type, give up
        }

        ge->fromJSON(e);
        id_pool.reserveID(ge->id);
        elements.push_back(ge);
        ids[ge->id] = elements.size() -1;
    }
}

uint16_t ElementManager::getIndex(uint16_t id) {
    auto it = ids.find(id);
    if (it == ids.end())
        throw std::runtime_error("ElementManager::getIndex: unknown id " + std::to_string(id));
    return it->second;
}

Region* ElementManager::newRegion() {
    auto r = new Region(project, parentNode);
    r->pos_id_pool = &position_id_pool;
    r->id = id_pool.newID();
    elements.push_back(r);

    ids[r->id] = elements.size() -1;

    return r;
}

void ElementManager::removeElementById(uint16_t elementId) {
    auto it = ids.find(elementId);
    if (it == ids.end())
        throw std::runtime_error("ElementManager::removeElementById: unknown element id");
    const size_t idx = static_cast<size_t>(it->second);
    if (idx >= elements.size())
        throw std::runtime_error("ElementManager::removeElementById: index out of range");
    GridElement* ge = elements[idx];
    if (ge->id != elementId)
        throw std::runtime_error("ElementManager::removeElementById: id mismatch");
    if (currentElement == static_cast<int>(elementId))
        currentElement = -1;
    elements.erase(elements.begin() + static_cast<std::ptrdiff_t>(idx));
    id_pool.releaseID(elementId);
    delete ge;
    ids.erase(elementId);
    for (size_t i = idx; i < elements.size(); ++i)
        ids[elements[i]->id] = static_cast<uint16_t>(i);
}

void ElementManager::restoreRegionFromSnapshot(const json& regionJson) {
    restoreRegionFromSnapshotAt(elements.size(), regionJson);
}

void ElementManager::restoreRegionFromSnapshotAt(size_t insertIndex, const json& regionJson) {
    if (!regionJson.contains("type") || regionJson["type"].get<int>() != ElementType::region)
        throw std::runtime_error("ElementManager::restoreRegionFromSnapshotAt: not a region snapshot");
    const uint16_t rid = regionJson.at("id").get<uint16_t>();
    if (ids.count(rid))
        throw std::runtime_error("ElementManager::restoreRegionFromSnapshotAt: id already in use");
    if (insertIndex > elements.size())
        insertIndex = elements.size();
    auto* r = new Region(project, parentNode);
    r->pos_id_pool = &position_id_pool;
    r->fromJSON(regionJson);
    id_pool.reserveID(rid);
    elements.insert(elements.begin() + static_cast<std::ptrdiff_t>(insertIndex), r);
    ids.clear();
    for (size_t i = 0; i < elements.size(); ++i)
        ids[elements[i]->id] = static_cast<uint16_t>(i);
}

void ElementManager::restoreAutomationCurveFromSnapshot(const json& curveJson) {
    restoreAutomationCurveFromSnapshotAt(elements.size(), curveJson);
}

void ElementManager::restoreAutomationCurveFromSnapshotAt(size_t insertIndex, const json& curveJson) {
    if (!curveJson.contains("type") || curveJson["type"].get<int>() != ElementType::automationCurve)
        throw std::runtime_error("ElementManager::restoreAutomationCurveFromSnapshotAt: not an automation curve snapshot");
    const uint16_t cid = curveJson.at("id").get<uint16_t>();
    if (ids.count(cid))
        throw std::runtime_error("ElementManager::restoreAutomationCurveFromSnapshotAt: id already in use");
    if (insertIndex > elements.size())
        insertIndex = elements.size();
    auto* a = new AutomationCurve(project, parentNode);
    a->pos_id_pool = &position_id_pool;
    a->fromJSON(curveJson);
    id_pool.reserveID(cid);
    elements.insert(elements.begin() + static_cast<std::ptrdiff_t>(insertIndex), a);
    ids.clear();
    for (size_t i = 0; i < elements.size(); ++i)
        ids[elements[i]->id] = static_cast<uint16_t>(i);
}

void ElementManager::restoreAudioClipFromSnapshot(const json& clipJson) {
    restoreAudioClipFromSnapshotAt(elements.size(), clipJson);
}

void ElementManager::restoreAudioClipFromSnapshotAt(size_t insertIndex, const json& clipJson) {
    if (!clipJson.contains("type") || clipJson["type"].get<int>() != ElementType::audioClip)
        throw std::runtime_error("ElementManager::restoreAudioClipFromSnapshotAt: not an audio clip snapshot");
    const uint16_t cid = clipJson.at("id").get<uint16_t>();
    if (ids.count(cid))
        throw std::runtime_error("ElementManager::restoreAudioClipFromSnapshotAt: id already in use");
    if (insertIndex > elements.size())
        insertIndex = elements.size();
    auto* a = new AudioClip(project, parentNode);
    a->pos_id_pool = &position_id_pool;
    a->fromJSON(clipJson);
    id_pool.reserveID(cid);
    elements.insert(elements.begin() + static_cast<std::ptrdiff_t>(insertIndex), a);
    ids.clear();
    for (size_t i = 0; i < elements.size(); ++i)
        ids[elements[i]->id] = static_cast<uint16_t>(i);
}

AudioClip* ElementManager::newAudioClip(std::string filepath) {

    auto a = new AudioClip(project, parentNode);

    a->pos_id_pool = &position_id_pool;
    a->setFile(filepath);
    if (a->filepath == "") {
        delete a;
        return nullptr;
    }

    a->id = id_pool.newID();
    elements.push_back(a);

    ids[a->id] = elements.size() - 1;

    return a;
}

AutomationCurve* ElementManager::newAutomationCurve() {
    auto a = new AutomationCurve(project, parentNode);
    a->pos_id_pool = &position_id_pool;
    a->id = id_pool.newID();
    elements.push_back(a);
    ids[a->id] = elements.size() - 1;
    return a;
}

ElementManager::ElementManager(Project* p, TrackManager* tm, ArrangerNode* n) : project(p), tm(tm), parentNode(n) {
}

void ElementManager::render(SDL_Renderer* renderer) {

    float height = 50;
    float topMargin = 5;
    float sideMargin = 5;
    float bottomMargin = 25;
    float allElementsHeight = elements.size() * (height + topMargin) + topMargin + bottomMargin;



    if(allElementsHeight< dstRect->h) {
        scrollY = 0;
    } else if (scrollY > allElementsHeight - dstRect->h) {
        scrollY = allElementsHeight - dstRect->h;
    }


    //background
    SDL_Rect clipRect = {
        static_cast<int>(dstRect->x),
        static_cast<int>(dstRect->y),
        static_cast<int>(dstRect->w),
        static_cast<int>(dstRect->h)
    };
    SDL_SetRenderClipRect(renderer, &clipRect);
    SDL_SetRenderDrawColor(renderer, colors.elementListBg[0], colors.elementListBg[1], colors.elementListBg[2], colors.elementListBg[3]);
    SDL_RenderFillRect(renderer, dstRect);



    float i = topMargin + dstRect->y - scrollY;


    hoveredElement = -1;

    //regions
    for(auto e: elements) {
        SDL_FRect rect{dstRect->x + sideMargin, i, dstRect->w - 2*sideMargin, height};

        if (mouseX >= rect.x && mouseX < (rect.x + rect.w) &&
            mouseY >= rect.y && mouseY < (rect.y + rect.h)) {
            hoveredElement = e->id;
            if(e->id == currentElement) {
                SDL_SetRenderDrawColor(renderer, 120, 40, 40, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 40, 40, 120, 255);
            }
        } else {
            if(e->id == currentElement) {
                SDL_SetRenderDrawColor(renderer, 60, 20, 20, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 20, 20, 60, 255);
            }
        }

        SDL_RenderFillRect(renderer, &rect);

        SDL_SetRenderDrawColor(renderer, 120, 120, 120, 255);
        SDL_RenderRect(renderer, &rect);



        std::string regionIdText = std::to_string(e->id);
        SDL_Color textColor = {255, 255, 255, 255};

        SDL_Surface* textSurface = TTF_RenderText_Blended(fonts.mainFont, regionIdText.c_str(), regionIdText.size(), textColor);
        if (textSurface) {
            SDL_Texture* textTexture = SDL_CreateTextureFromSurface(renderer, textSurface);
            if (textTexture) {
                SDL_FRect textRect;
                textRect.w = static_cast<float>(textSurface->w);
                textRect.h = static_cast<float>(textSurface->h);
                textRect.x = rect.x + (rect.w - textRect.w) / 2.0f;
                textRect.y = rect.y + (rect.h - textRect.h) / 2.0f;

                SDL_RenderTexture(renderer, textTexture, nullptr, &textRect);
                SDL_DestroyTexture(textTexture);
            }
            SDL_DestroySurface(textSurface);
        }


        i += height + topMargin;
    }

    //outline
    SDL_SetRenderDrawColor(renderer, 150, 150, 150, 255);
    SDL_RenderRect(renderer, dstRect);

    // New Region button
    float btnY = dstRect->y + dstRect->h - bottomMargin;
    float btnHalfW = dstRect->w / 2.f;
    SDL_FRect regionBtn{ dstRect->x, btnY, btnHalfW, bottomMargin };
    hoverNewRegion = false;
    if (mouseX >= regionBtn.x && mouseX < (regionBtn.x + regionBtn.w) &&
        mouseY >= regionBtn.y && mouseY < (regionBtn.y + regionBtn.h)) {
        SDL_SetRenderDrawColor(renderer, colors.trackNotes[0], colors.trackNotes[1], colors.trackNotes[2], 220);
        hoverNewRegion = true;
    } else {
        SDL_SetRenderDrawColor(renderer, colors.trackNotes[0], colors.trackNotes[1], colors.trackNotes[2], 180);
    }
    SDL_RenderFillRect(renderer, &regionBtn);
    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
    SDL_RenderRect(renderer, &regionBtn);
    if (fonts.mainFont) {
        SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, "+Region", 0, {255, 255, 255, 255});
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            if (tex) {
                SDL_FRect tr{ regionBtn.x + (regionBtn.w - surf->w) * 0.5f,
                              regionBtn.y + (regionBtn.h - surf->h) * 0.5f,
                              (float)surf->w, (float)surf->h };
                SDL_RenderTexture(renderer, tex, nullptr, &tr);
                SDL_DestroyTexture(tex);
            }
            SDL_DestroySurface(surf);
        }
    }

    // New Waveform button
    SDL_FRect waveformBtn{ dstRect->x + btnHalfW, btnY, btnHalfW, bottomMargin };
    hoverNewWaveform = false;
    if (mouseX >= waveformBtn.x && mouseX < (waveformBtn.x + waveformBtn.w) &&
        mouseY >= waveformBtn.y && mouseY < (waveformBtn.y + waveformBtn.h)) {
        SDL_SetRenderDrawColor(renderer, colors.trackAudio[0], colors.trackAudio[1], colors.trackAudio[2], 220);
        hoverNewWaveform = true;
    } else {
        SDL_SetRenderDrawColor(renderer, colors.trackAudio[0], colors.trackAudio[1], colors.trackAudio[2], 180);
    }
    SDL_RenderFillRect(renderer, &waveformBtn);
    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
    SDL_RenderRect(renderer, &waveformBtn);
    if (fonts.mainFont) {
        SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, "+Wave", 0, {255, 255, 255, 255});
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            if (tex) {
                SDL_FRect tr{ waveformBtn.x + (waveformBtn.w - surf->w) * 0.5f,
                              waveformBtn.y + (waveformBtn.h - surf->h) * 0.5f,
                              (float)surf->w, (float)surf->h };
                SDL_RenderTexture(renderer, tex, nullptr, &tr);
                SDL_DestroyTexture(tex);
            }
            SDL_DestroySurface(surf);
        }
    }

    SDL_SetRenderClipRect(renderer, NULL);
}

bool ElementManager::handleInput(SDL_Event& e) {
    bool running = true;
    switch (e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                if(hoverNewRegion) {
                    auto* cra = new CreateRegionAction(project, parentNode->nm->managerPath, static_cast<int>(parentNode->id));
                    project->um->newAction(cra);
                    currentElement = cra->regionID;
                    break;
                }
                if(hoverNewWaveform) {
                    auto* cca = new CreateAutomationCurveAction(project, parentNode->nm->managerPath, static_cast<int>(parentNode->id));
                    project->um->newAction(cca);
                    currentElement = cca->curveID;
                    break;
                }
                if(hoveredElement != -1) {
                    currentElement = hoveredElement;
                    break;
                }
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            scrollY -= e.wheel.y * 10;
            if(scrollY < 0) {
                scrollY = 0;
            }
            break;
    }
    return running;
}
