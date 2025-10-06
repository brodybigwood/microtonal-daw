#include "idManager.h"

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


