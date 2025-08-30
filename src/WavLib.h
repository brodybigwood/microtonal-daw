#include "PlugLib.h"

class WavLib : public PlugLib {
	public:
		WavLib(std::string path);
		~WavLib() {};

		bool load() override;
};
