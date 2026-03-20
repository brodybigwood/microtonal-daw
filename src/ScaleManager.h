#pragma once

#include "TuningTable.h"
#include "idManager.h"
#include <unordered_map>

class ScaleManager{
    public:
        std::vector<TuningTable*> scales;

        TuningTable* byID(uint16_t);

        void addScale(TuningTable&);

        TuningTable* lastScale = nullptr;
        TuningTable* getLastScale();

        json serialize();

        void deSerialize(json);

        ~ScaleManager();

    private:

        std::unordered_map<uint16_t, uint16_t> id_to_index;
        idManager id_manager;
};
