#include "AudioManager.h"
#include "NodeProcessor.h"
#include "Settings.h"
#include <jack/jack.h>
#include <thread>
#include <chrono>

AudioManager::AudioManager() {
#ifndef __EMSCRIPTEN__
    rtaudio = new RtAudio(RtAudio::UNSPECIFIED);
    std::cout << "[Audio] selected API: " << rtaudio->getApiName(rtaudio->getCurrentApi()) << std::endl;
#endif
}

AudioManager::~AudioManager() {
    stop();
#ifndef __EMSCRIPTEN__
    delete rtaudio;
    rtaudio = nullptr;
#endif
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

        am->streamTimeSeconds += static_cast<double>(frames) / sr;

        size_t bufSize = static_cast<size_t>(frames) * static_cast<size_t>(outCh);
        memset(am->sdlScratch_.data(), 0, bufSize * sizeof(float));

        // Apply queued actions before DSP
        if (project->um) {
            project->processor->setThreadActiveRoot(project->processor->dspGraph);
            project->um->flushAudioSync();
        }

        int ic = 0;
        int bs = frames;
        project->process(nullptr, am->sdlScratch_.data(), bs, ic, outCh, sr);

        // Advance AFTER processing: the block [t, t+dt) must be processed at
        // time t, or notes starting exactly at the play position are skipped.
        if (project->isPlaying.load()) {
            const double dt = static_cast<double>(frames) / sr;
            project->timeSeconds.store(project->timeSeconds.load() + dt);
        }

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

    AudioManager::instance()->streamTimeSeconds += static_cast<double>(bufferSize) / audioManager->sampleRate;

    unsigned int numChannels = audioManager->outputChannels;

    memset(outputBuffer, 0, bufferSize * numChannels * sizeof(float));

    // Apply queued actions to the audio copy before DSP.
    if (project->um) {
        project->processor->setThreadActiveRoot(project->processor->dspGraph);
        project->um->flushAudioSync();
    }

    // JACK can change buffer size dynamically — keep in sync.
    if (static_cast<unsigned int>(audioManager->bufferSize) != bufferSize) {
        audioManager->bufferSize = static_cast<int>(bufferSize);
    }

    float *outBuffer = static_cast<float *>(outputBuffer);
    float *inBuffer = static_cast<float *>(inputBuffer);

    int ic = static_cast<int>(audioManager->inputChannels);
    int oc = static_cast<int>(audioManager->outputChannels);
    int bs = static_cast<int>(bufferSize);
    int sr = static_cast<int>(audioManager->sampleRate);

    project->process(inBuffer, outBuffer, bs, ic, oc, sr);

    // Advance AFTER processing: the block [t, t+dt) must be processed at
    // time t, or notes starting exactly at the play position are skipped.
    if(project->isPlaying.load()) {
        const double dt = static_cast<double>(bufferSize) / audioManager->sampleRate;
        project->timeSeconds.store(project->timeSeconds.load() + dt);
    }

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
static bool tryOpenStream(RtAudio* ra, void* userData, RtAudio::StreamParameters* outParams,
                          RtAudio::StreamParameters* inParams, unsigned int sampleRate,
                          unsigned int* bufferFrames, RtAudio::StreamOptions* options,
                          unsigned int* inCh, bool* hasInput) {
    RtAudioErrorType result = ra->openStream(outParams, inParams, RTAUDIO_FLOAT32, sampleRate,
                                             bufferFrames, &AudioManager::callback, userData, options);
    if (result == RTAUDIO_NO_ERROR) return true;

    std::cerr << "[Audio:RtAudio] openStream failed: " << result << std::endl;
    if (inParams) {
        std::cerr << "[Audio:RtAudio] retrying without input device..." << std::endl;
        result = ra->openStream(outParams, nullptr, RTAUDIO_FLOAT32, sampleRate,
                                bufferFrames, &AudioManager::callback, userData, options);
        if (result == RTAUDIO_NO_ERROR) {
            *inCh = 0;
            *hasInput = false;
            return true;
        }
        std::cerr << "[Audio:RtAudio] openStream without input also failed: " << result << std::endl;
    }
    return false;
}

bool AudioManager::startRtAudio() {
    auto& s = Settings::instance();

    sampleRate = decodeSampleRate(s.audioSampleRate(), 48000);
    bufferSize = decodeBufferSize(s.audioBufferSize());
    options.flags = RTAUDIO_NONINTERLEAVED;
    options.streamName = "DAW";
    options.numberOfBuffers = s.audioTripleBuffer() ? 3 : 0;

    // --- try JACK first ---
    delete rtaudio;
    rtaudio = new RtAudio(RtAudio::UNIX_JACK);
    std::cout << "[Audio] trying JACK..." << std::endl;

    unsigned int outDev = rtaudio->getDefaultOutputDevice();
    outputChannels = 32;
    outputParams.deviceId = outDev;
    outputParams.nChannels = outputChannels;
    outputParams.firstChannel = 0;

    inputChannels = 32;
    inputParams.deviceId = outDev;
    inputParams.nChannels = inputChannels;
    inputParams.firstChannel = 0;
    hasInput_ = true;
    options.flags |= RTAUDIO_JACK_DONT_CONNECT;
    std::cout << "[Audio:RtAudio] JACK outCh=" << outputChannels << " inCh=" << inputChannels << std::endl;

    // Adopt JACK server rate before opening stream
    {
        jack_client_t* jc = jack_client_open("DAW-probe", JackNoStartServer, nullptr);
        if (jc) {
            sampleRate = jack_get_sample_rate(jc);
            jack_client_close(jc);
        }
    }
    if (tryOpenStream(rtaudio, this, &outputParams, &inputParams, sampleRate, &bufferSize, &options,
                      &inputChannels, &hasInput_))
        goto streamReady;

    // --- try ALSA ---
    options.flags &= ~RTAUDIO_JACK_DONT_CONNECT;
    {
    delete rtaudio;
    rtaudio = new RtAudio(RtAudio::LINUX_ALSA);
    std::cout << "[Audio] trying ALSA..." << std::endl;

    outDev = (s.audioOutputDevice() >= 0) ? static_cast<unsigned int>(s.audioOutputDevice()) : rtaudio->getDefaultOutputDevice();
    RtAudio::DeviceInfo info = rtaudio->getDeviceInfo(outDev);
    outputChannels = info.outputChannels > 0 ? info.outputChannels : 2;
    if (outputChannels > 32) outputChannels = 32;
    outputParams.deviceId = outDev;
    outputParams.nChannels = outputChannels;
    outputParams.firstChannel = 0;
    std::cout << "[Audio:RtAudio] ALSA device: " << info.name << " outCh=" << outputChannels << std::endl;

    {
        int inDevId = s.audioInputDevice();
        RtAudio::StreamParameters* inParams = nullptr;
        hasInput_ = false;
        inputChannels = 0;
        if (inDevId >= 0) {
            unsigned int inDev = static_cast<unsigned int>(inDevId);
            RtAudio::DeviceInfo inInfo = rtaudio->getDeviceInfo(inDev);
            inputChannels = inInfo.inputChannels > 0 ? inInfo.inputChannels : 2;
            if (inputChannels > 32) inputChannels = 32;
            inputParams.deviceId = inDev;
            inputParams.nChannels = inputChannels;
            inputParams.firstChannel = 0;
            inParams = &inputParams;
            hasInput_ = true;
            std::cout << "[Audio:RtAudio] ALSA input: " << inInfo.name << " inCh=" << inputChannels << std::endl;
        }
        if (!tryOpenStream(rtaudio, this, &outputParams, inParams, sampleRate, &bufferSize, &options,
                           &inputChannels, &hasInput_))
            return false;
    }
    } // ALSA scope

streamReady:
    std::cout << "[Audio:RtAudio] sampleRate=" << sampleRate << " bufferSize=" << bufferSize
              << " outCh=" << outputChannels << " inCh=" << inputChannels
              << " api=" << rtaudio->getApiName(rtaudio->getCurrentApi()) << std::endl;

    audioThreadHandle = std::thread(&AudioManager::audioThread, this);
    latency = rtaudio->getStreamLatency();
    std::cout << "[Audio:RtAudio] stream started, latency=" << latency << std::endl;

    // Auto-connect all DAW outputs to system playback ports
    if (rtaudio->getCurrentApi() == RtAudio::UNIX_JACK) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        jack_client_t* jc = jack_client_open("DAW-autowire", JackNoStartServer, nullptr);
        if (jc) {
            // DAW outputs -> system playback
            const char** sysPlay = jack_get_ports(jc, nullptr, JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);
            const char** dawOut = jack_get_ports(jc, "DAW", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput);
            if (sysPlay && dawOut)
                for (int i = 0; sysPlay[i] && dawOut[i]; i++)
                    jack_connect(jc, dawOut[i], sysPlay[i]);
            if (sysPlay) jack_free(sysPlay);
            if (dawOut) jack_free(dawOut);

            // system capture -> DAW inputs
            const char** sysCap = jack_get_ports(jc, nullptr, JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput);
            const char** dawIn = jack_get_ports(jc, "DAW", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);
            if (sysCap && dawIn)
                for (int i = 0; sysCap[i] && dawIn[i]; i++)
                    jack_connect(jc, sysCap[i], dawIn[i]);
            if (sysCap) jack_free(sysCap);
            if (dawIn) jack_free(dawIn);
            jack_client_close(jc);
        }
    }

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
    if (s.audioEngine() != 0) {
        if (startRtAudio()) return true;
        std::cerr << "[Audio] RtAudio failed, falling back to SDL..." << std::endl;
    }
#endif
    return startSDL();
}

