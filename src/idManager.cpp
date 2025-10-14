#include "idManager.h"
#include <algorithm>

uint16_t idManager::newID() {
    if(!free_ids.empty()) {
        uint16_t id = free_ids.back();
        free_ids.pop_back();
        return id;
    }
    return next_id++;
}

void idManager::releaseID(uint16_t id) {
    free_ids.push_back(id);
}

bool idManager::reserveID(uint16_t id) {
    auto it = std::find(free_ids.begin(), free_ids.end(), id);
    if(it != free_ids.end()) {
        free_ids.erase(it);
    }
        
    next_id = id + 1;

    return true;
}

void idManager::fromJSON(json j) {
    free_ids = j["free_ids"].get<std::vector<uint16_t>>();
    next_id = j["next_id"].get<uint16_t>();
}

json idManager::toJSON() {
    json j;
    j["free_ids"] = free_ids;
    j["next_id"] = next_id;

    return j;
}
