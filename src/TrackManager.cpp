#include "TrackManager.h"
#include <algorithm>
#include "WindowHandler.h"
#include <ranges>
#include "styles.h"
#include <SDL3_gfx/SDL3_gfxPrimitives.h>
#include "SongRoll.h"
#include "nodes/nodetypes.h"
#include "UndoManager.h"
#include "NodeManager.h"
#include "AudioManager.h"
#include <cstring>

namespace {
struct TrackButtons {
    SDL_FRect solo, mute, arm, monitor;
};

TrackButtons trackButtonsFor(const SDL_FRect& row) {
    const float bh = 18.f;
    const float bw = 26.f;
    const float gap = 4.f;
    const float x = row.x + row.w / 10.f + 6.f;
    const float y = row.y + (row.h - bh) * 0.5f;
    return {
        SDL_FRect{x, y, bw, bh},
        SDL_FRect{x + bw + gap, y, bw, bh},
        SDL_FRect{x + 2.f * (bw + gap), y, bw, bh},
        SDL_FRect{x + 3.f * (bw + gap), y, bw, bh}
    };
}

void renderSmallButton(SDL_Renderer* renderer, const SDL_FRect& rect, const char* label, SDL_Color fill) {
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
    SDL_RenderRect(renderer, &rect);
    if (!fonts.mainFont) return;
    SDL_Color textColor{235, 235, 235, 255};
    SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, label, 0, textColor);
    if (!surf) return;
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    if (tex) {
        float tw = static_cast<float>(surf->w), th = static_cast<float>(surf->h);
        SDL_FRect dst{rect.x + rect.w * 0.5f - tw * 0.5f, rect.y + rect.h * 0.5f - th * 0.5f, tw, th};
        SDL_RenderTexture(renderer, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
    }
    SDL_DestroySurface(surf);
}
} // namespace

TrackManager::TrackManager(ArrangerNode* n) : parentNode(n), newTrackE(nullptr), newTrackW(nullptr) {}


void TrackManager::setGeometry(SDL_FRect* dstRect, SDL_Renderer*& r) {

    this->dstRect = dstRect;

    newTrackRectE = SDL_FRect{
        dstRect->x, dstRect->y + dstRect->h - 20.0f,
        dstRect->w / 2.0f, 20.0f
    };

    newTrackRectW = SDL_FRect{
        dstRect->x + dstRect->w / 2.0f, dstRect->y + dstRect->h - 20.0f,
        dstRect->w / 2.0f, 20.0f
    };

    newTrackE = new Button();
    newTrackE->dstRect = &newTrackRectE;
    newTrackE->title = "New Track (notes)";
    newTrackE->inactive = {colors.trackNotes[0], colors.trackNotes[1], colors.trackNotes[2], 180};
    newTrackE->inactive_hover = {colors.trackNotes[0], colors.trackNotes[1], colors.trackNotes[2], 220};
    newTrackE->activated = [] { return false; };
    newTrackE->hover = [this] { return this->mouseOn(&(this->newTrackRectE)); };
    newTrackE->onClick = [this] { this->addTrack(TrackType::Notes); };

    newTrackW = new Button();
    newTrackW->dstRect = &newTrackRectW;
    newTrackW->title = "New Track (audio)";
    newTrackW->inactive = {colors.trackAudio[0], colors.trackAudio[1], colors.trackAudio[2], 180};
    newTrackW->inactive_hover = {colors.trackAudio[0], colors.trackAudio[1], colors.trackAudio[2], 220};
    newTrackW->activated = [] { return false; };
    newTrackW->hover = [this] { return this->mouseOn(&(this->newTrackRectW)); };
    newTrackW->onClick = [this] { this->addTrack(TrackType::Audio); };
}

TrackManager::~TrackManager() {

    if (newTrackE) {
        delete newTrackE;
        newTrackE = nullptr;
    }
    if (newTrackW) {
        delete newTrackW;
        newTrackW = nullptr;
    }

    for (auto t : tracks) {
        delete t;
    }
    tracks.clear();
}

