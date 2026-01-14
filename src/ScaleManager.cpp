#include "ScaleManager.h"
#include <algorithm>
#include <iostream>
ScaleManager* ScaleManager::instance() {
    static ScaleManager sm;
    return &sm;
}

ScaleManager::~ScaleManager() {
    for(auto s : scales) {
        delete s;
    }
    scales.clear();
}

TuningTable* ScaleManager::getLastScale() {
    if(lastScale == nullptr) {
        if(scales.empty()) {
	    auto s = new TuningTable(false);
	    s->id = id_manager.newID();
            scales.push_back(s);
	    id_to_index[s->id] = scales.size() - 1;
        }
        lastScale = scales.front();

        std::cout << "it was a nullptr" << std::endl;
    }
    return lastScale;
}

TuningTable* ScaleManager::byID(uint16_t id) {
    auto index = id_to_index[id];

    return scales[index];
}

void ScaleManager::addScale(TuningTable& t) {
    auto it = std::find_if(scales.begin(), scales.end(),
            [t](auto tt) {
            return tt->id == t.id;
        });

    if(it != scales.end()) {
        lastScale = *it;
        return;
    }

    TuningTable* n = new TuningTable(false);
    n->filepath = t.filepath;
    n->name = t.name;
    n->notes = t.notes;
    n->id = id_manager.newID();
    scales.push_back(n);

    id_to_index[n->id] = scales.size() - 1;

    lastScale = n;
}


json ScaleManager::serialize() {
    json j;

    j["scales"] = json::array();

    for( auto scale : scales) {
        json s = scale->serialize();
        j["scales"].push_back(s);
    }
    j["idManager"] = id_manager.toJSON();

    return j;
}

void ScaleManager::deSerialize(json j) {
    id_manager.fromJSON(j["idManager"]);

    for(auto s : j["scales"]) {
        auto scale = new TuningTable(false);
        scale->deSerialize(s);
        scales.push_back(scale);
        
        id_to_index[scale->id] = scales.size() - 1;

    }
}
