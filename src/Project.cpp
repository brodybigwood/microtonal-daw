#include "Project.h"

Project::Project(std::string filepath) {
        this->filepath = filepath;
        load();

        for(size_t i = 0; i<5; i++) {

            fract pos(0,1);
            Region midiRegion(pos, i);
            regions.push_back(midiRegion);
        }

        


}

Project::~Project() {
save();
}

void Project::load() {
    if(filepath != "") {

    }
}

void Project::save() {
    if(filepath != "") {

    }
}

void Project::createRegion(fract x, int y) {
    Region region(x, y);
    regions.push_back(region);
}