#ifndef EDITORHOSTFRAME_H
#define EDITORHOSTFRAME_H

#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "base/source/fobject.h"

class EditorHostFrame : public Steinberg::FObject, public Steinberg::IPlugFrame, public Steinberg::Vst::IComponentHandler {
public:
    // IPlugFrame
    Steinberg::tresult PLUGIN_API resizeView (Steinberg::IPlugView* view, Steinberg::ViewRect* newSize) override;
    Steinberg::uint32 PLUGIN_API addRef () override;
    Steinberg::uint32 PLUGIN_API release () override;
    Steinberg::tresult PLUGIN_API queryInterface (const Steinberg::TUID _iid, void** obj) override;

    // IComponentHandler

    Steinberg::Vst::IComponentHandler* componentHandler = nullptr;

    Steinberg::tresult PLUGIN_API beginEdit (Steinberg::Vst::ParamID id) override;
    Steinberg::tresult PLUGIN_API performEdit (Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value) override;
    Steinberg::tresult PLUGIN_API endEdit (Steinberg::Vst::ParamID id) override;
    Steinberg::tresult PLUGIN_API restartComponent (Steinberg::int32 flags) override;

private:
    Steinberg::uint32 refCount = 0;
};

#endif // EDITORHOSTFRAME_H
