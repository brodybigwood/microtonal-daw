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
	getNodeInstance = (nodeGetter)dlsym(lib->handle, "getNodeInstance");
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
