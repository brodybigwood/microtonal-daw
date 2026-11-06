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
#include "ContextMenu.h"
#include "TreeEntry.h"
#include "SongRoll.h"

static const char* elementTypeName(ElementType t) {
    switch (t) {
        case ElementType::region: return "MIDI";
        case ElementType::audioClip: return "Audio";
        case ElementType::automationCurve: return "Automation";
    }
    return "?";
}

// (Re)build an element's 64x36 list preview only when its content signature,
// size, or renderer changed. The preview is drawn by the element itself, so the
// list and the timeline share one rendering implementation.
static void ensureThumbnail(SDL_Renderer* renderer, GridElement* e) {
    if (!renderer || !e) return;
    constexpr int kW = 64, kH = 36;
    const uint64_t sig = e->thumbnailSignature();
    if (e->thumbTexture && e->thumbW == kW && e->thumbH == kH &&
        e->thumbRenderer == renderer && e->thumbSignature == sig)
        return;

    if (!e->thumbTexture || e->thumbW != kW || e->thumbH != kH || e->thumbRenderer != renderer) {
        if (e->thumbTexture) SDL_DestroyTexture(e->thumbTexture);
        e->thumbTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                            SDL_TEXTUREACCESS_TARGET, kW, kH);
        if (e->thumbTexture) SDL_SetTextureBlendMode(e->thumbTexture, SDL_BLENDMODE_BLEND);
        e->thumbW = kW;
        e->thumbH = kH;
        e->thumbRenderer = renderer;
    }
    if (!e->thumbTexture) return;

    SDL_Texture* prevTarget = SDL_GetRenderTarget(renderer);
    if (!SDL_SetRenderTarget(renderer, e->thumbTexture)) {
        SDL_SetRenderTarget(renderer, prevTarget);
        return;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 12, 12, 24, 255);
    SDL_RenderClear(renderer);
    e->drawThumbnail(renderer, kW, kH);
    SDL_SetRenderTarget(renderer, prevTarget);
    e->thumbSignature = sig;
}

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
        element->clearCachedTextures();
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

    // A seek is a discontinuity: release old voices. Notes already sounding at
    // the new position are chased below (a playhead that lands mid-note plays
    // that note from its current point).
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
                            // Notes at or past the position's end belong to nothing.
                            if (start >= posEndBeat) continue;
                            float end = note->endBeats() + posStartBeat - trim;

                            auto activeIt = std::find_if(dispatched.begin(), dispatched.end(),
                                [&note](const ActiveNote& active) { return active.note == note; });
                            const bool wasDispatched = activeIt != dispatched.end();

                            // Start a note that begins in this block, or one that is
                            // already sounding because the playhead landed inside it
                            // (transport start or seek). Chase the latter at offset 0,
                            // but not inside a position that has already ended.
                            const bool startsNow = start + epsilon >= currentBeat;
                            const bool soundingNow = start < currentBeat &&
                                                     end > currentBeat && posEndBeat > currentBeat;
                            if (!wasDispatched && start < currentBeat + beatWindow &&
                                (startsNow || soundingNow)) {
                                float secs = start > currentBeat
                                    ? project->activeTempoCurve().secondsForBeats(currentBeat, start)
                                    : 0.0f;
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

        // Positions reserve ids while deserializing, so the pool must be wired
        // before fromJSON runs (newRegion/newClip do the same for live creation).
        ge->pos_id_pool = &position_id_pool;
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

void ElementManager::applyElementOrder(const std::vector<uint16_t>& order) {
    if (order.size() != elements.size())
        throw std::runtime_error("ElementManager::applyElementOrder: wrong element count");
    std::vector<GridElement*> reordered;
    reordered.reserve(order.size());
    for (uint16_t id : order) {
        auto it = ids.find(id);
        if (it == ids.end())
            throw std::runtime_error("ElementManager::applyElementOrder: unknown element id");
        reordered.push_back(elements[static_cast<size_t>(it->second)]);
    }
    elements = std::move(reordered);
    ids.clear();
    for (size_t i = 0; i < elements.size(); ++i)
        ids[elements[i]->id] = static_cast<uint16_t>(i);
}

void ElementManager::restoreElementFromSnapshotAt(size_t insertIndex, const json& snapshot) {
    const int t = snapshot.at("type").get<int>();
    switch (t) {
        case ElementType::region:
            restoreRegionFromSnapshotAt(insertIndex, snapshot);
            break;
        case ElementType::audioClip:
            restoreAudioClipFromSnapshotAt(insertIndex, snapshot);
            break;
        case ElementType::automationCurve:
            restoreAutomationCurveFromSnapshotAt(insertIndex, snapshot);
            break;
        default:
            throw std::runtime_error("ElementManager::restoreElementFromSnapshotAt: bad element type");
    }
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

    //elements
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

        // Rendered preview of the element's content.
        ensureThumbnail(renderer, e);
        SDL_FRect thumb{rect.x + 4, rect.y + 7, 64, 36};
        SDL_SetRenderDrawColor(renderer, 8, 8, 16, 255);
        SDL_RenderFillRect(renderer, &thumb);
        if (e->thumbTexture) SDL_RenderTexture(renderer, e->thumbTexture, nullptr, &thumb);
        SDL_SetRenderDrawColor(renderer, 90, 90, 90, 255);
        SDL_RenderRect(renderer, &thumb);

        const float textX = rect.x + 74;
        const float avail = std::max(10.0f, rect.w - 74.0f - 22.0f);
        const auto drawLabel = [&](CachedText& cache, const std::string& str, SDL_Color color,
                                   float y, float targetH) {
            SDL_Texture* tex = cache.get(renderer, str, color);
            if (!tex) return;
            float tw = 0, th = 0;
            SDL_GetTextureSize(tex, &tw, &th);
            if (tw <= 0 || th <= 0) return;
            const float ww = std::min(avail, tw * (targetH / th));
            SDL_FRect dst{textX, y, ww, targetH};
            SDL_RenderTexture(renderer, tex, nullptr, &dst);
        };
        drawLabel(e->nameLabel, e->name, SDL_Color{245, 245, 250, 255}, rect.y + 5, 18.0f);
        drawLabel(e->metaLabel,
                  std::string(elementTypeName(e->type)) + " \xC2\xB7 " +
                      std::to_string(e->positions.size()) + " pos",
                  SDL_Color{165, 170, 190, 255}, rect.y + 28, 14.0f);

        // Dim id at the right edge (menus/undo names refer to it).
        if (SDL_Texture* idTex = e->idLabel.get(renderer, std::to_string(e->id), SDL_Color{140, 140, 165, 255})) {
            float tw = 0, th = 0;
            SDL_GetTextureSize(idTex, &tw, &th);
            if (tw > 0 && th > 0) {
                const float hh = 14.0f;
                const float ww = tw * (hh / th);
                SDL_FRect dst{rect.x + rect.w - 5 - ww, rect.y + 5, ww, hh};
                SDL_RenderTexture(renderer, idTex, nullptr, &dst);
            }
        }

        i += height + topMargin;
    }

    if (dragActive && dropIndex >= 0) {
        const float lineY = dstRect->y + topMargin - scrollY + dropIndex * (height + topMargin) - topMargin * 0.5f;
        SDL_FRect line{dstRect->x + sideMargin, lineY, dstRect->w - 2 * sideMargin, 2.0f};
        SDL_SetRenderDrawColor(renderer, 120, 200, 255, 255);
        SDL_RenderFillRect(renderer, &line);
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

    // New Automation button (creates an AutomationCurve element, not an audio clip)
    SDL_FRect automationBtn{ dstRect->x + btnHalfW, btnY, btnHalfW, bottomMargin };
    hoverNewAutomation = false;
    if (mouseX >= automationBtn.x && mouseX < (automationBtn.x + automationBtn.w) &&
        mouseY >= automationBtn.y && mouseY < (automationBtn.y + automationBtn.h)) {
        SDL_SetRenderDrawColor(renderer, colors.trackAudio[0], colors.trackAudio[1], colors.trackAudio[2], 220);
        hoverNewAutomation = true;
    } else {
        SDL_SetRenderDrawColor(renderer, colors.trackAudio[0], colors.trackAudio[1], colors.trackAudio[2], 180);
    }
    SDL_RenderFillRect(renderer, &automationBtn);
    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
    SDL_RenderRect(renderer, &automationBtn);
    if (fonts.mainFont) {
        SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, "+Automation", 0, {255, 255, 255, 255});
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            if (tex) {
                SDL_FRect tr{ automationBtn.x + (automationBtn.w - surf->w) * 0.5f,
                              automationBtn.y + (automationBtn.h - surf->h) * 0.5f,
                              (float)surf->w, (float)surf->h };
                SDL_RenderTexture(renderer, tex, nullptr, &tr);
                SDL_DestroyTexture(tex);
            }
            SDL_DestroySurface(surf);
        }
    }

    SDL_SetRenderClipRect(renderer, NULL);
}

