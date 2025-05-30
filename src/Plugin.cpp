#include "Plugin.h"
#include <cmath>
#include <cstddef>
#include <iostream>
#include <dlfcn.h>


#include "EditorHostFrame.h"

class EditorHostFrame;

Plugin::Plugin(const char* filepath) : filepath(filepath), pluginLibraryHandle(nullptr), pluginFactory(nullptr) {
    loadPluginLibrary();
    if (pluginFactory) {
        fetchPluginFactoryInfo();
        if (!getID()) {
            std::cerr << "couldnt get id of plugin" << std::endl;
        }
        instantiatePlugin();
    }
}

Plugin::~Plugin() {
    if (pluginLibraryHandle) {
        dlclose(pluginLibraryHandle);
        std::cout << "Plugin library unloaded." << std::endl;
    }
}

void Plugin::loadPluginLibrary() {
    pluginLibraryHandle = dlopen(filepath, RTLD_NOW);
    if (!pluginLibraryHandle) {
        std::cerr << "Error loading plugin: " << dlerror() << std::endl;
        return;
    }
    std::cout << "Plugin library loaded successfully." << std::endl;

    typedef Steinberg::FUnknown* (*GetPluginFactoryProc)();
    GetPluginFactoryProc getPluginFactory = (GetPluginFactoryProc)dlsym(pluginLibraryHandle, "GetPluginFactory");

    if (!getPluginFactory) {
        std::cerr << "Error finding GetPluginFactory: " << dlerror() << std::endl;
        dlclose(pluginLibraryHandle);
        pluginLibraryHandle = nullptr;
        return;
    }
    std::cout << "GetPluginFactory found." << std::endl;

    Steinberg::FUnknown* factory = getPluginFactory();
    if (!factory) {
        std::cerr << "Error getting IPluginFactory." << std::endl;
        dlclose(pluginLibraryHandle);
        pluginLibraryHandle = nullptr;
        return;
    }
    std::cout << "IPluginFactory obtained." << std::endl;
    pluginFactory = Steinberg::FUnknownPtr<Steinberg::IPluginFactory>(factory).get();
}

void Plugin::fetchPluginFactoryInfo() {
    if (pluginFactory) {
        Steinberg::PFactoryInfo factoryInfo;
        Steinberg::tresult result = pluginFactory->getFactoryInfo(&factoryInfo);
        if (result == Steinberg::kResultTrue) {
            vendor = factoryInfo.vendor;
            url = factoryInfo.url;
            email = factoryInfo.email;
            flags = factoryInfo.flags;

            std::cout << "--- Plugin Factory Info ---" << std::endl;
            std::cout << "Vendor:  " << vendor << std::endl;
            std::cout << "URL:     " << url << std::endl;
            std::cout << "Email:   " << email << std::endl;
            std::cout << "Flags:   " << flags << std::endl;
            std::cout << "--- End Factory Info ---" << std::endl;
        } else {
            std::cerr << "Error getting Plugin Factory Info." << std::endl;
        }
    } else {
        std::cerr << "Error: Could not get IPluginFactory interface." << std::endl;
    }
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
    int matchIndex = -1; 

    for (Steinberg::int32 i = 0; pluginFactory->getClassInfo(i, &classInfo) == Steinberg::kResultTrue; ++i) {
        bool hasMatch = false;
        for (size_t j = 0; j < numMainComponentCategories; ++j) {
            if (strcmp(classInfo.category, mainComponentCategories[j]) == 0) {
                hasMatch = true;
                if (static_cast<int>(j) < smallestMatch) {
                    smallestMatch = static_cast<int>(j);
                    matchIndex = i;
                }
                std::cout << "Found potential main component Class ID." << std::endl;
                break;
            }
        }
    }

    if (matchIndex != -1) {
        pluginFactory->getClassInfo(matchIndex, &classInfo);
        std::cout << "Class category: " << classInfo.category << std::endl;
        std::cout << "Class CID: " << classInfo.cid << std::endl;
        std::memcpy(componentCID, classInfo.cid, sizeof(Steinberg::TUID));
        return true;
    } else {
        std::cerr << "Could not find main component Class ID." << std::endl;
        return false;
    }
}

