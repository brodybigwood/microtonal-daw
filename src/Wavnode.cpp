#include "Wavnode.h"

Wavnode::Wavnode(char* filepath, PlugLib* library) {
	lib = static_cast<WavLib*>(library);
	size_t len = std::strlen(filepath) + 1;
	this->path = new char[len];
	std::memcpy(this->path, filepath, len);
}

Wavnode::~Wavnode() {};

void Wavnode::toggle() {};
void Wavnode::setup() {};
void Wavnode::process(audioData data, EventList* events) {};
void Wavnode::showWindow() {};
bool Wavnode::editorTick() {return true;};
void Wavnode::serialize(std::filesystem::path folder) {};
void Wavnode::deSerialize(std::filesystem::path folder) {};
