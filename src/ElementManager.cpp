#include "ElementManager.h"
#include "Region.h"
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

    j["elements"] = json::array();

    for(auto e : elements) {
        json je = e->toJSON();
        j["elements"].push_back(je);
    }

    return j;
}

void ElementManager::process(int bufferSize) {
    float epsilon = 1e-6;

    float window = static_cast<float>(AudioManager::instance()->bufferSize) / AudioManager::instance()->sampleRate;
    float time = static_cast<float>(project->timeSeconds.load());

    for (auto* element : elements)
        for (auto position : element->positions) {
            auto& pos = *position;
            const float regTimeSec = Note::secondsFromVector(pos.rhythmVector);

            Track* track = tm->getTrack(pos.trackID);
            auto& dispatched = track->dispatched;
            switch (element->type) {
                case ElementType::region:
                    {
                        if (!project->isPlaying.load()) {
                            for (auto& note : dispatched) {
                                Event event {
                                    noteEventType::noteOff,
                                    note->num,
                                    note->id,
                                    0
                                };
                                track->addEvent(event);
                            }

                            dispatched.clear();
                            break;
                        }
                        auto* region = static_cast<Region*>(element);
                        const float trim = Note::secondsFromVector(pos.startOffsetPairs);
                        for (auto& note : region->notes) {
                            float start = note->startSeconds() + regTimeSec - trim;
                            float end = note->endSeconds() + regTimeSec - trim;

                            if (std::find(dispatched.begin(), dispatched.end(), note) == dispatched.end() && start < time+window+epsilon && start+epsilon >= time) {

                                int offset = static_cast<int>(AudioManager::instance()->sampleRate * (start - time));
 
                                Event event {
                                    noteEventType::noteOn,
                                    note->num,
                                    note->id,
                                    offset
                                };
            
                                track->addEvent(event);

                                dispatched.push_back(note);
                            } else if (std::find(dispatched.begin(), dispatched.end(), note) != dispatched.end() && end < time+window+epsilon && end+epsilon >= time) {

                                int offset = static_cast<int>(AudioManager::instance()->sampleRate * (end - time));


                                Event event {
                                    noteEventType::noteOff,
                                    note->num,
                                    note->id,
                                    offset
                                };

                                track->addEvent(event);

                                dispatched.erase(std::remove(dispatched.begin(), dispatched.end(), note), dispatched.end());
                            }
                        }
                    }
                    break;
                case ElementType::audioClip:
                    {
// first find out if there is overlap with current block
// pos.start is start of this position in beats
// pos.end is the end of this position in beats
// time is current processing time in beats
// audio range is pos.start to pos.end
// time must be anywhere between    
                        if (!project->isPlaying.load()) break;
                        if (!(*track->buffer)) break;

                        const double localSec = time - static_cast<double>(Note::secondsFromVector(pos.rhythmVector));
                        const double fileSec = localSec + static_cast<double>(Note::secondsFromVector(pos.startOffsetPairs));
                        int readIdx = static_cast<int>(fileSec * AudioManager::instance()->sampleRate);
                        if (readIdx < 0) break;
                        AudioClip* ac = static_cast<AudioClip*>(element);
                        float* rbuffer = ac->buffer;
                        float* wbuffer = *(track->buffer);
                        for (size_t i = 0; i < bufferSize; ++i) {
                            if (readIdx >= ac->num_samples) {
                                wbuffer[i] += 0;
                            } else {
                                wbuffer[i] += rbuffer[readIdx];
                            }
                            readIdx++;
                        }
                    }
                    break;
                default:
                    break;
            }
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

    id_pool.fromJSON(j["id_pool"]);

    for(json e : j["elements"]) {
        GridElement* ge;

        switch (e["type"].get<int>()) {
            case ElementType::region:
                ge = new Region(project, parentNode);
                break;
            case ElementType::audioClip:
                ge = new AudioClip(project, parentNode);
                break;
            default:
                return; // unknown type, give up
        }

        ge->pos_id_pool = &id_pool;
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
    r->pos_id_pool = &id_pool;
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
    r->pos_id_pool = &id_pool;
    r->fromJSON(regionJson);
    id_pool.reserveID(rid);
    elements.insert(elements.begin() + static_cast<std::ptrdiff_t>(insertIndex), r);
    ids.clear();
    for (size_t i = 0; i < elements.size(); ++i)
        ids[elements[i]->id] = static_cast<uint16_t>(i);
}

AudioClip* ElementManager::newAudioClip(std::string filepath) {

    auto a = new AudioClip(project, parentNode);

    a->pos_id_pool = &id_pool;
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

    //new region btn
    SDL_FRect rect{dstRect->x, dstRect->y + dstRect->h - bottomMargin, dstRect->w, bottomMargin};
    hoverNew = false;
    if (mouseX >= rect.x && mouseX < (rect.x + rect.w) &&
        mouseY >= rect.y && mouseY < (rect.y + rect.h)) {
            SDL_SetRenderDrawColor(renderer, 40, 120, 40, 255);
            hoveredElement = -1;
            hoverNew = true;
    } else {
            SDL_SetRenderDrawColor(renderer, 20, 60, 20, 255);
            hoverNew = false;
    }
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
    SDL_RenderRect(renderer, &rect);

    SDL_SetRenderClipRect(renderer, NULL);
}

bool ElementManager::handleInput(SDL_Event& e) {
    bool running = true;
    switch (e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                if(hoverNew) {
                    project->um->newAction(new CreateRegionAction(project, parentNode->nm->managerPath, static_cast<int>(parentNode->id)));
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
