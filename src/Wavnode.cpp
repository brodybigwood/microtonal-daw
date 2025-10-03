#include "Wavnode.h"
#include "AudioManager.h"
#include <dlfcn.h>

Wavnode::Wavnode(char* filepath) {
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
	getNodeInstance = (nodeGetter)dlsym(handle, "getNodeInstance");
	if(!getNodeInstance) throw std::runtime_error(dlerror());

    mainNode = getNodeInstance();

	auto am = AudioManager::instance();
	ProcessContext ctx {
		am->sampleRate,
		am->bufferSize,
		am->outputChannels,
		am->outputChannels
	};

	mainNode->setup(ctx);
};
void Wavnode::process(audioData data, EventList* events) {
	mainNode->process(data);
};
void Wavnode::showWindow() {};
bool Wavnode::editorTick() {return true;};
void Wavnode::serialize(std::filesystem::path folder) {};
void Wavnode::deSerialize(std::filesystem::path folder) {};
