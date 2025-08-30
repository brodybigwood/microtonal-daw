#pragma once

#include <string>
#include <dlfcn.h>
#include <iostream>

class PlugLib {
	public:
		PlugLib(std::string path);
		virtual ~PlugLib();

		bool isLoaded() const {return handle != nullptr;};
		void* handle = nullptr;
		std::string path;
		virtual bool load() = 0;
};
