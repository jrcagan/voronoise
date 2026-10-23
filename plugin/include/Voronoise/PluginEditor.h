#pragma once

#include <JuceHeader.h> 
#include <vector>
#include "PluginProcessor.h"
#include "geometry/Utils.h"
#include "primitives/Ball.h"


//==============================================================================
class VoronoiseAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            private juce::Timer
{
public:
    explicit VoronoiseAudioProcessorEditor (VoronoiseAudioProcessor&);
    ~VoronoiseAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDoubleClick (const juce::MouseEvent& event) override;
    void spawnBall(int noteNumber, int velocity);


private:
    void timerCallback() override;

    VoronoiseAudioProcessor& processorRef;
    std::vector<GeoUtils::Point> points; 
    std::vector<Ball> balls;
    juce::ValueTree valueTree;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoronoiseAudioProcessorEditor)
};
