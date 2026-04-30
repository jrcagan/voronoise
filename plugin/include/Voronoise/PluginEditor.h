#pragma once

#include <JuceHeader.h> 
#include <vector>
#include "PluginProcessor.h"
#include "geometry/Utils.h"
#include "primitives/Ball.h"


//==============================================================================
class VoronoiseAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit VoronoiseAudioProcessorEditor (VoronoiseAudioProcessor&);
    ~VoronoiseAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDoubleClick (const juce::MouseEvent& event) override;
    void handleNoteEvent(int noteNumber, int velocity);
    void spawnBall(int noteNumber, int velocity);


private:
    VoronoiseAudioProcessor& processorRef;
    std::vector<GeoUtils::Point> points; 
    std::vector<Ball> balls;
    juce::ValueTree valueTree;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoronoiseAudioProcessorEditor)
};
