#include "ScaleManager.h"
#include <algorithm>

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
            scales.push_back(new TuningTable(false));
        }
        lastScale = scales.front();
    }
    return lastScale;
}

void ScaleManager::addScale(TuningTable& t) {
    bool exists = std::any_of(scales.begin(), scales.end(),
            [t](auto tt) {
            return tt->id == t.id;
        });

    if(exists) return;

    TuningTable* n = new TuningTable(false);
    n->filepath = t.filepath;
    n->name = t.name;
    n->notes = t.notes;
    n->id = t.id;

    scales.push_back(n);
    lastScale = n;
}
