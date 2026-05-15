#include "AudioManager.h"
#include "NodeProcessor.h"
#include "Settings.h"

AudioManager::AudioManager() {

}

AudioManager::~AudioManager() {
    stop();
}

AudioManager* AudioManager::instance() {
    static AudioManager am;
    return &am;
}

void AudioManager::setProject(Project* project) {
    this->project = project;
}

int AudioManager::callback(void *outputBuffer, void *inputBuffer, unsigned int bufferSize, double streamTimeSeconds, RtAudioStreamStatus status, void* userData) {

    (void)status;

    AudioManager *audioManager = static_cast<AudioManager *>(userData);

    Project* project = audioManager->project;

    if (project->loading.load()) {
        unsigned int numChannels = audioManager->outputChannels;
        memset(outputBuffer, 0, bufferSize * numChannels * sizeof(float));
        return 0;
    }

    project->sampleTime += bufferSize;

    if(project->isPlaying.load()) {
        const double dt = static_cast<double>(bufferSize) / audioManager->sampleRate;
        project->timeSeconds.store(project->timeSeconds.load() + dt);
    }

    AudioManager::instance()->streamTimeSeconds += static_cast<double>(bufferSize) / audioManager->sampleRate;

    unsigned int numChannels = audioManager->outputChannels;

    memset(outputBuffer, 0, bufferSize * numChannels * sizeof(float));

    // Apply queued actions to the audio copy before DSP.
    if (project->um) {
        project->processor->setThreadActiveRoot(project->processor->audioManager);
        project->um->flushAudioSync();
    }

    float *outBuffer = static_cast<float *>(outputBuffer);
    float *inBuffer = static_cast<float *>(inputBuffer);

    int ic = static_cast<int>(audioManager->inputChannels);
    int oc = static_cast<int>(audioManager->outputChannels);
    int bs = static_cast<int>(audioManager->bufferSize);
    int sr = static_cast<int>(audioManager->sampleRate);

    project->process(inBuffer, outBuffer, bs, ic, oc, sr);

    if(project->isPlaying.load()) {
        project->effectiveTime.store(
            project->timeSeconds.load() - static_cast<double>(audioManager->latency) / audioManager->sampleRate
        );
    } else {
        project->effectiveTime.store(project->timeSeconds.load());
    }

    return 0;
}

static unsigned int decodeSampleRate(int index, unsigned int preferred) {
    switch (index) {
        case 0:  return preferred;
        case 1:  return 44100;
        case 2:  return 48000;
        case 3:  return 96000;
        case 4:  return 192000;
        default: return preferred;
    }
}

bool AudioManager::start() {
    auto& s = Settings::instance();

    // --- output device ---
    int outDevId = s.audioOutputDevice();
    unsigned int outDev = (outDevId < 0) ? rtaudio.getDefaultOutputDevice() : static_cast<unsigned int>(outDevId);
    RtAudio::DeviceInfo info = rtaudio.getDeviceInfo(outDev);
    outputChannels = info.outputChannels > 0 ? info.outputChannels : 2;
    if (outputChannels > 8) outputChannels = 8;
    outputParams.deviceId = outDev;
    outputParams.nChannels = outputChannels;
    outputParams.firstChannel = 0;

    std::cout << "[Audio] output device: " << info.name << " (id=" << outDev << ")" << std::endl;

    // --- sample rate ---
    sampleRate = decodeSampleRate(s.audioSampleRate(), info.preferredSampleRate);

    // --- buffer size ---
    bufferSize = static_cast<unsigned int>(s.audioBufferSize());

    // --- input device ---
    int inDevId = s.audioInputDevice();
    RtAudio::StreamParameters* inParams = nullptr;
    hasInput_ = false;
    if (inDevId >= 0) {
        unsigned int inDev = static_cast<unsigned int>(inDevId);
        RtAudio::DeviceInfo inInfo = rtaudio.getDeviceInfo(inDev);
        inputChannels = inInfo.inputChannels > 0 ? inInfo.inputChannels : 2;
        if (inputChannels > 8) inputChannels = 8;
        inputParams.deviceId = inDev;
        inputParams.nChannels = inputChannels;
        inputParams.firstChannel = 0;
        inParams = &inputParams;
        hasInput_ = true;
        std::cout << "[Audio] input device: " << inInfo.name << " (id=" << inDev << ")" << std::endl;
    } else {
        inputChannels = 0;
    }

    // --- stream options ---
    options.flags = RTAUDIO_NONINTERLEAVED;
    options.streamName = "DAW";
    options.numberOfBuffers = s.audioTripleBuffer() ? 3 : 0;

    std::cout << "[Audio] sampleRate=" << sampleRate << " bufferSize=" << bufferSize
              << " outCh=" << outputChannels << " inCh=" << inputChannels
              << " tripleBuf=" << (s.audioTripleBuffer() ? "yes" : "no") << std::endl;

    try {
        rtaudio.openStream(
            &outputParams,
            inParams,
            RTAUDIO_FLOAT32,
            sampleRate,
            &bufferSize,
            &AudioManager::callback,
            this,
            &options
        );
    } catch (RtAudioErrorType& e) {
        std::cerr << "[Audio] openStream failed: " << e << std::endl;
        return false;
    }

    audioThreadHandle = std::thread(&AudioManager::audioThread, this);

    latency = rtaudio.getStreamLatency();
    std::cout << "[Audio] stream started, latency=" << latency << std::endl;

    if (project) project->processing = true;
    return true;
}

bool AudioManager::restart() {
    stop();
    return start();
}

std::vector<RtAudio::DeviceInfo> AudioManager::getOutputDevices() {
    std::vector<RtAudio::DeviceInfo> out;
    for (auto id : rtaudio.getDeviceIds()) {
        auto info = rtaudio.getDeviceInfo(id);
        if (info.outputChannels > 0) out.push_back(info);
    }
    return out;
}

std::vector<RtAudio::DeviceInfo> AudioManager::getInputDevices() {
    std::vector<RtAudio::DeviceInfo> in;
    for (auto id : rtaudio.getDeviceIds()) {
        auto info = rtaudio.getDeviceInfo(id);
        if (info.inputChannels > 0) in.push_back(info);
    }
    return in;
}

std::string AudioManager::getDeviceName(int deviceId) {
    if (deviceId < 0) return "Default";
    try {
        return rtaudio.getDeviceInfo(static_cast<unsigned int>(deviceId)).name;
    } catch (...) {
        return "Unknown";
    }
}

bool AudioManager::stop() {
    try {

        if (rtaudio.isStreamOpen()) {
            rtaudio.stopStream();
            rtaudio.closeStream();
            std::cout << "Audio stream stopped successfully!" << std::endl;
        }
    } catch (RtAudioErrorType& e) {
        std::cerr << "Error in RtAudio stop: " << e << std::endl;
        return false;
    }
    if (audioThreadHandle.joinable()) {
        audioThreadHandle.join(); 
    }

    return true;
}


void AudioManager::audioThread() {
    try {
        // Start the stream and keep it running
        rtaudio.startStream();
    } catch (RtAudioErrorType &e) {
        std::cerr << "Error starting audio stream: " << e << std::endl;
    }
}