int ElementManager::rowIndexAt(float x, float y) {
    if (!dstRect) return -1;
    if (x < dstRect->x || x >= dstRect->x + dstRect->w) return -1;
    constexpr float rowH = 50.0f, gap = 5.0f;
    const float ry = y - (dstRect->y + gap - static_cast<float>(scrollY));
    if (ry < 0) return -1;
    const int idx = static_cast<int>(ry / (rowH + gap));
    if (idx < 0 || idx >= static_cast<int>(elements.size())) return -1;
    const float inRow = ry - idx * (rowH + gap);
    return inRow <= rowH ? idx : -1;
}

static void openElementContextMenu(ElementManager* em) {
    const int idx = em->rowIndexAt(em->mouseX, em->mouseY);
    if (idx < 0 || idx >= static_cast<int>(em->elements.size())) return;
    GridElement* el = em->elements[static_cast<size_t>(idx)];
    em->currentElement = el->id;

    auto ctxMenu = ContextMenu::get();
    auto root = uTreeEntry();
    root->label = el->name;

    auto rename = uTreeEntry();
    rename->label = "Rename Element...";
    rename->click = [em, id = static_cast<int>(el->id)]() {
        auto* ctx = ContextMenu::get();
        ctx->skipNextEvent = true;
        if (em->project && em->project->window) SDL_StartTextInput(em->project->window);
        ctx->activate();
        ctx->dynamicTick = getTextInputTicker(
            [em, id](std::string text) {
                if (text.empty()) return;
                auto* element = em->getElement(static_cast<uint16_t>(id));
                if (!element || text == element->name) return;
                em->project->um->newAction(new RenameElementAction(em->project,
                    em->parentNode->nm->managerPath, static_cast<int>(em->parentNode->id), id, text));
            },
            []() {});
    };
    root->addChild(rename);

    if (el->type == ElementType::region) {
        auto open = uTreeEntry();
        open->label = "Open Piano Roll";
        open->click = [em, id = static_cast<int>(el->id)]() {
            if (!em->parentNode || !em->parentNode->sl) return;
            em->parentNode->sl->createPianoRoll(static_cast<Region*>(em->getElement(static_cast<uint16_t>(id))), true);
        };
        root->addChild(open);
    }

    auto del = uTreeEntry();
    del->label = "Delete Element";
    del->click = [em, id = static_cast<int>(el->id)]() {
        em->project->um->newAction(new DeleteElementAction(em->project,
            em->parentNode->nm->managerPath, static_cast<int>(em->parentNode->id), id));
    };
    root->addChild(del);

    ctxMenu->skipNextEvent = true;
    ctxMenu->activate();
    ctxMenu->dynamicTick = getTreeMenuTicker(root);
}

