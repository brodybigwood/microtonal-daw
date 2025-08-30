#include "PlugType.h"
#include "WavLib.h"

class Wavnode : public PlugType {
	public:
		Wavnode(char* path, PlugLib* library);
		~Wavnode();

		WavLib* lib;

		void toggle() override;
		void setup() override;
		void process(audioData, EventList*) override;
		void showWindow() override;
		bool editorTick() override;

		void serialize(std::filesystem::path) override;
		void deSerialize(std::filesystem::path) override;
};
