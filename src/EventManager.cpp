#include "EventManager.h"
#include <cstdlib>
#include <thread>
#include "Project.h"
#include <iostream>
#include "AudioManager.h"
#include "public.sdk/source/vst/vstnoteexpressiontypes.h"

#include "Region.h"


#include <algorithm>
#include <cmath>

EventManager::EventManager() {
}

EventManager::~EventManager() {

}

void EventManager::clearEvents() {
}

void EventManager::getEvents() {
}

void EventManager::tick() {
    time = project->tempo * project->timeSeconds/60.0f;
    window = (project->tempo * (float)AudioManager::instance()->bufferSize / AudioManager::instance()->sampleRate) / 60.0f;

    if(project->isPlaying) {
        getEvents();
    } else {
        clearEvents();
    }
}
