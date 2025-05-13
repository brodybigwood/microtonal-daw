#include "AudioManager.h"
#include <cmath>

AudioManager::AudioManager(Project* project) {
    this->project = project;
}

AudioManager::~AudioManager() {
    stop();
}

int AudioManager::callback(void *outputBuffer, void *inputBuffer, unsigned int bufferSize, double streamTimeSeconds, RtAudioStreamStatus status, void* userData) {

    AudioManager *audioManager = static_cast<AudioManager *>(userData);

    // Process the audio data (this is just an example of processing the output buffer)
    float *outBuffer = static_cast<float *>(outputBuffer);
    float *inBuffer = static_cast<float *>(inputBuffer);


    // Fill the output buffer with a simple sine wave (or your own audio data processing)
    Project* project = audioManager->project;

    std::cout<<project->isPlaying<<std::endl;

    if(project->isPlaying) {
        project->timeSeconds += static_cast<double>(bufferSize) / audioManager->sampleRate;


        for (unsigned int i = 0; i < bufferSize; ++i) {
            float sample = 0.5f * sin(2.0 * M_PI * 440.0 * streamTimeSeconds);  // 440Hz sine wave
            outBuffer[i] = sample;
            std::cout<<outBuffer[i]<<std::endl;
        }


    } else {
        for (unsigned int i = 0; i < bufferSize; ++i) {
            float sample = 0;
            outBuffer[i] = sample;
            std::cout<<outBuffer[i]<<std::endl;
        }
    }
    return 0;  // Return 0 to indicate successful processing
}

bool AudioManager::start() {

    std::vector<unsigned int> deviceIds = rtaudio.getDeviceIds();

    for (unsigned int i = 0; i < deviceIds.size(); ++i) {
        RtAudio::DeviceInfo info = rtaudio.getDeviceInfo(deviceIds[i]);
        std::cout << "Device " << i << ": " << info.name << std::endl;
        std::cout << "  Input channels: " << info.inputChannels << ", Output channels: " << info.outputChannels << std::endl;
        std::cout << "  Default rate: " << info.preferredSampleRate << std::endl;
    }

    auto defaultDevice = rtaudio.getDefaultOutputDevice();

    RtAudio::DeviceInfo info = rtaudio.getDeviceInfo(deviceIds[1]);
    sampleRate = info.preferredSampleRate;
    outputChannels = info.outputChannels > 0 ? info.outputChannels : 2;

    outputParams.deviceId = deviceIds[1];
    outputParams.nChannels = outputChannels;
    outputParams.firstChannel = 0; 

    bufferSize = 512; 

    std::cout << "Using default sample rate: " << sampleRate << std::endl;
    std::cout << "Using buffer size: " << bufferSize << std::endl;
    std::cout << "Using output channels: " << outputChannels << std::endl;

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