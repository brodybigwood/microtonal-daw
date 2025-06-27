#include "AudioManager.h"
#include <cmath>

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

    AudioManager *audioManager = static_cast<AudioManager *>(userData);

    Project* project = audioManager->project;

    project->sampleTime += bufferSize;

    if(project->isPlaying) {
        project->timeSeconds += static_cast<double>(bufferSize) / audioManager->sampleRate;
    }

    AudioManager::instance()->streamTimeSeconds += static_cast<double>(bufferSize) / audioManager->sampleRate;

    unsigned int numChannels = audioManager->outputChannels;
    std::vector<float> mono(bufferSize, 0.0f);

    memset(outputBuffer, 0, bufferSize * numChannels * sizeof(float));

    float *outBuffer = static_cast<float *>(outputBuffer);
    float *inBuffer = static_cast<float *>(inputBuffer);


    project->tracks[0]->process(mono.data(), bufferSize);


    for (unsigned int i = 0; i < bufferSize; ++i) {
        for (unsigned int ch = 0; ch < numChannels; ++ch) {
            outBuffer[i * numChannels + ch] = mono[i];
        }
    }

    project->effectiveTime = project->timeSeconds -  static_cast<double>(audioManager->latency) / audioManager->sampleRate;

    return 0;
}

bool AudioManager::start() {

    std::vector<unsigned int> deviceIds = rtaudio.getDeviceIds();

    for (unsigned int i = 0; i < deviceIds.size(); ++i) {
       // RtAudio::DeviceInfo info = rtaudio.getDeviceInfo(deviceIds[i]);
       // std::cout << "Device " << i << ": " << info.name << std::endl;
       // std::cout << "  Input channels: " << info.inputChannels << ", Output channels: " << info.outputChannels << std::endl;
        //std::cout << "  Default rate: " << info.preferredSampleRate << std::endl;
    }

    auto defaultDevice = rtaudio.getDefaultOutputDevice();

    RtAudio::DeviceInfo info = rtaudio.getDeviceInfo(deviceIds[1]);
    sampleRate = info.preferredSampleRate;
    outputChannels = info.outputChannels > 0 ? info.outputChannels : 2;

    outputParams.deviceId = deviceIds[1];
    outputParams.nChannels = outputChannels;
    outputParams.firstChannel = 0; 

    bufferSize = 512;

    //std::cout << "Using default sample rate: " << sampleRate << std::endl;
    //std::cout << "Using buffer size: " << bufferSize << std::endl;
    //std::cout << "Using output channels: " << outputChannels << std::endl;

    try {

        rtaudio.openStream(
            &outputParams, 
            NULL, 
            RTAUDIO_FLOAT32, 
            sampleRate, 
            &bufferSize, 
            &AudioManager::callback, 
            this
        );

    } catch (RtAudioErrorType& e) {
        std::cerr << "Error in RtAudio start: " << e<< std::endl;
        return false;
    }


    audioThreadHandle = std::thread(&AudioManager::audioThread, this);

    std::cout << "Audio stream started successfully!" << std::endl;

    std::cout << "AudioManager data: samplerate: " <<sampleRate << ", buff size: " <<bufferSize <<std::endl;

    latency = rtaudio.getStreamLatency();

    int bufferedFrames = 2;

    latency += bufferSize * bufferedFrames;

    std::cout<<"audio stream latency: "<<latency<<std::endl;

    return true;

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
    audioThreadHandle.join();  // Wait for the audio thread to finish before exiting

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
