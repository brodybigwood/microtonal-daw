#include "Track.h"
#include "BusManager.h"

void Track::setBus(uint8_t id) {
    BusManager* bm = BusManager::get();

    switch(type) {
        case TrackType::Automation:
            dstBus = bm->getBusW(id);
            break;
        case TrackType::Audio:
            dstBus = bm->getBusW(id);
            break;
        case TrackType::Notes:
            dstBus = bm->getBusE(id);
            break;
        default:
            break;
    }

    if(dstBus != nullptr) busID = dstBus->id;
}

void Track::process(float* input, int bufferSize) {

}

TrackType& Track::getType() {
    return type;
}

void Track::render(SDL_Renderer* renderer, SDL_FRect& dstRect) {
    //body
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(renderer, &dstRect);

    //type indicator
    SDL_FRect typeRect{
        dstRect.x, dstRect.y, dstRect.w / 10.0f, dstRect.h
    };
    switch(type) {
        case TrackType::Audio:
            SDL_SetRenderDrawColor(renderer, 255, 120, 120, 255);
            break;
        case TrackType::Automation:
            SDL_SetRenderDrawColor(renderer, 255, 220, 50, 255);
            break;
        case TrackType::Notes:
            SDL_SetRenderDrawColor(renderer, 120, 255, 120, 255);
            break;
        default:
            break;
    }
    SDL_RenderFillRect(renderer, &typeRect);

    //borders
    SDL_SetRenderDrawColor(renderer, 120, 120, 120, 255);
    SDL_RenderRect(renderer, &typeRect); //type
    SDL_RenderRect(renderer, &dstRect); //outer
}

void Track::handleInput(SDL_Event& e) {

}

void Track::fromJSON(json j) {
    type = static_cast<TrackType>(j["type"].get<uint16_t>());
    id = j["id"].get<uint16_t>();
    busID = j["busID"].get<int>();
    inputChannel = j["inputChannel"].get<uint8_t>();
}

json Track::toJSON() {
    json j;

    j["type"] = type;
    j["id"] = id;
    j["busID"] = busID;
    j["inputChannel"] = inputChannel;

    return j;
}
