#include "SDL_Events.h"

bool isEventForWindow(const SDL_Event& e, uint32_t windowID) {
    switch (e.type) {
        case SDL_EVENT_MOUSE_MOTION:
            return e.motion.windowID == windowID;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            return e.button.windowID == windowID;
        case SDL_EVENT_MOUSE_WHEEL:
            return e.wheel.windowID == windowID;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            return e.key.windowID == windowID;
        case SDL_EVENT_TEXT_INPUT:
        case SDL_EVENT_TEXT_EDITING:
            return e.edit.windowID == windowID;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            return e.window.windowID == windowID;
        case SDL_EVENT_DROP_BEGIN:
        case SDL_EVENT_DROP_COMPLETE:
        case SDL_EVENT_DROP_FILE:
        case SDL_EVENT_DROP_POSITION:
            return e.drop.windowID == windowID;
        default:
            return false;
    }
}


uint32_t getEventWindowID(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_MOUSE_MOTION:
            return e.motion.windowID;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            return e.button.windowID;
        case SDL_EVENT_MOUSE_WHEEL:
            return e.wheel.windowID;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            return e.key.windowID;
        case SDL_EVENT_TEXT_INPUT:
        case SDL_EVENT_TEXT_EDITING:
            return e.edit.windowID;
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            return e.window.windowID;
        case SDL_EVENT_DROP_BEGIN:
        case SDL_EVENT_DROP_COMPLETE:
        case SDL_EVENT_DROP_FILE:
        case SDL_EVENT_DROP_POSITION:
            return e.drop.windowID;
        default:
            return 0; // unknown or global event
    }
}

bool MouseOn(SDL_FRect* rect) {

    float mouseX;
    float mouseY;
    SDL_GetMouseState(&mouseX, &mouseY);

    return 
        mouseX >= rect->x &&
        mouseX < rect->x + rect->w &&
        mouseY >= rect->y &&
        mouseY < rect->y + rect->h
    ;
}

bool inPolygon(float* vx, float* vy, size_t vCount, float mouseX, float mouseY) {
    bool inside = false;

    for (int i = 0, j = vCount - 1; i < vCount; j = i++)
    {
        float xi = vx[i], yi = vy[i];
        float xj = vx[j], yj = vy[j];

        bool intersect =
            ((yi > mouseY) != (yj > mouseY)) &&
            (mouseX < (xj - xi) * (mouseY - yi) / (yj - yi) + xi);

        if (intersect)
            inside = !inside;
    }

    return inside;
}
