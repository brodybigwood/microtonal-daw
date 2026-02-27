#include <SDL3/SDL.h>
#include <cstdint>

bool isEventForWindow(const SDL_Event& e, uint32_t windowID);
uint32_t getEventWindowID(const SDL_Event& e);

bool MouseOn(SDL_FRect* rect);
bool inPolygon(float* vx, float* vy, size_t vCount, float mouseX, float mouseY);
