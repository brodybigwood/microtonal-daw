#include <RtAudio.h>
#include <thread>
#include <vector>
#include <string>
#include "Project.h"

#ifndef AUDIOMANAGER_H
#define AUDIOMANAGER_H

class AudioManager {
    public:
        AudioManager();
        ~AudioManager();

        Project* project;
        unsigned int sampleRate;
        unsigned int bufferSize;
        unsigned int inputChannels;
        unsigned int outputChannels;

        double streamTimeSeconds;

        bool start();
        bool stop();
        bool restart();

        std::vector<RtAudio::DeviceInfo> getOutputDevices();
        std::vector<RtAudio::DeviceInfo> getInputDevices();
        std::string getDeviceName(int deviceId);

        static int callback(void *outputBuffer, void *inputBuffer, unsigned int bufferSize, double streamTimeSeconds, RtAudioStreamStatus status, void* userData);

        static AudioManager* instance();

        void setProject(Project* project);

        unsigned int latency;

    private:

        void audioThread();

        std::thread audioThreadHandle;

        RtAudio rtaudio;
        RtAudio::StreamParameters outputParams;
        RtAudio::StreamParameters inputParams;
        RtAudio::StreamOptions options;
        bool hasInput_ = false;
};
#endif
