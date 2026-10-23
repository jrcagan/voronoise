#include "Voronoise/PluginEditor.h"

//==============================================================================
VoronoiseAudioProcessorEditor::VoronoiseAudioProcessorEditor(VoronoiseAudioProcessor &p)
    : AudioProcessorEditor(&p), mainPanel(p)
{
    setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(mainPanel);

    // the panel is drawn at one size and scaled, so keep the window the same shape
    constexpr double ASPECT = static_cast<double>(UI::MainPanel::WIDTH) / UI::MainPanel::HEIGHT;
    setResizable(true, true);
    getConstrainer()->setFixedAspectRatio(ASPECT);
    setResizeLimits(UI::MainPanel::WIDTH / 2, UI::MainPanel::HEIGHT / 2,
                    UI::MainPanel::WIDTH * 2, UI::MainPanel::HEIGHT * 2);
    setSize(static_cast<int>(UI::MainPanel::WIDTH * 0.9), static_cast<int>(UI::MainPanel::HEIGHT * 0.9));
}

VoronoiseAudioProcessorEditor::~VoronoiseAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

void VoronoiseAudioProcessorEditor::paint(juce::Graphics &g)
{
    g.fillAll(juce::Colours::black);
}

void VoronoiseAudioProcessorEditor::resized()
{
    const float scale = static_cast<float>(getWidth()) / static_cast<float>(UI::MainPanel::WIDTH);
    mainPanel.setBounds(0, 0, UI::MainPanel::WIDTH, UI::MainPanel::HEIGHT);
    mainPanel.setTransform(juce::AffineTransform::scale(scale));
}
