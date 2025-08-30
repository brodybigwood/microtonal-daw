#include "WavLib.h"

WavLib::WavLib(std::string path) : PlugLib(path) {
	format = PlugFormat::Wavnode;
}

bool WavLib::load() {
	handle = dlopen(path.c_str(), RTLD_NOW);
	if(!handle) {
		std::cerr << "dlopen failed: " << dlerror() << std::endl;
	} else {
		std::cout << "dlopen succeeded: handle=" << handle << std::endl;
	}
	dlerror();
	return true;
}
