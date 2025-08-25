#include "Voronoise/PluginEditor.h"
#include "geometry/Delaunay.h"
#include "geometry/Voronoi.h"
// Note: Utils.h is included via the other headers

//==============================================================================
VoronoiseAudioProcessorEditor::VoronoiseAudioProcessorEditor(VoronoiseAudioProcessor &p)
    : AudioProcessorEditor(&p), processorRef(p), valueTree(p.getValueTree())
{
    juce::ignoreUnused(processorRef);
    setSize(400, 300);
}

VoronoiseAudioProcessorEditor::~VoronoiseAudioProcessorEditor()
{
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
