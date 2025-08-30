#include "PlugLib.h"

PlugLib::PlugLib(std::string path) : path(std::move(path)) {
	handle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
	if(!handle) {
		std::cerr << "dlopen failed: " << dlerror() << std::endl;
	} else {
		std::cout << "dlopen succeeded: handle=" << handle << std::endl;
	}
}

PlugLib::~PlugLib() {
	if(handle) {
		dlclose(handle);
		handle = nullptr;
	}
}
