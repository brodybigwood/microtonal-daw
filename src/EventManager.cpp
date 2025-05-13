#include "EventManager.h"

EventManager::EventManager(Project* project) {
    this->project = project;
    this->regions = &(project->regions);
    this->instruments = &(project->instruments);
    this->automations = &(project->automations);
    this->files = &(project->files);
    this->recordings = &(project->recordings);
    this->isPlaying = &(project->isPlaying);
}

EventManager::~EventManager() {

}

void EventManager::tick() {

}