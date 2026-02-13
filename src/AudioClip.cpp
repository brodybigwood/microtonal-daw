#include "AudioClip.h"
#include <cmath>

AudioClip::~AudioClip() {
    if (buffer) delete[] buffer;
}

json AudioClip::toJSON() {
    json j;

    j["name"] = name;
    j["id"] = id;
    j["type"] = ElementType::audioClip;
    j["positions"] = GridElement::toJSON();

    j["filepath"] = filepath;

    return j;
}

void AudioClip::fromJSON(json j) {
    name = j["name"];
    id = j["id"];
    type = j["type"];
    GridElement::fromJSON(j["positions"]);
}

void AudioClip::draw(SDL_Renderer* renderer) {
    // gonna draw the waveform here    
}

void AudioClip::setFile(std::string file_path) {
    if (buffer) delete[] buffer;

    filepath = file_path;

    buffer = new float[48000 * 5];

    for (size_t i = 0; i < 48000 * 5; ++i) {
        buffer[i] = std::sin(2 * M_PI * 48000);
    }
}
