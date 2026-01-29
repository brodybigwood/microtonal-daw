#include "TreeEntry.h"

bool TreeEntry::isParent() {
    return !click;
}

TreeEntry::~TreeEntry() {
    if (labelTexture) {
        SDL_DestroyTexture(labelTexture);
    }
}

void TreeEntry::addChild(std::shared_ptr<TreeEntry> child) {
    children.push_back(child);
}

std::shared_ptr<TreeEntry> uTreeEntry() {
    return std::make_shared<TreeEntry>();
}
