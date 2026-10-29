#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Voronoise/ui/Theme.h"
#include "Voronoise/ui/MainPanel.h"

//==============================================================================
// holds the main panel and scales it to the window, the panel has all the actual ui
class VoronoiseAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            public juce::DragAndDropContainer
{
public:
    explicit VoronoiseAudioProcessorEditor (VoronoiseAudioProcessor&);
    ~VoronoiseAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    UI::MainPanel& getMainPanel() { return mainPanel; }
    UI::Canvas& getCanvas() { return mainPanel.getCanvas(); }

private:
    Theme::LookAndFeel lookAndFeel;
    UI::MainPanel mainPanel;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoronoiseAudioProcessorEditor)
};