bool ElementManager::handleInput(SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                if(hoverNewRegion) {
                    auto* cra = new CreateRegionAction(project, parentNode->nm->managerPath, static_cast<int>(parentNode->id));
                    project->um->newAction(cra);
                    currentElement = cra->regionID;
                    break;
                }
                if(hoverNewAutomation) {
                    auto* cca = new CreateAutomationCurveAction(project, parentNode->nm->managerPath, static_cast<int>(parentNode->id));
                    project->um->newAction(cca);
                    currentElement = cca->curveID;
                    break;
                }
                const int idx = rowIndexAt(mouseX, mouseY);
                if (idx >= 0 && idx < static_cast<int>(elements.size())) {
                    currentElement = elements[static_cast<size_t>(idx)]->id;
                    dragPending = true;
                    dragActive = false;
                    draggedElementID = elements[static_cast<size_t>(idx)]->id;
                    dragStartY = mouseY;
                    dropIndex = -1;
                }
            } else if (e.button.button == SDL_BUTTON_RIGHT) {
                openElementContextMenu(this);
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (dragPending) {
                if (!dragActive && std::fabs(mouseY - dragStartY) > 4.0f) dragActive = true;
                if (dragActive) {
                    constexpr float rowH = 50.0f, gap = 5.0f;
                    const float listTop = dstRect->y + gap - static_cast<float>(scrollY);
                    const int idx = rowIndexAt(mouseX, mouseY);
                    if (idx < 0)
                        dropIndex = (mouseY < listTop) ? 0 : static_cast<int>(elements.size());
                    else {
                        const float rowTop = listTop + idx * (rowH + gap);
                        dropIndex = idx + (mouseY > rowTop + rowH * 0.5f ? 1 : 0);
                    }
                }
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                if (dragActive && dropIndex >= 0 && project && project->um) {
                    std::vector<uint16_t> before, after;
                    for (auto* el : elements) before.push_back(el->id);
                    int src = -1;
                    for (size_t i = 0; i < before.size(); ++i)
                        if (before[i] == draggedElementID) { src = static_cast<int>(i); break; }
                    if (src >= 0) {
                        int slot = dropIndex;
                        if (slot > src) --slot;
                        after = before;
                        after.erase(after.begin() + src);
                        slot = std::max(0, std::min(static_cast<int>(after.size()), slot));
                        after.insert(after.begin() + slot, draggedElementID);
                        if (after != before)
                            project->um->newAction(new ElementReorderAction(project, parentNode->nm->managerPath,
                                static_cast<int>(parentNode->id), std::move(before), std::move(after)));
                    }
                }
                dragPending = false;
                dragActive = false;
                dropIndex = -1;
            }
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            scrollY -= e.wheel.y * 10;
            if(scrollY < 0) {
                scrollY = 0;
            }
            break;
    }
    return true;
}
