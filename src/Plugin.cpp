#include "Plugin.h"
#include <cmath>
#include <cstddef>
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

    if(!getID()) {
        std::cerr << "couldnt get id of plugin";
    }
    instantiate();
    
}

Plugin::~Plugin() {

}
bool Plugin::getID() {

    const char* mainComponentCategories[] = {
        "Audio Module Class",
        "ComponentClass",
        "Component Controller Class",
        "AudioEffectClass",
        "InstrumentClass",

    };

    size_t numMainComponentCategories = sizeof(mainComponentCategories) / sizeof(mainComponentCategories[0]);



    Steinberg::PClassInfo classInfo;

    int smallestMatch = numMainComponentCategories;
    int matchIndex;

    for (Steinberg::int32 i = 0; pluginFactory->getClassInfo(i, &classInfo) == Steinberg::kResultTrue; ++i) {

        bool hasMatch = false;



        for(size_t j = 0; j<numMainComponentCategories; j++) {
            if(strcmp(classInfo.category, mainComponentCategories[j]) == 0) {
                hasMatch = true;
                if(j <smallestMatch){
                    smallestMatch = j;
                    matchIndex = i;
                }

                std::cout << "Found potential main component Class ID." << std::endl;
                break;
            }
        }

    }
    pluginFactory->getClassInfo(matchIndex, &classInfo);
    std::cout << "Class category: "<< classInfo.category << std::endl;
    std::cout << "Class CID: " << classInfo.cid << std::endl;
    std::memcpy(componentCID, classInfo.cid, sizeof(Steinberg::TUID));


    if(componentCID == NULL) {
        return false;
    }

    return true;
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

bool Plugin::instantiate() {


    Steinberg::FUnknown* componentUnknown = nullptr;
    Steinberg::tresult result = pluginFactory->createInstance(
        componentCID, Steinberg::Vst::IComponent::iid, (void**)&componentUnknown
    );
    if (result == Steinberg::kResultTrue && componentUnknown) {
        component = Steinberg::FUnknownPtr<Steinberg::Vst::IComponent>(componentUnknown);
        if (component) {
            std::cout << "Successfully created main component instance." << std::endl;

/*
            className = Steinberg::FUnknownPtr<Steinberg::Vst::class>(componentUnknown);
            if (className) {
                std::cout << "Component also implements class." << std::endl;
            }
*/



            audioProcessor = Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor>(componentUnknown);
            if (audioProcessor) {
                std::cout << "Component also implements IAudioProcessor." << std::endl;
                // Proceed with audio processing setup
            }

            eventList = Steinberg::FUnknownPtr<Steinberg::Vst::IEventList>(componentUnknown);
            if (eventList) {
                std::cout << "Component also implements IEventList." << std::endl;
            }

            parameterChanges = Steinberg::FUnknownPtr<Steinberg::Vst::IParameterChanges>(componentUnknown);
            if (parameterChanges) {
                std::cout << "Component also implements IParameterChanges." << std::endl;
                // Now you can use the parameterChanges interface to interact with parameters
            }

        } else {
            std::cout << "Failed to create main component instance." << std::endl;   
            return false;    
        }
    } else {
        std::cout << "result was false" << std::endl;
        return false;
    }

    if (component->getControllerClassId(componentCID) == Steinberg::kResultTrue) {
        Steinberg::FUnknown* controllerUnknown = nullptr;
        Steinberg::tresult controllerResult = pluginFactory->createInstance(
            componentCID, Steinberg::Vst::IEditController::iid, (void**)&controllerUnknown
        );
        if (controllerResult == Steinberg::kResultTrue && controllerUnknown) {
            Steinberg::FUnknownPtr<Steinberg::Vst::IEditController> editController(controllerUnknown);
            if (editController) {
                // Proceed to create the view
                Steinberg::IPlugView* plugView = editController->createView(Steinberg::Vst::ViewType::kEditor);
                if (plugView) {
                    std::cout << "Successfully created the IPlugView." << std::endl;
                    // Now you can use the plugView pointer to get the GUI handle, etc.
                    // Remember to release the IPlugView interface when you are done with it:
                    // plugView->release();
                } else {
                    std::cerr << "Error: createView() returned a null pointer." << std::endl;
                }
            } else {
                std::cerr << "Error: Could not get IEditController interface." << std::endl;
                return false; // Or handle the error appropriately
            }
            controllerUnknown->release();
        } else {
            std::cerr << "Error creating Edit Controller instance." << std::endl;
        }

    } else {
        std::cout << "Failed to create controller instance." << std::endl;   
        return false;    
    }


    return true;
}