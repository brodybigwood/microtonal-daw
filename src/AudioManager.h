#include <RtAudio.h>
#include <thread>
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

        bool start();

        bool stop();

        static int callback(void *outputBuffer, void *inputBuffer, unsigned int bufferSize, double streamTimeSeconds, RtAudioStreamStatus status, void* userData);

        static AudioManager* instance();

        void setProject(Project* project);

    private:

        void audioThread();

        std::thread audioThreadHandle;

        RtAudio rtaudio;
        RtAudio::StreamParameters outputParams;
        RtAudio::StreamOptions options;
};
#endif
