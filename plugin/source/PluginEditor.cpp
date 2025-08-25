#include "Voronoise/PluginEditor.h"
#include "geometry/Delaunay.h"
#include "geometry/Voronoi.h"

//==============================================================================
VoronoiseAudioProcessorEditor::VoronoiseAudioProcessorEditor(VoronoiseAudioProcessor &p)
    : AudioProcessorEditor(&p), processorRef(p), valueTree(p.getValueTree())
{
    juce::ignoreUnused(processorRef);
    // Make sure that before the constructor has finished, you've set the
    // editor's size to whatever you need it to be.
    setSize(400, 300);
}

VoronoiseAudioProcessorEditor::~VoronoiseAudioProcessorEditor()
{
}

//==============================================================================
void VoronoiseAudioProcessorEditor::paint(juce::Graphics &g)
{
    // (Our component is opaque, so we must completely fill the background with a solid colour)
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

        points.push_back(Delaunay::Point(x, y));

        // Draw circles for each point
        g.drawEllipse(static_cast<float>(x) - 2.f, static_cast<float>(y) - 2.f, 4.f, 4.f, 1.5f);
    }

    if (points.size() > 2)
    {
        if (points.size() > 2)
        {
            std::vector<Delaunay::Triangle> triangles = Delaunay::triangulate(points);

            std::vector<Delaunay::Edge> voronoiEdges = Voronoi::getEdges(triangles);

            Delaunay::BBox bbox;
            auto box = g.getClipBounds();
            bbox.maxY = box.getTopLeft().getY();
            bbox.minY = box.getBottomLeft().getY();
            bbox.maxX = box.getTopRight().getX();
            bbox.minX = box.getBottomLeft().getX();

            auto voronoiCells = Voronoi::getCells(triangles, bbox);

            g.drawFittedText("Points: " + juce::String(points.size()) + ", Triangles: " + juce::String(triangles.size()),
                             getLocalBounds().reduced(10), juce::Justification::topRight, 1);

            g.setColour(juce::Colours::aqua);

            /*
            for (const auto e : voronoiEdges)
            {
                g.drawLine(static_cast<float>(e.u.getX()), static_cast<float>(e.u.getY()),
                           static_cast<float>(e.v.getX()), static_cast<float>(e.v.getY()), 1.5f);
            }
            */


            for (const auto &kv : voronoiCells)
            {
                const auto &cell = kv.second;
                const auto &verts = cell.vertices;
                if (verts.size() < 2)
                    continue;

                for (size_t i = 1; i < verts.size(); ++i)
                {
                    g.drawLine((float)verts[i - 1].getX(), (float)verts[i - 1].getY(),
                               (float)verts[i].getX(), (float)verts[i].getY(), 1.5f);
                }
                if (cell.closed && verts.size() > 2)
                {
                    g.drawLine((float)verts.back().getX(), (float)verts.back().getY(),
                               (float)verts.front().getX(), (float)verts.front().getY(), 1.5f);
                }
            }

            g.setColour(juce::Colours::grey);

            for (const auto &t : triangles)
            {
                // FIXED: Cast all double coordinates to float for the JUCE drawing API
                g.drawLine(static_cast<float>(t.a.getX()), static_cast<float>(t.a.getY()),
                           static_cast<float>(t.b.getX()), static_cast<float>(t.b.getY()), 1.5f);

                g.drawLine(static_cast<float>(t.b.getX()), static_cast<float>(t.b.getY()),
                           static_cast<float>(t.c.getX()), static_cast<float>(t.c.getY()), 1.5f);

                g.drawLine(static_cast<float>(t.c.getX()), static_cast<float>(t.c.getY()),
                           static_cast<float>(t.a.getX()), static_cast<float>(t.a.getY()), 1.5f);
            }
        }
    }
}
void VoronoiseAudioProcessorEditor::resized()
{
    // This is generally where you'll want to lay out the positions of any
    // subcomponents in your editor..
}

void VoronoiseAudioProcessorEditor::mouseDoubleClick(const juce::MouseEvent &event)
{
    // event.x and event.y
    Point point = Point(event.x, event.y);
    points.push_back(point);
    repaint();
    auto sitesTree = valueTree.getChildWithName("Sites");
    sitesTree.appendChild(
        juce::ValueTree("Site")
            .setProperty("x", static_cast<double>(event.x), nullptr)
            .setProperty("y", static_cast<double>(event.y), nullptr),
        nullptr);
}