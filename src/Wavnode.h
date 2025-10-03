#include "NodeInstance.h"
#include "EventManager.h"
#include <filesystem>

class Wavnode {
	public:
		Wavnode(char* path);
		~Wavnode();

		void toggle();
		void setup();
		void process(audioData, EventList*);
		void showWindow();
		bool editorTick();

		void serialize(std::filesystem::path);
		void deSerialize(std::filesystem::path);

        NodeInstance* mainNode;

        typedef NodeInstance* (*nodeGetter)();
		nodeGetter getNodeInstance = nullptr;

        void* handle;
        char* path;

};
