
#ifndef EVENTMANAGER_H
#define EVENTMANAGER_H

#include <thread>
#include <atomic>
#include "EventList.h"

class Project;

class EventManager {
    public:
        EventManager();
        ~EventManager();
        Project* project;

        void getEvents();
        void clearEvents();

        double time;
        double window;

        void start();

        void run();
        void stop();

private:
    std::thread eventThread;
    std::atomic<bool> running = false;

};
#endif
