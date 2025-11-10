#pragma once

#include "TuningTable.h"

class ScaleManager{
    public:
        static ScaleManager* instance();

        std::vector<TuningTable*> scales;

        void addScale(TuningTable&);

        TuningTable* lastScale;
        TuningTable* getLastScale();

        ~ScaleManager();
};
