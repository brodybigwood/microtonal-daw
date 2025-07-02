#include "EventManager.h"
#include <thread>
#include "Plugin.h"
#include "Project.h"
#include <iostream>
#include "AudioManager.h"
#include "public.sdk/source/vst/vstnoteexpressiontypes.h"




#include <algorithm>
#include <cmath>

EventManager::EventManager() {
}

EventManager::~EventManager() {

}

void EventManager::clearEvents() {
    for (DAW::Region* region : project->regions) {
        for (Note& note : region->notes) {
            if (note.dispatched) {
                Steinberg::Vst::Event e{};
                e.type = Steinberg::Vst::Event::kNoteOffEvent;
                e.noteOff.channel = note.channel;    // or correct channel
                e.noteOff.pitch = note.num;
                e.noteOff.velocity = 0.f;
                e.noteOff.noteId = note.id;
                e.sampleOffset = 0;

                region->inst->eventList.addEvent(e);

                note.dispatched = false;
            }
        }
    }

}
void EventManager::injectMPE(std::vector<Steinberg::Vst::Event>& events, Note& note, int& sampleOffset) {
    Steinberg::Vst::Event e{};
    e.type = Steinberg::Vst::Event::kLegacyMIDICCOutEvent;
    e.sampleOffset = sampleOffset;

    e.midiCCOut.channel = static_cast<uint8_t>(note.channel);
    e.midiCCOut.controlNumber = 129;

    int bendValue = static_cast<int>((note.num - std::floor(note.num)) * 8192 + 8192);
    bendValue = std::clamp(bendValue, 0, 16383);

    e.midiCCOut.value = bendValue & 0x7F;
    e.midiCCOut.value2 = (bendValue >> 7) & 0x7F;

    events.push_back(e);

    std::cout<<"mpe "<<note.channel<<std::endl;
}





void EventManager::getEvents() {


    for(DAW::Region* region :project->regions) {

        double regTime = region->startTime;

        std::vector<Steinberg::Vst::Event> events;

        for(Note& note :region->notes) {

            double start = note.start + regTime;
            double end = note.end + regTime;
            int offset = AudioManager::instance()->sampleRate * 60.0f * (end - time)/project->tempo;

            if(note.dispatched && end < time+window && end >= time) {



                Steinberg::Vst::Event e{};
                e.type = Steinberg::Vst::Event::kNoteOffEvent;
                e.noteOff.channel = note.channel;
                e.noteOff.pitch = note.num;
                e.noteOff.velocity = 1.0f;
                e.noteOff.noteId = note.id;
                e.sampleOffset = offset;

                events.push_back(e);


                std::cout<<"noteoff "<<note.num<<std::endl;
                note.dispatched = false;






            } else if(!note.dispatched && start < time+window && start >= time) {

                Steinberg::Vst::Event e{};
                e.type = Steinberg::Vst::Event::kNoteOnEvent;
                e.noteOn.channel = note.channel;
                e.noteOn.pitch = note.num;
                e.noteOn.velocity = 1.0f;
                e.noteOn.noteId = note.id;
                e.noteOn.length = 0;
                e.noteOn.tuning = 50.0f;
                e.sampleOffset = offset;

                events.push_back(e);


                injectMPE(events, note, offset);

                std::cout<<"noteon "<<note.num<<std::endl;
                note.dispatched = true;





            }
        }

        for(auto& e :events) {
            region->inst->eventList.addEvent(e);
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
