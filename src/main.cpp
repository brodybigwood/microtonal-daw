/*
  ==============================================================================

    main.cpp
    Created: 10 Apr 2025 2:04:22am
    Author:  brody

  ==============================================================================
*/


#include "Project.h"
#include <JuceHeader.h>
#include "MainComponent.h"
#include <memory>
#include <thread>
//#include "vars.h"



class NewProjectApplication  : public juce::JUCEApplication
{
public:
    //==============================================================================
    NewProjectApplication() {}

    const juce::String getApplicationName() override       { return ProjectInfo::projectName; }
    const juce::String getApplicationVersion() override    { return ProjectInfo::versionString; }
    bool moreThanOneInstanceAllowed() override             { return true; }

    Project* projectHolder = new Project();
    

    std::unique_ptr<MainComponent> mainComponent;
    //==============================================================================
    void initialise (const juce::String& commandLine) override
    {

        mainComponent = std::make_unique<MainComponent>(projectHolder);

    }

    void shutdown() override
    {
        // Add your application's shutdown code here..

        mainComponent = nullptr; 
    }

    //==============================================================================
    void systemRequestedQuit() override
    {
        // This is called when the app is being asked to quit: you can ignore this
        // request and let the app carry on running, or call quit() to allow the app to close.
        quit();
    }

    void anotherInstanceStarted (const juce::String& commandLine) override
    {
        // When another instance of the app is launched while this one is running,
        // this method is invoked, and the commandLine parameter tells you what
        // the other instance's command-line arguments were.
    }

    //==============================================================================

private:


    
};

//==============================================================================
// This macro generates the main() routine that launches the app.
START_JUCE_APPLICATION (NewProjectApplication)

