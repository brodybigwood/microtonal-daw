#include "patcher.h"
#include "NodeManager.h"
#include "NodeEditor.h"

PatcherNode::PatcherNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Patcher) {
    mainManager = new NodeManager(project);

    auto output0 = new Connection;
    output0->type = DataType::Waveform;
    output0->dir = Direction::output;
    outputs.addConnection(output0);
}

PatcherNode::~PatcherNode() {

}

void PatcherNode::process() {

}

void PatcherNode::renderPresent() {
    if (detached) {
        if (mainEditor) mainEditor->renderPresent();
    }
}

void PatcherNode::renderContent(SDL_Renderer*) {

    if (mainEditor) {
        mainEditor->tick();
    } else {
        if (!vCount) {
            vCount = 4;
            vx = new float[vCount];
            vy = new float[vCount];
    
            vx[0] = 0;
            vx[1] = TEX_W;
            vx[2] = TEX_W-300;
            vx[3] = 300;
    
            vy[0] = 0;
            vy[1] = 0;
            vy[2] = TEX_H;
            vy[3] = TEX_H;
        }
    
        filledPolygonRGBA(renderer, vx, vy, vCount, 50, 50, 50, 255);
        aapolygonRGBA(renderer, vx, vy, vCount, 0, 0, 0, 255);

        renderParams(renderer);
    }
}

void PatcherNode::attachFinal() {
    mainManager->resetNE();
    if (mainEditor) delete mainEditor;
    mainEditor = nullptr;
}

void PatcherNode::detachFinal() {
    mainEditor = new NodeEditor;
    mainManager->setNE(mainEditor);

    mainEditor->window = window;
    mainEditor->renderer = renderer;
    mainEditor->retach();
}

bool PatcherNode::handleCustomInput(SDL_Event& e) {
    if (mainEditor) mainEditor->handleWindowInput(e);
    return false;
}

void PatcherNode::clearCustomTextures() {
    if (neTex) SDL_DestroyTexture(neTex);
    neTex = nullptr;
}