void TrackManager::addTrack(TrackType tp) {
    auto pa = new AddArrangerTrackAction(parentNode->project, parentNode->nm->managerPath, parentNode->id, static_cast<int>(tp));
    parentNode->project->um->newAction(pa);
}

Track* TrackManager::addTrackNow(TrackType tp, int forcedTrackID, int forcedConnectionID, int forcedIndex,
                                 int forcedInputConnectionID) {
    uint16_t id;
    if (forcedTrackID >= 0) {
        id = static_cast<uint16_t>(forcedTrackID);
        id_pool.reserveID(id);
    } else {
        id = id_pool.newID();
    }

    Track* t = new Track;
    if (forcedIndex >= 0 && static_cast<size_t>(forcedIndex) <= tracks.size()) {
        tracks.insert(tracks.begin() + static_cast<ptrdiff_t>(forcedIndex), t);
    } else {
        tracks.push_back(t);
    }
    t->id = id;
    // Rebuild id map.
    for (size_t i = 0; i < tracks.size(); ++i) ids[tracks[i]->id] = static_cast<uint16_t>(i);
    t->type = tp;

    auto c = new Connection;
    c->nm = parentNode->nm;
    c->dir = Direction::output;
    c->is_connected = false;
    c->output_node = -1;
    c->output_connection = -1;
    c->input_node = parentNode->id;
    c->input_connection = -1;
    switch (tp) {
        case TrackType::Notes:
            c->type = DataType::Events;
            c->events = new std::vector<Event>;
            c->buffer = nullptr;
            t->events = &(c->events);
            break;
        default:
            c->type = DataType::Waveform;
            c->allocateBuffer(parentNode->outputs.bufferSize);
            c->events = nullptr;
            t->buffer = &(c->buffer);
            break;
    }

    t->connection = c;
    if (forcedConnectionID >= 0) {
        c->id = static_cast<uint16_t>(forcedConnectionID);
        c->nm = parentNode->nm;
        parentNode->outputs.connections.push_back(c);
        parentNode->outputs.id_pool.reserveID(c->id);
        parentNode->outputs.ids[c->id] = parentNode->outputs.connections.size() - 1;
    } else {
        parentNode->outputs.addConnection(c);
    }

    // One input port per track, in track order.
    int trackIndex = -1;
    for (size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i] == t) { trackIndex = static_cast<int>(i); break; }
    }
    t->inputConnection = addInputConnection(tp, forcedInputConnectionID, trackIndex);

    parentNode->makeConnectionRects();
    parentNode->nm->markTopologyDirty();
    return t;
}

Connection* TrackManager::addInputConnection(TrackType tp, int forcedID, int insertIndex) {
    auto* c = new Connection;
    c->nm = parentNode->inputs.nm;
    c->dir = Direction::input;
    c->type = (tp == TrackType::Notes) ? DataType::Events : DataType::Waveform;
    c->is_connected = false;
    c->input_node = -1;
    c->input_connection = -1;
    c->output_node = parentNode->id;
    c->events = nullptr;
    c->buffer = nullptr;
    c->bufferSize = 0;
    c->allocChannels = c->numChannels;

    if (forcedID >= 0) {
        c->id = static_cast<uint16_t>(forcedID);
        parentNode->inputs.id_pool.reserveID(c->id);
    } else {
        c->id = parentNode->inputs.id_pool.newID();
    }
    c->output_connection = c->id;

    auto& in = parentNode->inputs;
    if (insertIndex >= 0 && static_cast<size_t>(insertIndex) <= in.connections.size())
        in.connections.insert(in.connections.begin() + static_cast<ptrdiff_t>(insertIndex), c);
    else
        in.connections.push_back(c);

    in.ids.clear();
    for (size_t i = 0; i < in.connections.size(); ++i)
        in.ids[in.connections[i]->id] = static_cast<uint16_t>(i);
    parentNode->makeConnectionRects();
    parentNode->nm->markTopologyDirty();
    return c;
}

