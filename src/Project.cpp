#include "Project.h"

Project::Project(std::string filepath) {
        this->filepath = filepath;
        load();
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