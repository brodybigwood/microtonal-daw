#include <RtAudio.h>
#include <SDL3/SDL.h>
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
        static void SDLCALL sdlCallback(void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount);

        static AudioManager* instance();

        void setProject(Project* project);

        unsigned int latency;

    private:
        bool startSDL();
        bool startRtAudio();
        bool stopSDL();
        bool stopRtAudio();

        void audioThread();
        std::thread audioThreadHandle;

        // RtAudio backend
        RtAudio rtaudio;
        RtAudio::StreamParameters outputParams;
        RtAudio::StreamParameters inputParams;
        RtAudio::StreamOptions options;

        // SDL backend
        SDL_AudioStream* sdlStream_ = nullptr;
        std::vector<float> sdlScratch_;   // non-interleaved temp for DSP
        std::vector<float> sdlInterleaved_; // interleaved temp for SDL

        bool hasInput_ = false;
        bool usingSDL_ = false;
};
#endif
