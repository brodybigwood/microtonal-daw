#include "Plugin.h"
#include <cmath>
#include <iostream>

#include <dlfcn.h>



Plugin::Plugin(const char* filepath) {

    this->filepath = filepath;

    pluginLibraryHandle = dlopen(filepath, RTLD_NOW);

    if (pluginLibraryHandle == nullptr) {
        std::cerr << "Error loading plugin: " << dlerror() << std::endl;
        // Handle the error (e.g., return or throw an exception)
        return;
    }
    
    std::cout << "Plugin library loaded successfully." << std::endl;

    typedef Steinberg::FUnknown* (*GetPluginFactoryProc) ();

    GetPluginFactoryProc getPluginFactory = (GetPluginFactoryProc) dlsym(pluginLibraryHandle, "GetPluginFactory");

    if (getPluginFactory == nullptr) {
        std::cerr << "Error finding GetPluginFactory: " << dlerror() << std::endl;
        dlclose(pluginLibraryHandle); // Close the library if we can't proceed
        return; // Or handle the error appropriately
    }

    std::cout << "GetPluginFactory found." << std::endl;

    // Now you can call getPluginFactory() to get the IPluginFactory interface.
    Steinberg::FUnknown* factory = getPluginFactory();
    if (factory == nullptr) {
        std::cerr << "Error getting IPluginFactory." << std::endl;
        dlclose(pluginLibraryHandle);
        return;
    }

    std::cout << "IPluginFactory obtained." << std::endl;

    pluginFactory = Steinberg::FUnknownPtr<Steinberg::IPluginFactory>(factory).get();

    if (pluginFactory) {
        Steinberg::PFactoryInfo factoryInfo; // Declare factoryInfo

        Steinberg::tresult result = pluginFactory->getFactoryInfo(&factoryInfo); // Call getFactoryInfo

        if (result == Steinberg::kResultTrue) {

            vendor = factoryInfo.vendor;
            url = factoryInfo.url;
            email = factoryInfo.email;
            flags = factoryInfo.flags;

    
            std::cout << "--- Plugin Factory Info ---" << std::endl;
            std::cout << "Vendor:  " << factoryInfo.vendor << std::endl;
            std::cout << "URL:     " << factoryInfo.url << std::endl;
            std::cout << "Email:   " << factoryInfo.email << std::endl;
            std::cout << "Flags:   " << factoryInfo.flags << std::endl;
            std::cout << "--- End Factory Info ---" << std::endl;
        } else {
            std::cerr << "Error getting Plugin Factory Info." << std::endl;
        }
    } else {
        std::cerr << "Error: Could not get IPluginFactory interface." << std::endl;
    }
    
}

Plugin::~Plugin() {

}
void Plugin::load() {

    
}

void Plugin::process(
    float* thrubuffer,
    int bufferSize
) {

    for (unsigned int i = 0; i < bufferSize; ++i) {
        float sample = 0.5f * sin(2.0 * M_PI * 440.0 * bufferSize/1000 * i);  // 440Hz sine wave
        thrubuffer[i] = sample;
    }
}