void Plugin::process(float* thrubuffer, int bufferSize) {
    for (unsigned int i = 0; i < bufferSize; ++i) {
        float sample = 0.5f * sin(2.0 * M_PI * 440.0 * bufferSize / 1000.0f * i); 
        thrubuffer[i] = sample;
    }
}

bool Plugin::instantiatePlugin() {
    Steinberg::FUnknown* componentUnknown = nullptr;
    Steinberg::tresult result = pluginFactory->createInstance(
        componentCID, Steinberg::Vst::IComponent::iid, (void**)&componentUnknown
    );

    if (result != Steinberg::kResultTrue || !componentUnknown) {
        std::cout << "Error creating main component instance." << std::endl;
        return false;
    }

    component = Steinberg::FUnknownPtr<Steinberg::Vst::IComponent>(componentUnknown);
    std::cout << "Successfully created main component instance." << std::endl;

    audioProcessor = Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor>(componentUnknown);
    if (audioProcessor) {
        std::cout << "Component also implements IAudioProcessor." << std::endl;
    }

    eventList = Steinberg::FUnknownPtr<Steinberg::Vst::IEventList>(componentUnknown);
    if (eventList) {
        std::cout << "Component also implements IEventList." << std::endl;
    }

    parameterChanges = Steinberg::FUnknownPtr<Steinberg::Vst::IParameterChanges>(componentUnknown);
    if (parameterChanges) {
        std::cout << "Component also implements IParameterChanges." << std::endl;
    }

    if (component->getControllerClassId(componentCID) == Steinberg::kResultTrue) {
        return createEditControllerAndPlugView();
    } else {
        std::cout << "Failed to create controller instance." << std::endl;
        return false;
    }
}

bool Plugin::createEditControllerAndPlugView() {
    Steinberg::FUnknown* controllerUnknown = nullptr;
    Steinberg::tresult controllerResult = pluginFactory->createInstance(
        componentCID, Steinberg::Vst::IEditController::iid, (void**)&controllerUnknown
    );

    if (controllerResult != Steinberg::kResultTrue || !controllerUnknown) {
        std::cerr << "Error creating Edit Controller instance." << std::endl;
        return false;
    }

    Steinberg::FUnknownPtr<Steinberg::Vst::IEditController> editController(controllerUnknown);
    if (editController) {
        if (component->initialize(nullptr) != Steinberg::kResultTrue) {
            std::cerr << "Error initializing component." << std::endl;
            return false;
        }

        if (audioProcessor) {
            Steinberg::Vst::ProcessSetup processSetup;
            processSetup.sampleRate = 44100.0;
            processSetup.maxSamplesPerBlock = 256;
            processSetup.processMode = Steinberg::Vst::kOffline; 
            if (audioProcessor->setupProcessing(processSetup) != Steinberg::kResultTrue) {
                std::cerr << "Error setting up processing." << std::endl;
                return false;
            }
        }

        if (component->setActive(true) != Steinberg::kResultTrue) {
            std::cerr << "Error activating component." << std::endl;
            return false;
        }

        EditorHostFrame* hostFrame = new EditorHostFrame(); 
        Steinberg::IPtr<Steinberg::IPlugFrame> plugFrame(hostFrame);

        
        Steinberg::IPlugView* plugView = editController->createView(Steinberg::Vst::ViewType::kEditor);

        if (plugView) { //not working
            std::cout << "Successfully created the IPlugView." << std::endl;

            return true;
        } else {
            std::cerr << "Error: createView() returned a null pointer." << std::endl;
            return false;
        }
    } else {
        std::cerr << "Error: Could not get IEditController interface." << std::endl;
        return false;
    }
}