void TrackManager::removeInputConnectionForTrack(Track* track) {
    if (!track || !track->inputConnection) return;
    Connection* c = track->inputConnection;
    if (c->is_connected && parentNode->nm)
        parentNode->nm->severConnectionNow(static_cast<uint16_t>(c->input_node),
                                           static_cast<uint16_t>(c->input_connection),
                                           static_cast<uint16_t>(c->output_node),
                                           static_cast<uint16_t>(c->output_connection));
    auto& in = parentNode->inputs;
    auto it = std::find(in.connections.begin(), in.connections.end(), c);
    if (it != in.connections.end()) in.connections.erase(it);
    in.ids.erase(c->id);
    in.id_pool.releaseID(c->id);
    track->inputConnection = nullptr;
    delete c;
    syncInputPortOrder();
}

void TrackManager::syncInputPortOrder() {
    auto& in = parentNode->inputs;
    std::vector<Connection*> ordered;
    ordered.reserve(in.connections.size());
    for (auto* t : tracks) {
        if (t && t->inputConnection) ordered.push_back(t->inputConnection);
    }
    // Keep any non-track input port (e.g. a future mapped parameter) after the track ports.
    for (auto* c : in.connections) {
        if (std::find(ordered.begin(), ordered.end(), c) == ordered.end())
            ordered.push_back(c);
    }
    if (ordered.size() != in.connections.size())
        return; // a track is missing its port; leave the set untouched
    in.connections = std::move(ordered);
    in.ids.clear();
    for (size_t i = 0; i < in.connections.size(); ++i)
        in.ids[in.connections[i]->id] = static_cast<uint16_t>(i);
    parentNode->makeConnectionRects();
    if (parentNode->nm) parentNode->nm->markTopologyDirty();
}

bool TrackManager::isSoloed(uint16_t id) const {
    return std::find(soloTracks.begin(), soloTracks.end(), id) != soloTracks.end();
}

bool TrackManager::isMuted(uint16_t id) const {
    return std::find(muteTracks.begin(), muteTracks.end(), id) != muteTracks.end();
}

bool TrackManager::isAudible(uint16_t id) const {
    const bool anySolo = !soloTracks.empty();
    const bool solo = isSoloed(id);
    const bool mute = isMuted(id);
    if (anySolo && !solo) return false;
    return !mute;
}

void TrackManager::setSoloState(uint16_t id, bool on) {
    auto it = std::find(soloTracks.begin(), soloTracks.end(), id);
    if (on && it == soloTracks.end()) soloTracks.push_back(id);
    else if (!on && it != soloTracks.end()) soloTracks.erase(it);
}

void TrackManager::setMuteState(uint16_t id, bool on) {
    auto it = std::find(muteTracks.begin(), muteTracks.end(), id);
    if (on && it == muteTracks.end()) muteTracks.push_back(id);
    else if (!on && it != muteTracks.end()) muteTracks.erase(it);
}

void TrackManager::setRecordArmedState(uint16_t id, bool on) {
    Track* t = getTrack(id);
    if (t) t->recordArmed = on;
}

void TrackManager::setMonitorState(uint16_t id, bool on) {
    Track* t = getTrack(id);
    if (t) t->monitor = on;
}

void TrackManager::processAfterElements(int bufferSize) {
    for (auto* t : tracks) {
        if (!t || !t->connection) continue;

        if (t->type == TrackType::Audio && t->monitor && t->inputConnection &&
            t->inputConnection->is_connected && t->inputConnection->buffer &&
            t->buffer && *t->buffer) {
            const int outCh = t->connection->allocChannels;
            const int inCh = std::max(1, t->inputConnection->numChannels);
            const int n = std::min(inCh, outCh);
            const int inStride = t->inputConnection->bufferSize > 0 ? t->inputConnection->bufferSize : bufferSize;
            const int outStride = t->connection->bufferSize > 0 ? t->connection->bufferSize : bufferSize;
            for (int ch = 0; ch < n; ++ch) {
                const float* src = t->inputConnection->buffer + static_cast<size_t>(ch) * static_cast<size_t>(inStride);
                float* dst = *t->buffer + static_cast<size_t>(ch) * static_cast<size_t>(outStride);
                for (int i = 0; i < bufferSize; ++i) dst[i] += src[i];
            }
        } else if (t->type == TrackType::Notes && t->monitor && t->inputConnection &&
                   t->inputConnection->is_connected && t->inputConnection->events &&
                   t->events && *t->events &&
                   t->inputConnection->events != *t->events) {
            auto& out = **t->events;
            out.insert(out.end(), t->inputConnection->events->begin(), t->inputConnection->events->end());
        }

        if (!isAudible(t->id)) {
            if (t->type == TrackType::Audio && t->buffer && *t->buffer) {
                std::memset(*t->buffer, 0, sizeof(float) * static_cast<size_t>(bufferSize) *
                    static_cast<size_t>(t->connection->allocChannels));
            } else if (t->type == TrackType::Notes && t->events && *t->events) {
                (*t->events)->clear();
            }
        }
    }
}

