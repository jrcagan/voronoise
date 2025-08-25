#pragma once

#include "PluginProcessor.h"
#include "geometry/Utils.h"
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
    VoronoiseAudioProcessor& processorRef;
    std::vector<GeoUtils::Point> points; 
    juce::ValueTree valueTree;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoronoiseAudioProcessorEditor)
};
