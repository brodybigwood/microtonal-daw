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

void SDLCALL AudioManager::sdlCallback(void *userdata, SDL_AudioStream *stream, int additional_amount, int /*total_amount*/) {
    AudioManager* am = static_cast<AudioManager*>(userdata);
    Project* project = am->project;
    int outCh = static_cast<int>(am->outputChannels);
    int sr = static_cast<int>(am->sampleRate);
    int fixedBS = static_cast<int>(am->bufferSize); // fixed chunk size matching RtAudio path

    if (fixedBS <= 0) fixedBS = 512;

    // additional_amount is in BYTES (SDL3 doc). Convert to frames.
    int bytesPerFrame = outCh * static_cast<int>(sizeof(float));
    int additionalFrames = additional_amount / bytesPerFrame;
    if (additionalFrames <= 0) return;

    // Process in fixed-size chunks so the DSP sees consistent buffer sizes.
    int remaining = additionalFrames;
    size_t maxChunk = static_cast<size_t>(fixedBS) * static_cast<size_t>(outCh);
    if (am->sdlScratch_.size() < maxChunk) am->sdlScratch_.resize(maxChunk);
    if (am->sdlInterleaved_.size() < maxChunk) am->sdlInterleaved_.resize(maxChunk);

    while (remaining > 0) {
        int frames = (remaining >= fixedBS) ? fixedBS : remaining;
        remaining -= frames;

        if (project->loading.load()) {
            // Still push silence if loading
            size_t byteCount = static_cast<size_t>(frames) * static_cast<size_t>(outCh) * sizeof(float);
            am->sdlInterleaved_.assign(frames * outCh, 0.0f);
            SDL_PutAudioStreamData(stream, am->sdlInterleaved_.data(), static_cast<int>(byteCount));
            continue;
        }

        project->sampleTime += frames;

        if (project->isPlaying.load()) {
            const double dt = static_cast<double>(frames) / sr;
            project->timeSeconds.store(project->timeSeconds.load() + dt);
        }

        am->streamTimeSeconds += static_cast<double>(frames) / sr;

        size_t bufSize = static_cast<size_t>(frames) * static_cast<size_t>(outCh);
        memset(am->sdlScratch_.data(), 0, bufSize * sizeof(float));

        // Apply queued actions before DSP
        if (project->um) {
            project->processor->setThreadActiveRoot(project->processor->audioManager);
            project->um->flushAudioSync();
        }

        int ic = 0;
        int bs = frames;
        project->process(nullptr, am->sdlScratch_.data(), bs, ic, outCh, sr);

        // Interleave
        float* dst = am->sdlInterleaved_.data();
        for (int f = 0; f < frames; ++f) {
            for (int ch = 0; ch < outCh; ++ch) {
                dst[f * outCh + ch] = am->sdlScratch_[ch * frames + f];
            }
        }

        SDL_PutAudioStreamData(stream, am->sdlInterleaved_.data(),
                               frames * outCh * static_cast<int>(sizeof(float)));
    }

    if (!project->loading.load()) {
        if (project->isPlaying.load()) {
            project->effectiveTime.store(
                project->timeSeconds.load() - static_cast<double>(am->latency) / sr
            );
        } else {
            project->effectiveTime.store(project->timeSeconds.load());
        }
    }
}

#ifndef __EMSCRIPTEN__
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
#endif // __EMSCRIPTEN__

static unsigned int decodeSampleRate(int index, unsigned int preferred) {
    switch (index) {
        case 0:  return preferred;
        case 1:  return 22050;
        case 2:  return 44100;
        case 3:  return 48000;
        case 4:  return 88200;
        case 5:  return 96000;
        case 6:  return 176400;
        case 7:  return 192000;
        default: return 48000;
    }
}

static unsigned int decodeBufferSize(int value) {
    static const unsigned int sizes[] = {64, 128, 256, 512, 1024, 2048, 4096};
    if (value >= 0 && value <= 6) return sizes[value];
    return static_cast<unsigned int>(value); // tolerate old direct-size values
}

