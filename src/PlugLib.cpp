#include "PlugLib.h"

PlugLib::PlugLib(std::string path) : path(std::move(path)) {

}

PlugLib::~PlugLib() {
	if(handle) {
		dlclose(handle);
		handle = nullptr;
	}
}
