#include "Voronoise/PluginEditor.h"
#include "geometry/Delaunay.h"

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
        Point p;

        auto site = sitesTree.getChild(i);
        double x = static_cast<double>(site["x"]);
        double y = static_cast<double>(site["y"]);
        p.setX(x);
        p.setY(y);

        // 2.0 stuff is just a placeholder rn to just draw circles yah yah yah
        g.drawEllipse(static_cast<float>(x), static_cast<float>(y), 2.f, 2.f, 2.f);
        points.push_back(p);
    }

    if (points.size() > 2) {
        std::vector<Delaunay::Triangle> triangles = Delaunay::triangulate(points);
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
    Point point = Point(event.x,event.y);
    points.push_back(point);
    repaint();
    auto sitesTree = valueTree.getChildWithName("Sites");
        sitesTree.appendChild(
            juce::ValueTree("Site")
                .setProperty("x", static_cast<double>(event.x), nullptr)
                .setProperty("y", static_cast<double>(event.y), nullptr),
            nullptr
        );

}