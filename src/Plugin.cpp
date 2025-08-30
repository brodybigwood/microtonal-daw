#include "Plugin.h"
#include "VstPlugin.h"
#include "PluginManager.h"

Plugin::Plugin(char* filepath) {
	lib = PluginManager::instance().load(filepath);
	obj = std::make_unique<VstPlugin>(filepath, lib);
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
	return j;
}

void Plugin::fromJSON(json j) {
	std::filesystem::path folder = std::filesystem::path(project->filepath)
	/ "racks"
	/ std::to_string(rack->id)
	/ std::to_string(rack->getIndex(this));

	obj->deSerialize(folder);
}

