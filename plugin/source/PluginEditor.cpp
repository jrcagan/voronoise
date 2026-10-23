#include "Voronoise/PluginEditor.h"
#include "geometry/Delaunay.h"
#include "geometry/Voronoi.h"

//==============================================================================
VoronoiseAudioProcessorEditor::VoronoiseAudioProcessorEditor(VoronoiseAudioProcessor &p)
    : AudioProcessorEditor(&p), processorRef(p), valueTree(p.getValueTree())
{
    setSize(400, 300);
    startTimerHz(60);
}

VoronoiseAudioProcessorEditor::~VoronoiseAudioProcessorEditor()
{
    stopTimer();
}

//==============================================================================
void VoronoiseAudioProcessorEditor::paint(juce::Graphics &g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(juce::Colours::white);
    g.setFont(15.0f);

    auto sitesTree = valueTree.getChildWithName("Sites");
    points.clear();

    for (int i = 0; i < sitesTree.getNumChildren(); i++)
    {
        auto site = sitesTree.getChild(i);
        double x = static_cast<double>(site["x"]);
        double y = static_cast<double>(site["y"]);

        points.push_back(GeoUtils::Point(x, y));

        g.drawEllipse(static_cast<float>(x) - 2.f, static_cast<float>(y) - 2.f, 4.f, 4.f, 1.5f);
    }

    if (points.size() > 2)
    {
        std::vector<GeoUtils::Triangle> triangles = Delaunay::triangulate(points);

        GeoUtils::BBox bbox;
        auto box = getLocalBounds().toDouble();
        bbox.minX = box.getX();
        bbox.minY = box.getY();
        bbox.maxX = box.getRight();
        bbox.maxY = box.getBottom();

        auto voronoiCells = Voronoi::getCells(triangles, bbox);

        g.drawFittedText("Points: " + juce::String(points.size()) + ", Triangles: " + juce::String(triangles.size()),
                         getLocalBounds().reduced(10), juce::Justification::topRight, 1);

        g.setColour(juce::Colours::aqua);

        for (const auto &kv : voronoiCells)
        {
            const auto &cell = kv.second;
            const auto &verts = cell.vertices;
            if (verts.size() < 2)
                continue;

            for (size_t i = 0; i < verts.size(); ++i)
            {
                g.drawLine((float)verts[i].getX(), (float)verts[i].getY(),
                           (float)verts[(i + 1) % verts.size()].getX(), (float)verts[(i + 1) % verts.size()].getY(), 1.5f);
            }
        }

        g.setColour(juce::Colours::grey);

        for (const auto &t : triangles)
        {
            g.drawLine(static_cast<float>(t.a.getX()), static_cast<float>(t.a.getY()),
                       static_cast<float>(t.b.getX()), static_cast<float>(t.b.getY()), 1.5f);
            g.drawLine(static_cast<float>(t.b.getX()), static_cast<float>(t.b.getY()),
                       static_cast<float>(t.c.getX()), static_cast<float>(t.c.getY()), 1.5f);
            g.drawLine(static_cast<float>(t.c.getX()), static_cast<float>(t.c.getY()),
                       static_cast<float>(t.a.getX()), static_cast<float>(t.a.getY()), 1.5f);
        }
    }
}

void VoronoiseAudioProcessorEditor::resized()
{
}

void VoronoiseAudioProcessorEditor::mouseDoubleClick(const juce::MouseEvent &event)
{
    GeoUtils::Point point(event.x, event.y);
    points.push_back(point);
    repaint();

    auto sitesTree = valueTree.getChildWithName("Sites");
    sitesTree.appendChild(
        juce::ValueTree("Site")
            .setProperty("x", static_cast<double>(event.x), nullptr)
            .setProperty("y", static_cast<double>(event.y), nullptr),
        nullptr);
}

// spawns a ball for every note-on the audio thread has queued up since the last tick
void VoronoiseAudioProcessorEditor::timerCallback()
{
    VoronoiseAudioProcessor::NoteEvent event;

    while (processorRef.noteEventFifo.pull(event))
    {
        spawnBall(event.noteNumber, event.velocity);
    }
}

void VoronoiseAudioProcessorEditor::spawnBall(int noteNumber, int velocity)
{
    Ball ball;

    auto sitesTree = valueTree.getChildWithName("Sites");

    // no sites to spawn from yet (and % 0 would crash)
    if (sitesTree.getNumChildren() == 0)
        return;

    int idx = std::rand() % sitesTree.getNumChildren();

    auto point = sitesTree.getChild(idx);
    ball.setX(static_cast<float>(point.getProperty("x")));
    ball.setY(static_cast<float>(point.getProperty("y")));
    ball.radius = 2.0;
    // now write code to actually have the ball contain the specified note number and velocity 
    ball.velocity = static_cast<float>(velocity);
    ball.frequency = AudioUtils::getFreq(noteNumber);
}



