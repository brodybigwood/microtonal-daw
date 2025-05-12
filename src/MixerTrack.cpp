#include "MixerTrack.h"
#include "Project.h"

MixerTrack::MixerTrack(Project* project) {
    this->edit = project->edit;
    this->engine = &project->engine;
    juce::ValueTree valueTree("TRACK");
    //this->track = new tracktion_engine::AudioTrack(*edit, valueTree);

    this->track = edit->insertNewAudioTrack(tracktion::engine::TrackInsertPoint::getEndOfTracks(*edit), nullptr, 0).get();



}


MixerTrack::~MixerTrack() {

}