#include "VstLib.h"
#include <iostream>
#include <cstring>

VstLib::VstLib(std::string filepath) : PlugLib(filepath) {
	format = PlugFormat::VST;
}

bool VstLib::load() {
    handle = dlopen(path.c_str(), RTLD_NOW);
    if(!handle) {
	std::cerr << "dlopen failed: " << dlerror() << std::endl;
    } else {
	std::cout << "dlopen succeeded: handle=" << handle << std::endl;
    }
    dlerror(); // clear errors

    using GetFactoryProc = Steinberg::FUnknown* (*)();
    auto getFactory = reinterpret_cast<GetFactoryProc>(dlsym(handle, "GetPluginFactory"));
    if (!getFactory) {
        std::cerr << "dlsym failed getfactory: " << dlerror() << std::endl;
	std::exit(0);
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

bool VstLib::fetchClassIDs() {
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

VstLib::~VstLib() {
    plugProvider.reset();

    factoryWrapper.reset();
    factory = nullptr;

}

