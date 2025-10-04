#include "Wavnode.h"

Wavnode::Wavnode() {}

Wavnode::~Wavnode() {}

Wavnode* Wavnode::get() {
    static Wavnode w;
    return &w;
};

void Wavnode::toggle() {};
void Wavnode::setup() {};
void Wavnode::process() {
};
void Wavnode::serialize(std::filesystem::path folder) {};
void Wavnode::deSerialize(std::filesystem::path folder) {};