void TrackManager::captureTakes(Project* project, int bufferSize) {
    if (!project || bufferSize <= 0) return;

    const double blockStart = project->activeBeatPosition();
    const double blockEnd = blockStart + project->deltaBeats;
    const bool playing = project->dspIsPlaying;
    const bool recording = project->dspRecording;
    const bool genChanged = (project->dspTransportGeneration != lastSeenTransportGeneration);
    lastSeenTransportGeneration = project->dspTransportGeneration;

    for (Track* t : tracks) {
        if (!t || !t->inputConnection) continue;

        const bool wanted = playing && recording && t->recordArmed;

        // A discontinuity (seek) ends the take where it had reached; a new take starts at the
        // new position in this block.
        if (t->capture.active && (!wanted || genChanged))
            finalizeTake(project, t);

        if (!t->capture.active && wanted) {
            const bool ready = (t->type == TrackType::Audio)
                ? (t->inputConnection->is_connected && t->inputConnection->buffer != nullptr)
                : (t->inputConnection->is_connected && t->inputConnection->events != nullptr);
            if (!ready) continue;
            auto& cap = t->capture;
            cap.active = true;
            cap.startBeat = blockStart;
            cap.endBeat = blockEnd;
            cap.channels = (t->type == TrackType::Audio) ? std::max(1, t->inputConnection->numChannels) : 0;
            cap.audio.clear();
            cap.pendingNotes.clear();
        }

        if (!t->capture.active) continue;
        auto& cap = t->capture;
        cap.endBeat = blockEnd;

        if (t->type == TrackType::Audio) {
            if (cap.channels <= 0) continue;
            const size_t oldFrames = cap.audio.size() / static_cast<size_t>(cap.channels);
            const size_t newFrames = oldFrames + static_cast<size_t>(bufferSize);
            cap.audio.resize(newFrames * static_cast<size_t>(cap.channels), 0.0f);
            if (t->inputConnection->is_connected && t->inputConnection->buffer) {
                const int inCh = std::max(1, t->inputConnection->numChannels);
                const int n = std::min(cap.channels, inCh);
                const int stride = t->inputConnection->bufferSize > 0 ? t->inputConnection->bufferSize : bufferSize;
                for (int ch = 0; ch < n; ++ch) {
                    const float* src = t->inputConnection->buffer + static_cast<size_t>(ch) * static_cast<size_t>(stride);
                    float* dst = cap.audio.data() + static_cast<size_t>(ch) * newFrames + oldFrames;
                    std::memcpy(dst, src, sizeof(float) * static_cast<size_t>(bufferSize));
                }
            }
        } else if (t->type == TrackType::Notes) {
            if (!t->inputConnection->is_connected || !t->inputConnection->events) continue;
            for (const Event& ev : *t->inputConnection->events) {
                const double beat = blockStart + project->deltaBeats *
                    (static_cast<double>(ev.sampleOffset) / static_cast<double>(bufferSize));
                if (ev.type == noteEventType::noteOn) {
                    RecordedTakeNote note;
                    note.pitch = ev.num;
                    note.channel = ev.channel;
                    note.startBeat = beat;
                    note.endBeat = beat;
                    t->capture.pendingNotes[ev.id] = note;
                } else if (ev.type == noteEventType::noteOff) {
                    auto it = t->capture.pendingNotes.find(ev.id);
                    if (it != t->capture.pendingNotes.end()) {
                        it->second.endBeat = std::max(it->second.startBeat, beat);
                        t->capture.pendingNotes.erase(it);
                    }
                }
            }
        }
    }
}

