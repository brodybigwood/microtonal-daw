#include "EventManager.h"
#include <thread>
#include "Plugin.h"
#include "Project.h"
#include <iostream>
#include "AudioManager.h"

EventManager::EventManager() {
}

EventManager::~EventManager() {

}

void EventManager::clearEvents() {
    for (Region* region : project->regions) {
        for (Note& note : region->notes) {
            if (note.dispatched) {
                Steinberg::Vst::Event e{};
                e.type = Steinberg::Vst::Event::kNoteOffEvent;
                e.noteOff.channel = 0;    // or correct channel
                e.noteOff.pitch = note.num;
                e.noteOff.velocity = 0.f;
                e.noteOff.noteId = -1;
                e.sampleOffset = 0;

                for (auto index : region->outputs) {
                    if (index < project->instruments.size()) {
                        auto* inst = project->instruments[index];
                        inst->eventList.addEvent(e);
                    }
                }
                note.dispatched = false;
            }
        }
    }

}

void EventManager::getEvents() {


    for(Region* region :project->regions) {

        std::vector<Steinberg::Vst::Event> events;

        for(Note& note :region->notes) {

            if(note.dispatched && note.end < time+window && note.end >= time) {

                Steinberg::Vst::Event e{};
                e.type = Steinberg::Vst::Event::kNoteOffEvent;
                e.noteOff.channel = 0;
                e.noteOff.pitch = note.num;
                e.noteOff.velocity = 1.0f;
                e.noteOff.noteId = -1;
                e.sampleOffset = AudioManager::instance()->sampleRate * 60.0f * (note.end - time)/project->tempo;

                events.push_back(e);


                std::cout<<"noteoff "<<note.num<<std::endl;
                note.dispatched = false;

            } else if(!note.dispatched && note.start < time+window && note.start >= time) {

                Steinberg::Vst::Event e{};
                e.type = Steinberg::Vst::Event::kNoteOnEvent;
                e.noteOn.channel = 0;
                e.noteOn.pitch = note.num;
                e.noteOn.velocity = 1.0f;
                e.noteOn.noteId = -1;
                e.noteOn.length = 0;
                e.sampleOffset = AudioManager::instance()->sampleRate * 60.0f * (note.start - time)/project->tempo;

                events.push_back(e);

                std::cout<<"noteon "<<note.num<<std::endl;
                note.dispatched = true;

            }
        }
        if(region->outputType == "Instruments") {
            for (auto index : region->outputs) {
                Instrument* inst = project->instruments[index];
                for(auto& e :events) {
                  inst->eventList.addEvent(e);
                }
            }


        }

    }

}

void EventManager::run() {
    while(running) {


        time = project->tempo * project->timeSeconds/60.0f;

        window = (project->tempo * (float)AudioManager::instance()->bufferSize / AudioManager::instance()->sampleRate) / 60.0f;




        if(project->isPlaying) {
            getEvents();
        } else {
            clearEvents();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

void EventManager::stop() {
    running = false;
    if (eventThread.joinable()) {
        eventThread.join();
    }
}


void EventManager::start(){
    running = true;
    eventThread = std::thread([this]() {
        this->run();
    });
}
