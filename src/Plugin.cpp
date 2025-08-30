#include "Plugin.h"
#include "VstPlugin.h"
#include "PluginManager.h"
#include "Wavnode.h"

Plugin::Plugin(char* filepath, PlugFormat p) {
	lib = PluginManager::instance().load(filepath, p);
	switch(p) {
		case PlugFormat::VST: {
			obj = std::make_unique<VstPlugin>(filepath, lib);
			break;
		}
		case PlugFormat::Wavnode: {
			obj = std::make_unique<Wavnode>(filepath, lib);
			break;
		}
	}
	this->rack = obj->rack;
	project = Project::instance();
}

Plugin::~Plugin() {}

void Plugin::process(audioData data, EventList* eventList) {
	obj->process(data, eventList);
}

json Plugin::toJSON() {

	std::filesystem::path folder = std::filesystem::path(project->filepath)
	/ "racks"
	/ std::to_string(rack->id)
	/ std::to_string(rack->getIndex(this));

	std::filesystem::create_directories(folder);



	obj->serialize(folder);
	
	json j;
	j["path"] = obj->path;
	j["format"] = lib->format;
	return j;
}

void Plugin::fromJSON(json j) {
	std::filesystem::path folder = std::filesystem::path(project->filepath)
	/ "racks"
	/ std::to_string(rack->id)
	/ std::to_string(rack->getIndex(this));

	obj->deSerialize(folder);
}

void Plugin::toggle() {
	obj->toggle();
}
