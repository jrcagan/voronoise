#pragma once

#include "PluginProcessor.h"
#include <JuceHeader.h> 
#include <vector>

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

private:
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    VoronoiseAudioProcessor& processorRef;
    std::vector<juce::Point<float>> points;
    juce::ValueTree valueTree;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoronoiseAudioProcessorEditor)
};
