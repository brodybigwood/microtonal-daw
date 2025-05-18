#ifndef EDITORHOSTFRAME_H
#define EDITORHOSTFRAME_H

#include "pluginterfaces/gui/iplugview.h"

class EditorHostFrame : public Steinberg::IPlugFrame {
public:
    // Implement the methods from the IPlugFrame interface:
    Steinberg::tresult PLUGIN_API resizeView (Steinberg::IPlugView* view, Steinberg::ViewRect* newSize) override;
    Steinberg::uint32 PLUGIN_API addRef () override;
    Steinberg::uint32 PLUGIN_API release () override;
    Steinberg::tresult PLUGIN_API queryInterface (const Steinberg::TUID _iid, void** obj) override;

private:
    Steinberg::uint32 refCount = 0; // For managing the object's lifetime
};

#endif // EDITORHOSTFRAME_H