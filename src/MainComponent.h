#pragma once

#include <JuceHeader.h>
#include "WindowHandler.h"
#include "Project.h"
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <iostream>

//#include "vars.h"
//==============================================================================
/*
    This component lives inside our window, and this is where you should put all
    your controls and content.
*/
class MainComponent  : public juce::AudioAppComponent, juce::Timer, public juce::Component
{
public:
    //==============================================================================
    MainComponent(Project* project);
    ~MainComponent() override;

    //==============================================================================
    void prepareToPlay (int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    //==============================================================================
    void paint (juce::Graphics& g) override;
    void resized() override;

    std::unique_ptr<WindowHandler> gui;  // Changed to unique_ptr
    std::unique_ptr<juce::AudioProcessorEditor> pluginEditor;

    Project* project;

    void timerCallback() override {
        if(!gui->tick()) {
            juce::JUCEApplicationBase::getInstance()->quit();

        }
    }
    

private:
    //==============================================================================
    // Your private member variables go here...


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};