void TrackManager::finalizeTake(Project* project, Track* track) {
    if (!track || !track->capture.active) return;
    auto& cap = track->capture;

    RecordedTake take;
    take.managerPath = parentNode && parentNode->nm ? parentNode->nm->managerPath : std::vector<int>{};
    take.nodeID = parentNode ? static_cast<int>(parentNode->id) : 0;
    take.trackID = track->id;
    take.startBeat = cap.startBeat;
    take.endBeat = cap.endBeat;

    if (track->type == TrackType::Audio) {
        take.isAudio = true;
        take.channels = cap.channels;
        take.frames = cap.channels > 0 ? cap.audio.size() / static_cast<size_t>(cap.channels) : 0;
        AudioManager* am = AudioManager::instance();
        take.sampleRate = am ? static_cast<int>(am->sampleRate) : 48000;
        take.audio = std::move(cap.audio);
    } else {
        take.isAudio = false;
        take.notes.reserve(cap.pendingNotes.size());
        for (auto& [voiceId, note] : cap.pendingNotes) {
            RecordedTakeNote n = note;
            n.endBeat = std::max(n.startBeat, take.endBeat);
            take.notes.push_back(n);
        }
        std::sort(take.notes.begin(), take.notes.end(),
            [](const RecordedTakeNote& a, const RecordedTakeNote& b) { return a.startBeat < b.startBeat; });
    }

    cap = Track::TakeCapture{};

    const bool empty = take.isAudio ? (take.frames == 0 || take.channels <= 0) : take.notes.empty();
    if (!empty && project)
        project->pushRecordedTake(std::move(take));
}

void TrackManager::removeTrackNow(uint16_t trackID) {
    auto it = ids.find(trackID);
    if (it == ids.end()) return;
    size_t index = it->second;

    Track* track = tracks[index];
    Connection* connection = track->connection;
    auto& out = parentNode->outputs;

    if (track->capture.active)
        finalizeTake(parentNode->project, track);

    removeInputConnectionForTrack(track);

    if (connection) {
        auto cit = std::find(out.connections.begin(), out.connections.end(), connection);
        if (cit != out.connections.end()) {
            size_t cidx = std::distance(out.connections.begin(), cit);
            if (cidx != out.connections.size() - 1) {
                std::swap(out.connections[cidx], out.connections.back());
                out.ids[out.connections[cidx]->id] = cidx;
            }
            out.ids.erase(connection->id);
            out.id_pool.releaseID(connection->id);
            out.connections.pop_back();
        }
        delete connection;
    }

    id_pool.releaseID(track->id);
    if (index != tracks.size() - 1) {
        std::swap(tracks[index], tracks.back());
        ids[tracks[index]->id] = index;
    }
    ids.erase(track->id);
    if (movingTrack == track) movingTrack = nullptr;
    delete tracks.back();
    tracks.pop_back();

    syncInputPortOrder();
    parentNode->makeConnectionRects();
}

Track* TrackManager::getTrack(uint16_t id) {
    auto index = getIndex(id);
    if (index == -1) return nullptr;
    return tracks[index];
}

int TrackManager::getIndex(uint16_t id) {
    auto it = ids.find(id);
    if( it == ids.end()) return -1; 
    return it->second;
}

int TrackManager::getID(int index) {
    if (index < tracks.size()) return tracks[index]->id;
    else return -1;
}

void TrackManager::solo(uint16_t id) {
    auto it = std::find(soloTracks.begin(), soloTracks.end(), id);
    if (it != soloTracks.end()) {
        soloTracks.erase(it);
    } else {
        soloTracks.push_back(id);
    }
};

void TrackManager::mute(uint16_t id) {
    auto it = std::find(muteTracks.begin(), muteTracks.end(), id);
    if (it != muteTracks.end()) {
        muteTracks.erase(it);
    } else {
        muteTracks.push_back(id);
    }
}

