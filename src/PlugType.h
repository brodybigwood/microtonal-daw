#pragma once
#include "EventList.h"
#include "Processing.h"
#include <filesystem>

class Rack;

class PlugType{
	public: 
		PlugType();
		virtual ~PlugType();
	
		virtual void toggle() = 0;
		virtual void setup() = 0;
		virtual void process(audioData, EventList*) = 0;
		virtual void showWindow() = 0;
		virtual bool editorTick() = 0;
	
		bool windowOpen = false;
		bool processing = false;
	
		char* path;
		const char* name;

		Rack* rack;
	
		virtual void serialize(std::filesystem::path) = 0;
		virtual void deSerialize(std::filesystem::path) = 0;
};
