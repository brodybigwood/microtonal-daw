#include "visualizer.h"
#include <algorithm>
#include <cmath>
#include <cstring>

VisualizerNode::VisualizerNode(uint16_t id, NodeManager* nm) : Node(id, nm, NodeType::Visualizer) {
    in = new Connection;
    in->type = DataType::Waveform;
    in->dir = Direction::input;
    inputs.addConnection(in);

    out = new Connection;
    out->type = DataType::Waveform;
    out->dir = Direction::output;
    outputs.addConnection(out);

    vCount = 4;
    vx = new float[vCount];
    vy = new float[vCount];
    vx[0] = 0.0f;    vy[0] = 0.0f;
    vx[1] = TEX_W;   vy[1] = 0.0f;
    vx[2] = TEX_W;   vy[2] = TEX_H;
    vx[3] = 0.0f;    vy[3] = TEX_H;

    levelHistory.assign(TEX_W, 0.0f);
}

void VisualizerNode::setup() {
    if (levelHistory.size() != TEX_W) {
        levelHistory.assign(TEX_W, 0.0f);
        writePos = 0;
    }
}

void VisualizerNode::process() {
    if (!out || !out->buffer || bufferSize <= 0) return;

    if (!in || !in->is_connected || !in->buffer) {
        std::memset(out->buffer, 0, static_cast<size_t>(bufferSize) * sizeof(float));
        envLevel *= 0.96f;
        levelHistory[writePos] = std::clamp(envLevel, 0.0f, 1.0f);
        writePos = (writePos + 1) % levelHistory.size();
        return;
    }

    float peak = 0.0f;
    for (int i = 0; i < bufferSize; ++i) {
        const float s = in->buffer[i];
        out->buffer[i] = s; // passthrough unchanged
        peak = std::max(peak, std::fabs(s));
    }

    if (!project || !project->isPlaying.load()) {
        return; // freeze graph when transport is not running
    }

    // Fast attack / slow release level follower.
    if (peak > envLevel) envLevel = peak;
    else envLevel = envLevel * 0.92f + peak * 0.08f;

    levelHistory[writePos] = std::clamp(envLevel, 0.0f, 1.0f);
    writePos = (writePos + 1) % levelHistory.size();
}

void VisualizerNode::renderContent(SDL_Renderer* renderer) {
    const SDL_FRect graphRect{0.0f, 0.0f, static_cast<float>(TEX_W), static_cast<float>(TEX_H)};
    SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
    SDL_RenderFillRect(renderer, &graphRect);
    SDL_SetRenderDrawColor(renderer, 80, 80, 80, 255);
    SDL_RenderRect(renderer, &graphRect);

    // Horizontal guides.
    SDL_SetRenderDrawColor(renderer, 55, 55, 55, 255);
    for (int i = 1; i <= 3; ++i) {
        const float y = graphRect.y + graphRect.h * (static_cast<float>(i) / 4.0f);
        SDL_RenderLine(renderer, graphRect.x, y, graphRect.x + graphRect.w, y);
    }

    // Scrolling level trace (oldest on left, newest on right).
    SDL_SetRenderDrawColor(renderer, 120, 255, 150, 255);
    const int samples = std::min(static_cast<int>(graphRect.w), static_cast<int>(levelHistory.size()));
    for (int x = 1; x < samples; ++x) {
        const size_t i0 = (writePos + levelHistory.size() - static_cast<size_t>(samples - x + 1)) % levelHistory.size();
        const size_t i1 = (writePos + levelHistory.size() - static_cast<size_t>(samples - x)) % levelHistory.size();
        const float v0 = std::clamp(levelHistory[i0], 0.0f, 1.0f);
        const float v1 = std::clamp(levelHistory[i1], 0.0f, 1.0f);

        const float px0 = graphRect.x + static_cast<float>(x - 1);
        const float py0 = graphRect.y + graphRect.h * (1.0f - v0);
        const float px1 = graphRect.x + static_cast<float>(x);
        const float py1 = graphRect.y + graphRect.h * (1.0f - v1);
        SDL_RenderLine(renderer, px0, py0, px1, py1);
    }
}
