#include "MixerTrack.h"
#include "EventManager.h"
#include "Project.h"

MixerTrack::MixerTrack(Project* project) {
    bar.type = VolumeBar::Type::Vertical;
    this->id = (project->id_track)++;
}


MixerTrack::~MixerTrack() {
    childInstruments.clear();
    childTracks.clear();
}

void MixerTrack::process(audioData data) {

    channel* tempChannels = new channel[data.output.numChannels];

    for (size_t i = 0; i < data.output.numChannels; i++) {
        tempChannels[i].buffer = new float[data.bufferSize]();
    }

    audioStream output {
        tempChannels,
        data.output.numChannels
    };

    audioData tempData {
        data.input,
        output,
        data.bufferSize
    };


    for (size_t i = 0; i < childTracks.size(); i++) {
        childTracks[i]->process(tempData);
    }
    for (size_t i = 0; i < childInstruments.size(); i++) {
        childInstruments[i]->process(data);//left side
    }
    rack.process(data);

    for (size_t i = 0; i < data.output.numChannels; i++) {
        for (size_t j = 0; j < data.bufferSize; j++) {
            data.output.channels[i].buffer[j] += tempChannels[i].buffer[j];
        }
    }


    for (size_t i = 0; i < data.output.numChannels; i++) {
        delete[] tempChannels[i].buffer;
    }
    delete[] tempChannels;
}

json MixerTrack::toJSON() {
    json j;
    j["name"] = name;
    j["id"] = id;

    json arr1 = json::array();
    for (auto child : childInstruments) {
        arr1.push_back(child->id);
    }
    j["childInstruments"] = arr1;

    json arr2 = json::array();
    for (auto child : childTracks) {
        arr2.push_back(child->id);
    }
    j["childTracks"] = arr2;

    j["rack"] = rack.toJSON();

    return j;
}

void MixerTrack::fromJSON(json js) {

    constexpr uint16_t NO_ID = std::numeric_limits<uint16_t>::max();

    json j;
    if (js.contains("rack")) {
        j = js["rack"];
    }
    if (j.contains("rack")) {
        json jr = j["rack"];
        Project::instance()->id_rack = jr.value("id", NO_ID);
        rack = Rack();
        rack.fromJSON(jr);
        Project::instance()->racks.push_back(&rack);
    }

    uint16_t max_id = 0;
    for (auto rack : Project::instance()->racks) {
        if (rack->id > max_id) max_id = rack->id;
    }
    Project::instance()->id_rack = max_id + 1;


}

void MixerTrack::render(SDL_Renderer* renderer, SDL_FRect* dstRect) {
    SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
    SDL_RenderRect(renderer, dstRect);
    SDL_SetRenderDrawColor(renderer, 70, 70, 70, 255);
    SDL_RenderFillRect(renderer, dstRect);
    bar.render(renderer, dstRect);
}
