#include "PlugLib.h"
#include <dlfcn.h>
#include <iostream>
#include <cstring>

PlugLib::PlugLib(std::string path) : path(std::move(path)) {

    //this->path = "/home/brody/Downloads/TAL-NoiseMaker_64_linux/TAL-NoiseMaker/TAL-NoiseMaker.vst3/Contents/x86_64-linux/TAL-NoiseMaker.so";
    //this->path = "/home/brody/Downloads/surge-xt-linux-x86_64-1.3.4/lib/vst3/Surge XT.vst3/Contents/x86_64-linux/Surge XT.so";

    //this->path = "/home/brody/Downloads/VitalInstaller(3)/VitalBinaries/Vital.vst3/Contents/x86_64-linux/Vital.so";
    //this->path = "/home/brody/Downloads/TAL-BassLine-101_64_linux/TAL-BassLine-101/TAL-BassLine-101.vst3/Contents/x86_64-linux/TAL-BassLine-101.so";

    load();
}

bool PlugLib::load() {
    handle = dlopen(path.c_str(), RTLD_NOW);
    if (!handle) {
        std::cerr << "dlopen failed: " << dlerror() << std::endl;
        return false;
    }

    using GetFactoryProc = Steinberg::FUnknown* (*)();
    auto getFactory = reinterpret_cast<GetFactoryProc>(dlsym(handle, "GetPluginFactory"));
    if (!getFactory) {
        std::cerr << "dlsym failed: " << dlerror() << std::endl;
        return false;
    }

    Steinberg::FUnknown* raw = getFactory();
    if (!raw) {
        std::cerr << "GetPluginFactory returned null.\n";
        return false;
    }

    factory = Steinberg::FUnknownPtr<Steinberg::IPluginFactory>(raw);
    if (!factory) {
        std::cerr << "Factory cast failed.\n";
        return false;
    }

    factoryWrapper = std::make_unique<VST3::Hosting::PluginFactory>(factory);
    return fetchClassIDs();
}

bool PlugLib::fetchClassIDs() {
    bool foundComponent = false, foundController = false;

    for (Steinberg::int32 i = 0; i < factory->countClasses(); ++i) {
        Steinberg::PClassInfo info{};
        if (factory->getClassInfo(i, &info) != Steinberg::kResultTrue)
            continue;

        if (!foundComponent && std::strcmp(info.category, "Audio Module Class") == 0) {
            std::memcpy(componentCID, info.cid, sizeof(Steinberg::TUID));
            VST3::Hosting::ClassInfo classInfo(info);
            plugProvider = std::make_unique<Steinberg::Vst::PlugProvider>(*factoryWrapper, classInfo, true);

            foundComponent = plugProvider->initialize();
        }

        if (!foundController && std::strcmp(info.category, "Component Controller Class") == 0) {
            std::memcpy(controllerCID, info.cid, sizeof(Steinberg::TUID));
            foundController = true;
        }

        if (foundComponent && foundController) break;
    }

    if (!foundComponent || !foundController) {
        std::cerr << "Failed to locate component/controller CIDs.\n";
        return false;
    }

    return true;
}

PlugLib::~PlugLib() {
    plugProvider.reset();

    factoryWrapper.reset();
    factory = nullptr;

    if (handle) {
        dlclose(handle);
        handle = nullptr;
    }
}

