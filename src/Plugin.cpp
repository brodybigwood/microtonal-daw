#include "Plugin.h"
#include <cmath>
#include <cstddef>
#include <iostream>
#include <dlfcn.h>



Plugin::Plugin(const char* filepath) : filepath(filepath), pluginFactory(nullptr) {

    lib = PluginManager::instance().load(filepath);
    if (!lib) {
        std::cerr << "Failed to load plugin library.\n";
        return;
    }

    pluginFactory = lib->getFactory();
    factoryWrapper = std::make_unique<VST3::Hosting::PluginFactory>(*lib->getFactoryWrapper());

    if (pluginFactory) {
        fetchPluginFactoryInfo();
        instantiatePlugin();
    }

}

Plugin::~Plugin() {
    if (view){
        view->release();
    }
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

bool Plugin::editorTick() {


    if (!windowOpen || !editorHost) return false;

    std::cout << "window open" << std::endl;
    if (!editorHost->tick()) {
        if (view) {
            view->removed();
        }

        editorHost.reset();
        windowOpen = false;
        std::cout << "window closed" << std::endl;
        return false;
    }

    return true;
}



void Plugin::process(float* thrubuffer, int bufferSize) {

    for (unsigned int i = 0; i < bufferSize; ++i) {
        float sample = 0.5f * sin(2.0 * M_PI * 440.0 * bufferSize / 1000.0f * i); 
        thrubuffer[i] = sample;
    }
}

bool Plugin::instantiatePlugin() {

    std::memcpy(componentCID, lib->getComponentCID(), sizeof(Steinberg::TUID));
    std::memcpy(controllerCID, lib->getControllerCID(), sizeof(Steinberg::TUID));



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



    hostFrame = std::make_unique<EditorHostFrame>();
    // Cast to IComponentHandler, the expected interface
    componentHandler = hostFrame.get();

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

    editorHost->view = view;

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

             windowOpen = true;


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
