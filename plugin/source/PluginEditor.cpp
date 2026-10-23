#include "Voronoise/PluginEditor.h"
#include "geometry/Delaunay.h"
#include "geometry/Voronoi.h"

//==============================================================================
VoronoiseAudioProcessorEditor::VoronoiseAudioProcessorEditor(VoronoiseAudioProcessor &p)
    : AudioProcessorEditor(&p), processorRef(p), valueTree(p.getValueTree())
{
    // draw/ball buttons act like a two way switch
    for (auto* button : { &drawButton, &ballButton })
    {
        button->setClickingTogglesState(true);
        button->setRadioGroupId(1);
        addAndMakeVisible(button);
    }

    drawButton.onClick = [this] { if (drawButton.getToggleState()) setMode(Mode::DrawSites); };
    ballButton.onClick = [this] { if (ballButton.getToggleState()) setMode(Mode::DropBalls); };
    drawButton.setToggleState(true, juce::dontSendNotification);

    // sites can change from outside the editor too (e.g. loading state)
    valueTree.addListener(this);

    setSize(400, 300);

    lastTimerMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(60);
}

VoronoiseAudioProcessorEditor::~VoronoiseAudioProcessorEditor()
{
    stopTimer();
    valueTree.removeListener(this);
}

//==============================================================================
void VoronoiseAudioProcessorEditor::paint(juce::Graphics &g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
    g.setColour(juce::Colours::white);
    g.setFont(15.0f);

    for (const auto &point : points)
    {
        g.drawEllipse(static_cast<float>(point.getX()) - 2.f, static_cast<float>(point.getY()) - 2.f, 4.f, 4.f, 1.5f);
    }

    if (points.size() > 2)
    {
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

    for (const auto &ball : balls)
    {
        g.setColour(ball.color.withMultipliedAlpha(ball.getAlpha()));
        g.fillEllipse(ball.getX() - ball.radius, ball.getY() - ball.radius, ball.radius * 2.f, ball.radius * 2.f);
    }
}

void VoronoiseAudioProcessorEditor::resized()
{
    auto buttonArea = getLocalBounds().reduced(8).removeFromTop(24);
    drawButton.setBounds(buttonArea.removeFromLeft(50));
    buttonArea.removeFromLeft(4);
    ballButton.setBounds(buttonArea.removeFromLeft(50));

    // voronoi is clipped to the window so it has to be redone when the size changes
    rebuildGeometry();
}

void VoronoiseAudioProcessorEditor::setMode(Mode newMode)
{
    mode = newMode;
    drawButton.setToggleState(mode == Mode::DrawSites, juce::dontSendNotification);
    ballButton.setToggleState(mode == Mode::DropBalls, juce::dontSendNotification);
}

void VoronoiseAudioProcessorEditor::mouseDown(const juce::MouseEvent &event)
{
    if (mode == Mode::DropBalls)
    {
        dropBall(event.position);
    }
}

void VoronoiseAudioProcessorEditor::mouseDoubleClick(const juce::MouseEvent &event)
{
    if (mode != Mode::DrawSites)
        return;

    // the listener picks this up and rebuilds the voronoi
    auto sitesTree = valueTree.getChildWithName("Sites");
    sitesTree.appendChild(
        juce::ValueTree("Site")
            .setProperty("x", static_cast<double>(event.x), nullptr)
            .setProperty("y", static_cast<double>(event.y), nullptr),
        nullptr);
}

void VoronoiseAudioProcessorEditor::valueTreeChildAdded(juce::ValueTree &, juce::ValueTree &)
{
    rebuildGeometry();
}

void VoronoiseAudioProcessorEditor::valueTreeChildRemoved(juce::ValueTree &, juce::ValueTree &, int)
{
    rebuildGeometry();
}

void VoronoiseAudioProcessorEditor::rebuildGeometry()
{
    auto sitesTree = valueTree.getChildWithName("Sites");
    points.clear();

    for (int i = 0; i < sitesTree.getNumChildren(); i++)
    {
        auto site = sitesTree.getChild(i);
        points.push_back(GeoUtils::Point(static_cast<double>(site["x"]), static_cast<double>(site["y"])));
    }

    triangles.clear();
    voronoiCells.clear();
    edges.clear();

    if (points.size() > 2)
    {
        triangles = Delaunay::triangulate(points);

        GeoUtils::BBox bbox;
        auto box = getLocalBounds().toDouble();
        bbox.minX = box.getX();
        bbox.minY = box.getY();
        bbox.maxX = box.getRight();
        bbox.maxY = box.getBottom();

        voronoiCells = Voronoi::getCells(triangles, bbox);
        edges = Voronoi::getEdges(triangles, bbox);
    }

    repaint();
}

// runs at 60hz: picks up notes from the audio thread and moves the balls
void VoronoiseAudioProcessorEditor::timerCallback()
{
    VoronoiseAudioProcessor::NoteEvent event;

    while (processorRef.noteEventFifo.pull(event))
    {
        spawnBall(event.noteNumber, event.velocity);
    }

    const auto now = juce::Time::getMillisecondCounterHiRes();
    // cap dt so a stall (window dragged, debugger) doesn't launch the balls through everything
    const auto dt = juce::jmin(static_cast<float>((now - lastTimerMs) / 1000.0), 1.f / 30.f);
    lastTimerMs = now;

    if (! balls.empty())
    {
        updateBalls(dt);
        repaint();
    }
}

void VoronoiseAudioProcessorEditor::updateBalls(float dt)
{
    const auto bounds = getLocalBounds().toFloat();

    for (auto &ball : balls)
    {
        ball.update(dt, bounds, edges);
    }

    balls.erase(std::remove_if(balls.begin(), balls.end(), [](const Ball &b) { return ! b.isAlive(); }),
                balls.end());
}

void VoronoiseAudioProcessorEditor::addBall(Ball ball)
{
    // drop the oldest ball if there are too many
    if (balls.size() >= MAX_BALLS)
        balls.erase(balls.begin());

    balls.push_back(ball);
    repaint();
}

// drops a ball wherever the user clicked in ball mode
void VoronoiseAudioProcessorEditor::dropBall(juce::Point<float> position)
{
    Ball ball;
    ball.setXY(position.x, position.y);
    ball.radius = 6.f;
    ball.color = juce::Colours::orange;

    addBall(ball);
}

// spawns a ball at a random site anytime a midi note comes in
void VoronoiseAudioProcessorEditor::spawnBall(int noteNumber, int velocity)
{
    auto sitesTree = valueTree.getChildWithName("Sites");

    // no sites to spawn from yet
    if (sitesTree.getNumChildren() == 0)
        return;

    Ball ball;

    auto point = sitesTree.getChild(random.nextInt(sitesTree.getNumChildren()));
    ball.setX(static_cast<float>(point.getProperty("x")));
    ball.setY(static_cast<float>(point.getProperty("y")));

    // louder notes make bigger balls, and each pitch class gets its own colour
    ball.radius = 3.f + 7.f * static_cast<float>(velocity) / 127.f;
    ball.color = juce::Colour::fromHSV(static_cast<float>(noteNumber % 12) / 12.f, 0.8f, 1.f, 1.f);
    ball.velocity = { random.nextFloat() * 200.f - 100.f, 0.f };
    ball.noteVelocity = static_cast<float>(velocity);
    ball.frequency = AudioUtils::getFreq(noteNumber);

    addBall(ball);
}
