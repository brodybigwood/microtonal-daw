#include <filesystem>

class Wavnode {
	public:
		Wavnode();
		~Wavnode();
        
        static Wavnode* get();
        
		void process();
        void setup();
        void toggle();

		void serialize(std::filesystem::path);
		void deSerialize(std::filesystem::path);
};
