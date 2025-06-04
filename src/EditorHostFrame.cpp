#include "EditorHostFrame.h"
#include <iostream>
#include "pluginterfaces/base/funknown.h" // For FUnknownPrivate::iidEqual

// Implementation of resizeView
Steinberg::tresult PLUGIN_API EditorHostFrame::resizeView (Steinberg::IPlugView* view, Steinberg::ViewRect* newSize) {
    std::cout << "IPlugFrame::resizeView called" << std::endl;
    return Steinberg::kResultTrue; // For now, just acknowledge the call
}

// Implementation of addRef (for reference counting)
Steinberg::uint32 PLUGIN_API EditorHostFrame::addRef () {
    refCount++;
    return refCount;
}

// Implementation of release (for reference counting and object deletion)
Steinberg::uint32 PLUGIN_API EditorHostFrame::release () {
    refCount--;
    if (refCount == 0) {
        delete this;
        return 0;
    }
    return refCount;
}

// Implementation of queryInterface (for accessing different interfaces of the object)
Steinberg::tresult PLUGIN_API EditorHostFrame::queryInterface (const Steinberg::TUID _iid, void** obj) {
    if (Steinberg::FUnknownPrivate::iidEqual (_iid, Steinberg::IPlugFrame::iid) ||
        Steinberg::FUnknownPrivate::iidEqual (_iid, Steinberg::FUnknown::iid)) {
        *obj = static_cast<Steinberg::IPlugFrame*>(this);
    addRef ();
    return Steinberg::kResultTrue;
        }

        if (Steinberg::FUnknownPrivate::iidEqual (_iid, Steinberg::Vst::IComponentHandler::iid)) {
            *obj = static_cast<Steinberg::Vst::IComponentHandler*>(this);
            addRef();
            return Steinberg::kResultTrue;
        }

        *obj = nullptr;
        return Steinberg::kNoInterface;
}

Steinberg::tresult PLUGIN_API EditorHostFrame::beginEdit(Steinberg::Vst::ParamID id) {
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API EditorHostFrame::performEdit(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) {
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API EditorHostFrame::endEdit(Steinberg::Vst::ParamID id) {
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API EditorHostFrame::restartComponent(Steinberg::int32 flags) {
    return Steinberg::kResultOk;
}