#ifndef __EMSCRIPTEN__
bool AudioManager::startRtAudio() {
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

    std::cout << "[Audio:RtAudio] output device: " << info.name << " (id=" << outDev << ")" << std::endl;

    // --- sample rate ---
    sampleRate = decodeSampleRate(s.audioSampleRate(), info.preferredSampleRate);

    // --- buffer size ---
    bufferSize = decodeBufferSize(s.audioBufferSize());

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
        std::cout << "[Audio:RtAudio] input device: " << inInfo.name << " (id=" << inDev << ")" << std::endl;
    } else {
        inputChannels = 0;
    }

    // --- stream options ---
    options.flags = RTAUDIO_NONINTERLEAVED;
    options.streamName = "DAW";
    options.numberOfBuffers = s.audioTripleBuffer() ? 3 : 0;

    std::cout << "[Audio:RtAudio] sampleRate=" << sampleRate << " bufferSize=" << bufferSize
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
        std::cerr << "[Audio:RtAudio] openStream failed: " << e << std::endl;
        return false;
    }

    audioThreadHandle = std::thread(&AudioManager::audioThread, this);

    latency = rtaudio.getStreamLatency();
    std::cout << "[Audio:RtAudio] stream started, latency=" << latency << std::endl;

    usingSDL_ = false;
    if (project) project->processing = true;
    return true;
}
#endif // __EMSCRIPTEN__

bool AudioManager::startSDL() {
    auto& s = Settings::instance();

    sampleRate = decodeSampleRate(s.audioSampleRate(), 48000);
    bufferSize = decodeBufferSize(s.audioBufferSize());
    outputChannels = 2;
    inputChannels = 0;
    hasInput_ = false;

    std::cout << "[Audio:SDL] sampleRate=" << sampleRate << " bufferSize=" << bufferSize
              << " outCh=" << outputChannels << std::endl;

    SDL_AudioSpec spec;
    spec.format = SDL_AUDIO_F32;
    spec.channels = static_cast<int>(outputChannels);
    spec.freq = static_cast<int>(sampleRate);

    sdlStream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                           AudioManager::sdlCallback, this);
    if (!sdlStream_) {
        std::cerr << "[Audio:SDL] SDL_OpenAudioDeviceStream failed: " << SDL_GetError() << std::endl;
        return false;
    }

    SDL_ResumeAudioStreamDevice(sdlStream_);

    float streamGain = SDL_GetAudioStreamGain(sdlStream_);
    SDL_AudioDeviceID devId = SDL_GetAudioStreamDevice(sdlStream_);
    float deviceGain = devId ? SDL_GetAudioDeviceGain(devId) : -1.f;
    std::cout << "[Audio:SDL] streamGain=" << streamGain << " deviceGain=" << deviceGain << std::endl;

    latency = 0; // SDL doesn't expose latency the same way
    std::cout << "[Audio:SDL] stream started" << std::endl;

    usingSDL_ = true;
    if (project) project->processing = true;
    return true;
}

bool AudioManager::start() {
#ifndef __EMSCRIPTEN__
    auto& s = Settings::instance();
    if (s.audioEngine() != 0)
        return startRtAudio();
#endif
    return startSDL();
}

bool AudioManager::restart() {
    stop();
    return start();
}

#ifndef __EMSCRIPTEN__
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

bool AudioManager::stopRtAudio() {
    try {
        if (rtaudio.isStreamOpen()) {
            rtaudio.stopStream();
            rtaudio.closeStream();
            std::cout << "[Audio:RtAudio] stream stopped" << std::endl;
        }
    } catch (RtAudioErrorType& e) {
        std::cerr << "[Audio:RtAudio] stop error: " << e << std::endl;
        return false;
    }
    if (audioThreadHandle.joinable()) {
        audioThreadHandle.join();
    }
    return true;
}
#endif // __EMSCRIPTEN__

bool AudioManager::stopSDL() {
    if (sdlStream_) {
        SDL_DestroyAudioStream(sdlStream_);
        sdlStream_ = nullptr;
        std::cout << "[Audio:SDL] stream destroyed" << std::endl;
    }
    return true;
}

bool AudioManager::stop() {
    if (usingSDL_) {
        usingSDL_ = false;
        return stopSDL();
    }
#ifndef __EMSCRIPTEN__
    else {
        return stopRtAudio();
    }
#endif
    return false;
}


#ifndef __EMSCRIPTEN__
void AudioManager::audioThread() {
    try {
        // Start the stream and keep it running
        rtaudio.startStream();
    } catch (RtAudioErrorType &e) {
        std::cerr << "Error starting audio stream: " << e << std::endl;
    }
}
#endif // __EMSCRIPTEN__