bool TrackManager::mouseOn(SDL_FRect* rect) {
    int x = *mouseX;
    int y = *mouseY;
    return (
            x > rect->x &&
            x < rect->x + rect->w &&
            y > rect->y &&
            y < rect->y + rect->h
        );
}

void TrackManager::render(SDL_Renderer* renderer) {

    // Update button rects from current dstRect (may have changed due to resize).
    newTrackRectE = SDL_FRect{
        dstRect->x, dstRect->y + dstRect->h - 20.0f,
        dstRect->w / 2.0f, 20.0f
    };
    newTrackRectW = SDL_FRect{
        dstRect->x + dstRect->w / 2.0f, dstRect->y + dstRect->h - 20.0f,
        dstRect->w / 2.0f, 20.0f
    };

    SDL_FRect trackRect{
        dstRect->x, dstRect->y - *scrollY, dstRect->w, *divHeight
    };

    SDL_SetRenderDrawColor(renderer, colors.trackBackground[0], colors.trackBackground[1], colors.trackBackground[2], colors.trackBackground[3]);
    SDL_RenderFillRect(renderer, dstRect);

    for(auto track : tracks) {
        renderTrack(renderer, track, &trackRect);
        trackRect.y += *divHeight;
        if(trackRect.y >= dstRect->y + dstRect->h) break;
    }

    if (movingTrack) {
        size_t moveFromIDX = ids[movingTrack->id];
        int raw_new_idx = static_cast<int>(moveFromIDX) + moveAmount;
        size_t new_idx = std::max(0, std::min(raw_new_idx, static_cast<int>(tracks.size())));
        if (moveAmount > 0 && new_idx != tracks.size()) new_idx++;

        float y = new_idx * *divHeight - *scrollY + dstRect->y;
        thickLineRGBA(renderer, dstRect->x, y, dstRect->x + dstRect->w, y, 3, 0, 0, 255, 255);
    }

    newTrackE->inactive = {colors.trackNotes[0], colors.trackNotes[1], colors.trackNotes[2], 180};
    newTrackE->inactive_hover = {colors.trackNotes[0], colors.trackNotes[1], colors.trackNotes[2], 220};
    newTrackE->renderer = renderer;
    newTrackE->render();
    newTrackW->inactive = {colors.trackAudio[0], colors.trackAudio[1], colors.trackAudio[2], 180};
    newTrackW->inactive_hover = {colors.trackAudio[0], colors.trackAudio[1], colors.trackAudio[2], 220};
    newTrackW->renderer = renderer;
    newTrackW->render();
}

bool TrackManager::handleTrackInput(Track* track, int y, SDL_Event& e) {
    SDL_FRect trackRect{
        dstRect->x, static_cast<float>(y) - static_cast<float>(*scrollY), dstRect->w, *divHeight
    };

    if (e.type != SDL_EVENT_MOUSE_BUTTON_DOWN || e.button.button != SDL_BUTTON_LEFT)
        return false;

    const TrackButtons buttons = trackButtonsFor(trackRect);
    auto hit = [this](const SDL_FRect& r) {
        return *mouseX >= r.x && *mouseX < r.x + r.w && *mouseY >= r.y && *mouseY < r.y + r.h;
    };

    int control = -1;
    if (hit(buttons.solo)) control = 0;
    else if (hit(buttons.mute)) control = 1;
    else if (hit(buttons.arm)) control = 2;
    else if (hit(buttons.monitor)) control = 3;
    if (control < 0) return false;

    Project* p = parentNode ? parentNode->project : nullptr;
    if (!p || !p->um || !parentNode->nm) return true;
    p->um->newAction(new ArrangerTrackControlAction(p, parentNode->nm->managerPath,
        static_cast<int>(parentNode->id), track->id, control));
    return true;
}

