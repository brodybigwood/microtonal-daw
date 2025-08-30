#include "WavLib.h"

WavLib::WavLib(std::string path) : PlugLib(path) {
	format = PlugFormat::Wavnode;
}

bool WavLib::load() {
	return true;
}
