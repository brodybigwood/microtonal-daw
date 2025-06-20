#pragma once


#include <string>
#include <memory>
#include <cstring> // for memcpy
#include <dlfcn.h>

#include "public.sdk/source/vst/hosting/plugprovider.h"
#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"



class PlugLib {
public:
    PlugLib(std::string path);
    ~PlugLib() {}

    bool isLoaded() const { return handle != nullptr; }
    Steinberg::IPluginFactory* getFactory() const { return factory; }
    const Steinberg::TUID& getComponentCID() const { return componentCID; }
    const Steinberg::TUID& getControllerCID() const { return controllerCID; }

    std::unique_ptr<VST3::Hosting::PluginFactory>& getFactoryWrapper() { return factoryWrapper; }

private:
    void* handle = nullptr;
    Steinberg::FUnknownPtr<Steinberg::IPluginFactory> factory;
    std::unique_ptr<VST3::Hosting::PluginFactory> factoryWrapper;

    Steinberg::TUID componentCID{};
    Steinberg::TUID controllerCID{};

    std::string path;

    bool load();
    bool fetchClassIDs();
};
