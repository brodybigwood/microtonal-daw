#pragma once

#include "pluginterfaces/gui/iplugview.h"
#include <memory>

class EditorWindowHost {
public:
    virtual ~EditorWindowHost() = default;

    virtual void* getNativeWindowHandle() = 0;
    virtual const char* getPlatformType() = 0;

    virtual bool tick() = 0;

    virtual void resize(int w, int h) = 0;

    bool windowOpen = 0;

    const char* name;

    void setName(const char* name) {
        this->name = name;
    }

    static std::unique_ptr<EditorWindowHost> create();
};
