#include "Plugin.h"
#include <cmath>
#include <cstddef>
#include <iostream>
#include <dlfcn.h>



Plugin::Plugin(const char* filepath) : filepath(filepath), pluginFactory(nullptr) {

    am = AudioManager::instance();

    project = Project::instance();

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

    if (!audioProcessor) return;

    const int totalInputChannels = std::accumulate(
        inputBuses.begin(), inputBuses.end(), 0,
                                                   [](int sum, const auto& b) { return sum + b.numChannels; });


    const int totalOutputChannels = std::accumulate(
        outputBuses.begin(), outputBuses.end(), 0,
                                              [](int sum, const auto& b) { return sum + b.numChannels; });

    std::vector<float> tempBuffer(bufferSize * totalOutputChannels, 0.0f);

    std::vector<float> inputTemp(bufferSize * totalInputChannels, 0.0f);

    int chOffset = 0;
    for (auto& bus : inputBuses) {
        for (int ch = 0; ch < bus.numChannels; ++ch) {
            bus.channelBuffers32[ch] = inputTemp.data() + (chOffset * bufferSize);
            ++chOffset;
        }
    }



    chOffset = 0;
    for (auto& bus : outputBuses) {
        for (int ch = 0; ch < bus.numChannels; ++ch) {
            bus.channelBuffers32[ch] = tempBuffer.data() + (chOffset * bufferSize);
            ++chOffset;
        }
    }

    data.numSamples = bufferSize;

    Steinberg::Vst::ProcessContext context{};
    context.state |= Steinberg::Vst::ProcessContext::kPlaying | Steinberg::Vst::ProcessContext::kProjectTimeMusicValid;

    context.projectTimeSamples = project->sampleTime;
    context.sampleRate = am->sampleRate;

    data.processContext = &context;


    std::cout << "--- ProcessData ---\n";
    std::cout << "numSamples: " << data.numSamples << "\n";
    std::cout << "symbolicSampleSize: " << data.symbolicSampleSize << "\n";
    std::cout << "numInputs: " << data.numInputs << "\n";
    std::cout << "numOutputs: " << data.numOutputs << "\n";
    std::cout << "inputs: " << data.inputs << "\n";
    std::cout << "outputs: " << data.outputs << "\n";
    std::cout << "inputEvents: " << data.inputEvents << "\n";
    std::cout << "processContext: " << data.processContext << "\n";

    for (int i = 0; i < data.numInputs; ++i) {
        std::cout << "Input bus " << i << ": " << inputBuses[i].numChannels << " channels\n";
        for (int ch = 0; ch < inputBuses[i].numChannels; ++ch) {
            std::cout << "  ch[" << ch << "] ptr: " << inputBuses[i].channelBuffers32[ch] << "\n";
        }
    }

    for (int i = 0; i < data.numOutputs; ++i) {
        std::cout << "Output bus " << i << ": " << outputBuses[i].numChannels << " channels\n";
        for (int ch = 0; ch < outputBuses[i].numChannels; ++ch) {
            std::cout << "  ch[" << ch << "] ptr: " << outputBuses[i].channelBuffers32[ch] << "\n";
        }
    }
    std::cout << "--- End ProcessData ---\n";








    audioProcessor->process(data);
    eventList->events.clear();

    for (int s = 0; s < bufferSize; ++s) {
        float mixed = 0.0f;
        for (int ch = 0; ch < totalOutputChannels; ++ch) {
            mixed += tempBuffer[ch * bufferSize + s];
        }
        thrubuffer[s] += mixed;
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

    Steinberg::FUnknown* processorUnknown = nullptr;
    auto res = component->queryInterface(Steinberg::Vst::IAudioProcessor::iid, (void**)&processorUnknown);
    if (res == Steinberg::kResultTrue && processorUnknown) {
        audioProcessor = Steinberg::FUnknownPtr<Steinberg::Vst::IAudioProcessor>(processorUnknown);
        std::cout << "Got audio processor" << std::endl;
    } else {
        std::cerr << "Failed to get IAudioProcessor interface." << std::endl;
        return false;
    }

    if (audioProcessor) {
        Steinberg::Vst::ProcessSetup setup{};

        setup.sampleRate = am->sampleRate;
        setup.maxSamplesPerBlock = am->bufferSize;
        setup.processMode = Steinberg::Vst::kRealtime;
        setup.symbolicSampleSize = Steinberg::Vst::kSample32;

        if (audioProcessor->canProcessSampleSize(setup.symbolicSampleSize) != Steinberg::kResultTrue){
            std::cerr << "sample size invalid ." << std::endl;
    }

        if(component->initialize(nullptr) != Steinberg::kResultTrue) {
            std::cerr << "Error initializing ." << std::endl;
            return false;
        }


        std::vector<Steinberg::Vst::SpeakerArrangement> inputArrangements;
        std::vector<Steinberg::Vst::SpeakerArrangement> outputArrangements;

        Steinberg::int32 numInputs = component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput);
        Steinberg::int32 numOutputs = component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput);

        for(Steinberg::int32 busIndex = 0; busIndex<numInputs; busIndex++) {
            Steinberg::Vst::SpeakerArrangement arr;
            if (audioProcessor->getBusArrangement(Steinberg::Vst::kInput, busIndex, arr) == Steinberg::kResultTrue) {
                inputArrangements.push_back(arr);
            } else {
                inputArrangements.push_back(Steinberg::Vst::SpeakerArr::kEmpty);
            }


            Steinberg::Vst::BusInfo busInfo;
            Steinberg::tresult result = component->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, busIndex, busInfo);

            Steinberg::Vst::AudioBusBuffers bufs;
            bufs.channelBuffers32 = new float*[busInfo.channelCount];
            bufs.numChannels = busInfo.channelCount;
            bufs.silenceFlags = 0;

            float* bufferData = new float[am->bufferSize * busInfo.channelCount];

            for(size_t ch = 0; ch<busInfo.channelCount; ch++) {
                bufs.channelBuffers32[ch] = bufferData + (ch * am->bufferSize);
            }

            inputBuses.push_back(bufs);
        }

        for(Steinberg::int32 busIndex = 0; busIndex<numOutputs; busIndex++) {
            Steinberg::Vst::SpeakerArrangement arr;
            if (audioProcessor->getBusArrangement(Steinberg::Vst::kOutput, busIndex, arr) == Steinberg::kResultTrue) {
                outputArrangements.push_back(arr);
            } else {
                outputArrangements.push_back(Steinberg::Vst::SpeakerArr::kEmpty);
            }




            Steinberg::Vst::BusInfo busInfo;
            Steinberg::tresult result = component->getBusInfo(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, busIndex, busInfo);

            Steinberg::Vst::AudioBusBuffers bufs;
            bufs.channelBuffers32 = new float*[busInfo.channelCount];
            bufs.numChannels = busInfo.channelCount;
            bufs.silenceFlags = 0;

            float* bufferData = new float[am->bufferSize * busInfo.channelCount];

            for(size_t ch = 0; ch<busInfo.channelCount; ch++) {
                bufs.channelBuffers32[ch] = bufferData + (ch * am->bufferSize);
            }

            outputBuses.push_back(bufs);
        }

        auto result = audioProcessor->setBusArrangements(
            inputArrangements.data(), static_cast<Steinberg::int32>(inputArrangements.size()),
                                                         outputArrangements.data(), static_cast<Steinberg::int32>(outputArrangements.size())
        );


        if (result != Steinberg::kResultOk) {
            std::cerr << "Failed to set bus arrangements" << std::endl;
        }


        component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kOutput, 0, true);

        for (Steinberg::int32 i = 0; i < component->getBusCount(Steinberg::Vst::kAudio, Steinberg::Vst::kInput); ++i) {
            component->activateBus(Steinberg::Vst::kAudio, Steinberg::Vst::kInput, i, true);
        }

        for (auto& bus : inputBuses) bus.silenceFlags = 0;
        for (auto& bus : outputBuses) bus.silenceFlags = 0;


        data.symbolicSampleSize = Steinberg::Vst::kSample32;
        data.numSamples = am->bufferSize;
        data.outputs = outputBuses.data();
        data.numOutputs = static_cast<Steinberg::int32>(outputBuses.size());

        data.inputs = inputBuses.data();
        data.numInputs = static_cast<Steinberg::int32>(inputBuses.size());

        data.inputEvents = eventList;


        if (component->setActive(true) != Steinberg::kResultTrue) {
            std::cerr << "Failed to activate component." << std::endl;
            return false;
        }

        if (audioProcessor->setupProcessing(setup) != Steinberg::kResultTrue) {
            std::cerr << "Failed to setup audio processor." << std::endl;
            return false;
        }

        if (audioProcessor->setProcessing(true) != Steinberg::kResultTrue) {
            std::cerr << "Failed to set audio processor to processing state" << std::endl;
            return false;
        }

    }




    if(createEditControllerAndPlugView(controllerCID)) {

        std::cout << "window is ready to show" <<std::endl;

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

    if(windowOpen) return;
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
