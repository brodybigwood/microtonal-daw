#include <thread>
#include "Project.h"

#ifndef EVENTMANAGER_H
#define EVENTMANAGER_H

class EventManager {
    public:
        EventManager(Project* project);
        ~EventManager();
        Project* project;
        std::vector<Region*>* regions;
        std::vector<Instrument*>* instruments;
        std::vector<Region>* files;
        std::vector<Region>* recordings;
        std::vector<Region>* automations;

        bool* isPlaying;

        void tick();

};
#endif