#include "Voronoise/PluginProcessor.h"
#include "Voronoise/PluginEditor.h"

//==============================================================================
VoronoiseAudioProcessorEditor::VoronoiseAudioProcessorEditor (VoronoiseAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p), valueTree (p.getValueTree())
{
    juce::ignoreUnused (processorRef);
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize (400, 300);
}

VoronoiseAudioProcessorEditor::~VoronoiseAudioProcessorEditor()
{
}

//==============================================================================
void VoronoiseAudioProcessorEditor::paint (juce::Graphics& g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));

    g.setColour (juce::Colours::white);
    g.setFont (15.0f);
    g.drawFittedText ("Hello World!", getLocalBounds(), juce::Justification::centred, 1);

    auto sitesTree = valueTree.getChildWithName("Sites");

    for(int i = 0; i < sitesTree.getNumChildren(); i++) {

        auto site = sitesTree.getChild(i);
        float x = static_cast<float>(site["x"]);
        float y = static_cast<float>(site["y"]);
        // 2.f stuff is just a placeholder rn to just draw circles yah yah yah
        g.drawEllipse(x, y, 2.f, 2.f, 2.f);
    }
    
}

void VoronoiseAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
}

void VoronoiseAudioProcessorEditor::mouseDoubleClick (const juce::MouseEvent& event)
{
    // event.x and event.y
    juce::Point<float> point = juce::Point<float>(static_cast<float>(event.x),static_cast<float>(event.y));
    points.push_back(point);
    repaint();
    auto sitesTree = valueTree.getChildWithName("Sites");
        sitesTree.appendChild(
            juce::ValueTree("Site")
                .setProperty("x", event.x, nullptr)
                .setProperty("y", event.y, nullptr),
            nullptr
        );

}