void TrackManager::moveTrack() {
    if (!movingTrack) return;

    auto connection = movingTrack->connection;

    auto it = std::find(tracks.begin(), tracks.end(), movingTrack);
    size_t idx = std::distance(tracks.begin(), it);

    tracks.erase(it);

    int raw_new_idx = static_cast<int>(idx) + moveAmount;
    size_t new_idx = std::max(0, std::min(raw_new_idx, static_cast<int>(tracks.size())));
    tracks.insert(tracks.begin() + new_idx, movingTrack);

    // that messed up the id map so rebuild it
    ids.clear();
    for (size_t i = 0; i < tracks.size(); ++i) ids[tracks[i]->id] = i;       

    movingTrack = nullptr;

    auto& connections = parentNode->outputs.connections;
    auto it1 = std::find(connections.begin(), connections.end(), connection);
    idx = std::distance(connections.begin(), it1);
    connections.erase(it1);

    raw_new_idx = static_cast<int>(idx) + moveAmount;
    new_idx = std::max(0, std::min(raw_new_idx, static_cast<int>(connections.size())));
    connections.insert(connections.begin() + new_idx, connection);

    // that messed up the id map so rebuild it
    parentNode->outputs.ids.clear();
    for (size_t i = 0; i < connections.size(); ++i) parentNode->outputs.ids[connections[i]->id] = i;

    syncInputPortOrder();
    parentNode->makeConnectionRects();
}

void TrackManager::handleInput(SDL_Event& e) {

    hoveredTrack = nullptr;
    bool consumed = false;

    int y = dstRect->y;
    for (auto track : tracks) {
        const float rowTop = static_cast<float>(y) - static_cast<float>(*scrollY);
        if (*mouseY >= rowTop && *mouseY < rowTop + *divHeight) {
            hoveredTrack = track;
            consumed = handleTrackInput(track, y, e);
            break;
        }
        y += static_cast<int>(*divHeight);
    }

    switch(e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                if (consumed) break;
                if( newTrackE->hover()) newTrackE->onClick();
                else if( newTrackW->hover()) {
                    newTrackW->onClick();
                }
                movingTrack = hoveredTrack;
                last_lmb_y = *mouseY;
            } else if (e.button.button == SDL_BUTTON_RIGHT && hoveredTrack) {
                auto* ctxMenu = ContextMenu::get();
                ctxMenu->activate();
                uint16_t tid = hoveredTrack->id;
                int ttype = static_cast<int>(hoveredTrack->type);
                auto root = uTreeEntry();
                auto del = uTreeEntry();
                del->label = "Delete Track";
                del->click = [this, tid, ttype]() {
                    if (!parentNode || !parentNode->project || !parentNode->nm) return;
                    // Find the connection ID for this track.
                    int connID = -1;
                    Track* t = getTrack(tid);
                    if (t && t->connection) connID = static_cast<int>(t->connection->id);
                    auto* action = new RemoveArrangerTrackAction(
                        parentNode->project, parentNode->nm->managerPath,
                        static_cast<int>(parentNode->id), ttype,
                        static_cast<int>(tid), connID);
                    parentNode->project->um->newAction(action);
                };
                root->addChild(del);
                ctxMenu->dynamicTick = getTreeMenuTicker(root);
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                moveTrack();
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            moveAmount = std::round((*mouseY - last_lmb_y) / *divHeight);
            break;
        default:
            break;
    };
}