bool AudioManager::restart() {
    stop();
#ifndef __EMSCRIPTEN__
    delete rtaudio;
    rtaudio = new RtAudio(RtAudio::UNSPECIFIED);
    std::cout << "[Audio] selected API: " << rtaudio->getApiName(rtaudio->getCurrentApi()) << std::endl;
#endif
    return start();
}

#ifndef __EMSCRIPTEN__
std::vector<RtAudio::DeviceInfo> AudioManager::getOutputDevices() {
    std::vector<RtAudio::DeviceInfo> out;
    for (auto id : rtaudio->getDeviceIds()) {
        auto info = rtaudio->getDeviceInfo(id);
        if (info.outputChannels > 0) out.push_back(info);
    }
    return out;
}

std::vector<RtAudio::DeviceInfo> AudioManager::getInputDevices() {
    std::vector<RtAudio::DeviceInfo> in;
    for (auto id : rtaudio->getDeviceIds()) {
        auto info = rtaudio->getDeviceInfo(id);
        if (info.inputChannels > 0) in.push_back(info);
    }
    return in;
}

std::string AudioManager::getDeviceName(int deviceId) {
    if (deviceId < 0) return "Default";
    try {
        return rtaudio->getDeviceInfo(static_cast<unsigned int>(deviceId)).name;
    } catch (...) {
        return "Unknown";
    }
}

bool AudioManager::stopRtAudio() {
    if (!rtaudio) return false;
    if (rtaudio->isStreamOpen()) {
        rtaudio->stopStream();
        rtaudio->closeStream();
        std::cout << "[Audio:RtAudio] stream stopped" << std::endl;
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
    RtAudioErrorType result = rtaudio->startStream();
    if (result != RTAUDIO_NO_ERROR)
        std::cerr << "[Audio:RtAudio] startStream error: " << result << std::endl;
}
#endif // __EMSCRIPTEN__
