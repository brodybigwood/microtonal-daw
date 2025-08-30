#include "Wavnode.h"
#include "AudioManager.h"

Wavnode::Wavnode(char* filepath, PlugLib* library) {
	lib = static_cast<WavLib*>(library);
	size_t len = std::strlen(filepath) + 1;
	this->path = new char[len];
	std::memcpy(this->path, filepath, len);

	auto project = Project::instance();
	if(project->processing) {
		setup();
	};
}

Wavnode::~Wavnode() {}

void Wavnode::toggle() {};
void Wavnode::setup() {
	setupPlug = (setupFunc)dlsym(lib->handle, "setup");
	if(!setupPlug) throw std::runtime_error(dlerror());
	auto am = AudioManager::instance();
	ProcessContext ctx {
		am->sampleRate,
		am->bufferSize,
		am->inputChannels,
		am->outputChannels
	};

	setupPlug(ctx);

	processPlug = (processFunc)dlsym(lib->handle, "process");
	if(!processPlug) throw std::runtime_error(dlerror());
};
void Wavnode::process(audioData data, EventList* events) {
	processPlug(data);
};
void Wavnode::showWindow() {};
bool Wavnode::editorTick() {return true;};
void Wavnode::serialize(std::filesystem::path folder) {};
void Wavnode::deSerialize(std::filesystem::path folder) {};
