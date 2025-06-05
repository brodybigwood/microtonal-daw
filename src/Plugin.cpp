#include "Plugin.h"
#include <cmath>
#include <cstddef>
#include <iostream>
#include <dlfcn.h>



Plugin::Plugin(const char* filepath) : filepath(filepath), pluginLibraryHandle(nullptr), pluginFactory(nullptr) {
    //this->filepath = "/home/brody/Downloads/TAL-NoiseMaker_64_linux/TAL-NoiseMaker/TAL-NoiseMaker.vst3/Contents/x86_64-linux/TAL-NoiseMaker.so";
    //this->filepath = "/home/brody/Downloads/surge-xt-linux-x86_64-1.3.4/lib/vst3/Surge XT.vst3/Contents/x86_64-linux/Surge XT.so";
    loadPluginLibrary();
// //
    if (pluginFactory) {
        fetchPluginFactoryInfo();

        if (!getID()) {
            std::cerr << "couldnt get id of plugin" << std::endl;
        }

        instantiatePlugin();
    }
}

Plugin::~Plugin() {
    if (view){
        view->release();
    }
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

    Steinberg::FUnknown* factoryRaw = getPluginFactory();
    if (!factoryRaw) {
        std::cerr << "Error getting IPluginFactory." << std::endl;
        dlclose(pluginLibraryHandle);
        pluginLibraryHandle = nullptr;
        return;
    }
    std::cout << "IPluginFactory obtained." << std::endl;
    pluginFactory = Steinberg::FUnknownPtr<Steinberg::IPluginFactory>(factoryRaw);
    factoryWrapper = std::make_unique<VST3::Hosting::PluginFactory>(pluginFactory);
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

            name = vendor.c_str();


            std::cout << "pluginFactory raw ptr: " << pluginFactory << std::endl;
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
    bool foundComponent = false;
    bool foundController = false;

    if (!pluginFactory) {
        std::cerr << "pluginFactory is null!" << std::endl;
        return false;
    }

    for (Steinberg::int32 i = 0; i<2; ++i) {
        Steinberg::PClassInfo pClassInfo{};

        auto res = pluginFactory->getClassInfo(i, &pClassInfo);
        if (res != Steinberg::kResultTrue)
            break;
        std::cout << "Class[" << i << "] category: " << pClassInfo.category << std::endl;
        if (!foundComponent && strcmp(pClassInfo.category, "Audio Module Class") == 0) {
            std::memcpy(componentCID, pClassInfo.cid, sizeof(Steinberg::TUID));
            foundComponent = true;

            VST3::Hosting::ClassInfo classInfo(pClassInfo);

            plugProvider = std::make_unique<Steinberg::Vst::PlugProvider>(*factoryWrapper, classInfo, true);


            if (!plugProvider->initialize())
            {
                std::cerr << "Failed to initialize plug provider." << std::endl;
            } else {
                std::cout << "initialized plug provider" << std::endl;
            }
        }
        if (!foundController && strcmp(pClassInfo.category, "Component Controller Class") == 0) {
            std::memcpy(controllerCID, pClassInfo.cid, sizeof(Steinberg::TUID));
            foundController = true;
        }
    }

    if (!foundComponent)
        std::cerr << "Could not find component Class ID." << std::endl;
    if (!foundController)
        std::cerr << "Could not find controller Class ID." << std::endl;


    return foundComponent && foundController;
}

bool Plugin::editorTick() {
    if(editorHost->windowOpen) {
        editorHost->tick();
        return true;
    }
    return false;
}


void Plugin::process(float* thrubuffer, int bufferSize) {

    for (unsigned int i = 0; i < bufferSize; ++i) {
        float sample = 0.5f * sin(2.0 * M_PI * 440.0 * bufferSize / 1000.0f * i); 
        thrubuffer[i] = sample;
    }
}

bool Plugin::instantiatePlugin() {
    Steinberg::FUnknown* componentUnknown = nullptr;
    auto result = pluginFactory->createInstance(
        componentCID, Steinberg::Vst::IComponent::iid, (void**)&componentUnknown
    );


    if (result != Steinberg::kResultTrue || !componentUnknown) {
        std::cout << "Error creating main component instance." << std::endl;
        return false;
    }

    component = Steinberg::FUnknownPtr<Steinberg::Vst::IComponent>(componentUnknown);

    if(createEditControllerAndPlugView(controllerCID)) {

        showWindow();

        return true;
    }


}

bool Plugin::createEditControllerAndPlugView(const Steinberg::TUID controllerCID) {
    Steinberg::FUnknown* controllerUnknown = nullptr;
    auto controllerResult = pluginFactory->createInstance(controllerCID, Steinberg::Vst::IEditController::iid, (void**)&controllerUnknown);

    if (controllerResult != Steinberg::kResultTrue || !controllerUnknown) {
        std::cerr << "Error creating Edit Controller instance." << std::endl;
        return false;
    }

    editController = Steinberg::FUnknownPtr<Steinberg::Vst::IEditController>(controllerUnknown);
    if (!editController) {
        std::cerr << "Error: Could not get IEditController interface." << std::endl;
        return false;
    }

    if (editController->initialize(nullptr) != Steinberg::kResultTrue) {
        std::cerr << "Error initializing edit controller." << std::endl;
        return false;
    }

    makeConnection();



    hostFrame = new EditorHostFrame();
    // Cast to IComponentHandler, the expected interface
    componentHandler = hostFrame;
    if (editController->setComponentHandler(componentHandler) != Steinberg::kResultTrue) {
        std::cerr << "Failed to set component handler." << std::endl;
    }


    view = editController->createView(Steinberg::Vst::ViewType::kEditor);

    if (!view) {
        std::cerr << "Failed to create editor view." << std::endl;
        return false;
    }

    std::cout << "Editor view created successfully." << std::endl;


    return true;
}

void Plugin::showWindow() {
    editorHost = EditorWindowHost::create();
    if (!editorHost) {
        std::cerr << "Failed to create editor host window." << std::endl;
        return;
    }

    auto nativeHandle = editorHost->getNativeWindowHandle();
    auto platformType = editorHost->getPlatformType();

    if (view->isPlatformTypeSupported(platformType) == Steinberg::kResultTrue) {

        std::cout<<"test"<<std::endl;


        auto result = view->attached(nativeHandle, platformType);

                std::cout << "Attach result: " << result << std::endl;
        editorHost->setName(name);


        if (result == Steinberg::kResultTrue) {
            view->getSize(&viewRect);
            std::cout << "Attached view. Size: "
            << viewRect.right - viewRect.left << "x"
            << viewRect.bottom - viewRect.top << std::endl;

            editorHost->resize(viewRect.right - viewRect.left, viewRect.bottom - viewRect.top);

             view->onSize(&viewRect);


        } else {
            std::cout << "nativeHandle pointer: " << nativeHandle << std::endl;
            std::cerr << "Failed to attach view" << std::endl;
            exit(0);
        }
    } else {
        std::cerr << "Platform type not supported: " << platformType << std::endl;
    }


}

void Plugin::makeConnection() {
    componentConnection = Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint>(component);
    controllerConnection = Steinberg::FUnknownPtr<Steinberg::Vst::IConnectionPoint>(editController);

    if (componentConnection && controllerConnection) {
        if (componentConnection->connect(controllerConnection) != Steinberg::kResultTrue) {
            std::cerr << "Failed to connect component to controller." << std::endl;
        }
        if (controllerConnection->connect(componentConnection) != Steinberg::kResultTrue) {
            std::cerr << "Failed to connect controller to component." << std::endl;
        }
    }
}
