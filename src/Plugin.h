#pragma once
#include "EventList.h"
#include "Processing.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;
#include "PlugType.h"
#include "AudioManager.h"
#include "Project.h"

class Plugin {
	public:
		void process(audioData, EventList*);

		Plugin(char* filepath);
		~Plugin();

		std::unique_ptr<PlugType> obj;

		void toggle() {
			obj->toggle();
		}

		int id;
		Rack* rack;

		bool processing() {return obj->processing;};
		bool windowOpen() {return obj->windowOpen;};

		void showWindow() {obj->showWindow();};
		bool editorTick() {return obj->editorTick();};
		void setup() {obj->setup();};

		void fromJSON(json j);
		json toJSON();

		Project* project;
};