void TrackManager::renderTrack(SDL_Renderer* renderer, Track* track, SDL_FRect* rect) {

    //body

    if (track == hoveredTrack) {
        uint8_t r = std::min(255, colors.trackBody[0] + 10);
        uint8_t g = std::min(255, colors.trackBody[1] + 10);
        uint8_t b = std::min(255, colors.trackBody[2] + 10);
        SDL_SetRenderDrawColor(renderer, r, g, b, 255);
    } else {
        SDL_SetRenderDrawColor(renderer, colors.trackBody[0], colors.trackBody[1], colors.trackBody[2], colors.trackBody[3]);
    }
    SDL_RenderFillRect(renderer, rect);

    //type indicator
    SDL_FRect typeRect{
        rect->x, rect->y, rect->w / 10.0f, rect->h
    };
    switch(track->type) {
        case TrackType::Audio:
            SDL_SetRenderDrawColor(renderer, colors.trackAudio[0], colors.trackAudio[1], colors.trackAudio[2], colors.trackAudio[3]);
            break;
        case TrackType::Notes:
            SDL_SetRenderDrawColor(renderer, colors.trackNotes[0], colors.trackNotes[1], colors.trackNotes[2], colors.trackNotes[3]);
            break;
        default:
            break;
    }
    SDL_RenderFillRect(renderer, &typeRect);

    // bus text - needs optimization

    std::string text = std::to_string(track->id);
    SDL_Color color{0, 0, 0, 255};

    SDL_Surface* surf = TTF_RenderText_Blended(fonts.mainFont, text.c_str(), 0, color);
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);

    SDL_RenderTexture(renderer, tex, nullptr, &typeRect);

    SDL_DestroySurface(surf);
    SDL_DestroyTexture(tex);

    // Per-track controls: solo, mute, record-arm, monitor.
    const TrackButtons buttons = trackButtonsFor(*rect);
    renderSmallButton(renderer, buttons.solo, "S",
        isSoloed(track->id) ? SDL_Color{255, 200, 50, 255} : SDL_Color{70, 70, 75, 255});
    renderSmallButton(renderer, buttons.mute, "M",
        isMuted(track->id) ? SDL_Color{255, 60, 60, 255} : SDL_Color{70, 70, 75, 255});
    renderSmallButton(renderer, buttons.arm, "R",
        track->recordArmed ? SDL_Color{210, 30, 30, 255} : SDL_Color{70, 70, 75, 255});
    renderSmallButton(renderer, buttons.monitor, "I",
        track->monitor ? SDL_Color{70, 190, 120, 255} : SDL_Color{70, 70, 75, 255});

    //borders
    SDL_SetRenderDrawColor(renderer, colors.trackBorder[0], colors.trackBorder[1], colors.trackBorder[2], colors.trackBorder[3]);
    SDL_RenderRect(renderer, &typeRect); //type
    SDL_RenderRect(renderer, rect); //outer
}

void TrackManager::process(float* input, int bufferSize) {
    for(auto track : tracks) {
        track->process(input, bufferSize);
    }
}

void TrackManager::fromJSON(json j) {
    for (auto* t : tracks) {
        delete t;
    }
    tracks.clear();
    ids.clear();
    id_pool = idManager();
    soloTracks.clear();
    muteTracks.clear();

    soloTracks = j["soloTracks"].get<std::vector<uint16_t>>();
    muteTracks = j["muteTracks"].get<std::vector<uint16_t>>();

    for(auto t : j["tracks"] ) {
        Track* tr = new Track;
        tr->connection = parentNode->outputs.getConnection(t["connectionID"]);
        if (!tr->connection) {
            delete tr;
            continue;
        }
        tr->buffer = &(tr->connection->buffer);
        tr->events = &(tr->connection->events);
        tr->fromJSON(t);
        tracks.push_back(tr);

        // Resolve the per-track input port; older saves have none, so create one in track
        // order. Both graph copies deserialize the same JSON in the same order.
        const int inputID = t.value("inputConnectionID", -1);
        tr->inputConnection = inputID >= 0
            ? parentNode->inputs.getConnection(static_cast<uint16_t>(inputID))
            : nullptr;
        if (!tr->inputConnection)
            tr->inputConnection = addInputConnection(tr->type, -1, static_cast<int>(tracks.size()) - 1);
    }

    id_pool.fromJSON(j["id_pool"]);

    for( auto [id, index] : j["ids"].items()) {
        ids[static_cast<uint16_t>(std::stoi(id))] = index.get<uint16_t>();
    }
}

json TrackManager::toJSON() {
    json j;
    j["soloTracks"] = soloTracks;
    j["muteTracks"] = muteTracks;

    j["tracks"] = json::array();
    for( auto t : tracks ){
        auto jt = t->toJSON();
        jt["connectionID"] = t->connection->id;
        jt["inputConnectionID"] = t->inputConnection ? static_cast<int>(t->inputConnection->id) : -1;
        j["tracks"].push_back(jt);
    };

    j["id_pool"] = id_pool.toJSON();

    j["ids"] = json::object();
    for( auto [id, index] : ids) {
        j["ids"][std::to_string(id)] = index;
    }

    return j;
}
