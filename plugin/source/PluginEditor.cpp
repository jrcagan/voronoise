#include "Voronoise/PluginEditor.h"
#include "geometry/Delaunay.h"
#include "geometry/Voronoi.h"
#include <set>

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

    // combo box ids start at 1, so id = waveform + 1
    soundBox.addItemList(getWaveformNames(), 1);
    soundBox.setSelectedId(static_cast<int>(sound) + 1, juce::dontSendNotification);
    soundBox.onChange = [this] { setSound(static_cast<Waveform>(soundBox.getSelectedId() - 1)); };
    addAndMakeVisible(soundBox);

    loadSampleButton.onClick = [this] { chooseSample(); };
    addChildComponent(loadSampleButton); // only shown when "Sample" is picked
    updateSampleButtonText();

    settingsPanel = std::make_unique<Settings::SettingsPanel>(processorRef, [this] { chooseSample(); });
    addChildComponent(*settingsPanel);

    settingsButton.setClickingTogglesState(true);
    settingsButton.onClick = [this] { setSettingsVisible(settingsButton.getToggleState()); };
    addAndMakeVisible(settingsButton);

    // sites can change from outside the editor too (e.g. loading state)
    valueTree.addListener(this);
    lines = getLinesSetting();

    setResizable(true, true);
    setResizeLimits(480, 360, 2400, 1600);
    setSize(760, 500);

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
                         getLocalBounds().reduced(10), juce::Justification::bottomLeft, 1);

        if (lines != Params::Lines::DelaunayOnly)
        {
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
        }

        if (lines != Params::Lines::VoronoiOnly)
        {
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
    buttonArea.removeFromLeft(12);
    soundBox.setBounds(buttonArea.removeFromLeft(100));
    buttonArea.removeFromLeft(4);
    loadSampleButton.setBounds(buttonArea.removeFromLeft(120));
    settingsButton.setBounds(buttonArea.removeFromRight(80));

    // floats over the right side of the canvas, the balls keep going underneath it
    const int panelWidth = juce::jmin(380, getWidth() - 40);
    settingsPanel->setBounds(getWidth() - panelWidth - 8, 40, panelWidth, getHeight() - 48);

    // voronoi is clipped to the window so it has to be redone when the size changes
    rebuildGeometry();
}

void VoronoiseAudioProcessorEditor::setSettingsVisible(bool shouldBeVisible)
{
    settingsPanel->setVisible(shouldBeVisible);
    settingsButton.setToggleState(shouldBeVisible, juce::dontSendNotification);
}

bool VoronoiseAudioProcessorEditor::isSettingsVisible() const
{
    return settingsPanel->isVisible();
}

void VoronoiseAudioProcessorEditor::setMode(Mode newMode)
{
    mode = newMode;
    drawButton.setToggleState(mode == Mode::DrawSites, juce::dontSendNotification);
    ballButton.setToggleState(mode == Mode::DropBalls, juce::dontSendNotification);
}

void VoronoiseAudioProcessorEditor::setSound(Waveform newSound)
{
    if (newSound >= Waveform::END)
        return;

    sound = newSound;
    soundBox.setSelectedId(static_cast<int>(sound) + 1, juce::dontSendNotification);
    loadSampleButton.setVisible(sound == Waveform::Sample);
}

void VoronoiseAudioProcessorEditor::chooseSample()
{
    fileChooser = std::make_unique<juce::FileChooser>("Pick a sample", juce::File(), processorRef.getSampleWildcard());

    // async so the plugin window doesn't block the host
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [safeThis = juce::Component::SafePointer(this)](const juce::FileChooser &chooser)
                             {
                                 auto file = chooser.getResult();
                                 if (safeThis == nullptr || file == juce::File())
                                     return;

                                 if (! safeThis->processorRef.loadSample(file))
                                     juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                                            "Couldn't load sample",
                                                                            "Voronoise can't read " + file.getFileName());

                                 safeThis->updateSampleButtonText();
                             });
}

void VoronoiseAudioProcessorEditor::updateSampleButtonText()
{
    const auto name = processorRef.getSampleName();
    loadSampleButton.setButtonText(name.isEmpty() ? "Load sample" : name);
    loadSampleButton.setTooltip(name.isEmpty() ? "Pick a sound file for sample balls" : "Click to pick a different sample");
}

int VoronoiseAudioProcessorEditor::getNoteForHeight(float y) const
{
    // scale, range and probabilities all come from the notes settings
    const float height = juce::jmax(1.f, static_cast<float>(getHeight()));
    return Scale::chooseByHeight(1.f - y / height, processorRef.getNoteSettings());
}

Params::Lines VoronoiseAudioProcessorEditor::getLinesSetting() const
{
    return static_cast<Params::Lines>(static_cast<int>(processorRef.getParamValue(Params::LINES)));
}

Ball::Physics VoronoiseAudioProcessorEditor::getPhysics()
{
    Ball::Physics physics;
    physics.gravity = processorRef.getParamValue(Params::GRAVITY);
    physics.restitution = processorRef.getParamValue(Params::BOUNCINESS);
    physics.drag = processorRef.getParamValue(Params::DRAG);
    physics.passThroughChance = processorRef.getParamValue(Params::PASS_THROUGH);
    physics.passThroughPlays = processorRef.getParamValue(Params::PASS_THROUGH_PLAYS) > 0.5f;
    physics.random = &random;
    return physics;
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
    cellCentres.clear();

    // edge indices are about to change, so balls can't be halfway through any of them
    for (auto &ball : balls)
        ball.passingEdges.clear();

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

        for (const auto &kv : voronoiCells)
            cellCentres.push_back(getCentroid(kv.second.vertices));

        // balls bounce off whichever lines are switched on, the set drops duplicates
        std::set<GeoUtils::Edge, GeoUtils::EdgeComparator> uniqueEdges;
        if (lines != Params::Lines::DelaunayOnly)
        {
            for (const auto &edge : Voronoi::getEdges(triangles, bbox))
                uniqueEdges.insert(edge);
        }
        if (lines != Params::Lines::VoronoiOnly)
        {
            for (const auto &t : triangles)
            {
                uniqueEdges.insert({ t.a, t.b });
                uniqueEdges.insert({ t.b, t.c });
                uniqueEdges.insert({ t.c, t.a });
            }
        }

        // voronoi cells get clipped to the window, those border pieces are just the walls,
        // which the ball already bounces off (and plays on) by itself, so leave them out
        auto onBorder = [&bbox](const GeoUtils::Edge &e)
        {
            auto same = [](double a, double b) { return std::abs(a - b) < 1e-6; };
            return (same(e.u.x, bbox.minX) && same(e.v.x, bbox.minX)) || (same(e.u.x, bbox.maxX) && same(e.v.x, bbox.maxX))
                || (same(e.u.y, bbox.minY) && same(e.v.y, bbox.minY)) || (same(e.u.y, bbox.maxY) && same(e.v.y, bbox.maxY));
        };

        for (const auto &edge : uniqueEdges)
        {
            if (! onBorder(edge))
                edges.push_back(edge);
        }
    }

    repaint();
}

void VoronoiseAudioProcessorEditor::syncWithProcessor()
{
    // loading a project swaps the whole state out, so follow it to the new sites
    if (valueTree != processorRef.getValueTree())
    {
        valueTree.removeListener(this);
        valueTree = processorRef.getValueTree();
        valueTree.addListener(this);
        rebuildGeometry();
        updateSampleButtonText();
    }

    const auto newLines = getLinesSetting();
    if (newLines != lines)
    {
        lines = newLines;
        rebuildGeometry();
    }
}

// middle of a polygon by area, falls back to the average of the corners if it's flat
juce::Point<float> VoronoiseAudioProcessorEditor::getCentroid(const std::vector<GeoUtils::Point> &vertices)
{
    double area = 0.0, cx = 0.0, cy = 0.0;
    for (size_t i = 0; i < vertices.size(); i++)
    {
        const auto &a = vertices[i];
        const auto &b = vertices[(i + 1) % vertices.size()];
        const double cross = a.x * b.y - b.x * a.y;
        area += cross;
        cx += (a.x + b.x) * cross;
        cy += (a.y + b.y) * cross;
    }

    if (std::abs(area) < 1e-9)
    {
        GeoUtils::Point sum;
        for (const auto &v : vertices)
            sum += v;
        return (sum / static_cast<double>(std::max<size_t>(1, vertices.size()))).toFloat();
    }

    return GeoUtils::Point(cx / (3.0 * area), cy / (3.0 * area)).toFloat();
}

// runs at 60hz: picks up notes from the audio thread and moves the balls
void VoronoiseAudioProcessorEditor::timerCallback()
{
    syncWithProcessor();

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
    const auto physics = getPhysics();

    for (auto &ball : balls)
    {
        const float impact = ball.update(dt, bounds, edges, physics);

        if (ball.shouldPlayHit(impact))
        {
            processorRef.hitFifo.push({ ball.noteNumber, ball.getHitGain(impact), ball.sound });
        }
    }

    balls.erase(std::remove_if(balls.begin(), balls.end(), [](const Ball &b) { return ! b.isAlive(); }),
                balls.end());
}

void VoronoiseAudioProcessorEditor::addBall(Ball ball)
{
    // drop the oldest balls if there are too many
    const auto maxBalls = static_cast<size_t>(juce::jmax(1.f, processorRef.getParamValue(Params::MAX_BALLS)));
    while (balls.size() >= maxBalls)
        balls.erase(balls.begin());

    balls.push_back(ball);
    repaint();
}

void VoronoiseAudioProcessorEditor::setupBall(Ball &ball, int noteNumber, float sizeScale)
{
    // size with some random spread, variation 1 = anywhere from 0 to double
    const float variation = processorRef.getParamValue(Params::SIZE_VARIATION);
    const float spread = 1.f + variation * (random.nextFloat() * 2.f - 1.f);
    ball.radius = juce::jmax(1.f, processorRef.getParamValue(Params::BALL_SIZE) * sizeScale * spread);

    // launched sideways in a random direction
    const float launchSpeed = processorRef.getParamValue(Params::LAUNCH_SPEED);
    ball.velocity = { (random.nextFloat() * 2.f - 1.f) * launchSpeed, 0.f };

    ball.timeLeftAlive = processorRef.getParamValue(Params::LIFETIME);

    // each pitch class gets its own colour
    ball.noteNumber = noteNumber;
    ball.frequency = AudioUtils::getFreq(noteNumber);
    ball.color = juce::Colour::fromHSV(static_cast<float>(noteNumber % 12) / 12.f, 0.8f, 1.f, 1.f);
    ball.sound = sound;
}

// drops a ball wherever the user clicked in ball mode
void VoronoiseAudioProcessorEditor::dropBall(juce::Point<float> position)
{
    const bool randomPitch = processorRef.getParamValue(Params::PITCH_SOURCE) > 0.5f;
    const int note = randomPitch ? Scale::chooseRandom(random, processorRef.getNoteSettings())
                                 : getNoteForHeight(position.y);

    Ball ball;
    ball.setXY(position.x, position.y);
    setupBall(ball, note, 1.f);

    addBall(ball);
}

// midi notes have a chance of dropping a ball from the centre of a random voronoi cell
void VoronoiseAudioProcessorEditor::spawnBall(int noteNumber, int velocity)
{
    auto sitesTree = valueTree.getChildWithName("Sites");

    // no sites to spawn from yet
    if (sitesTree.getNumChildren() == 0)
        return;

    if (random.nextFloat() >= processorRef.getParamValue(Params::MIDI_DROP_CHANCE))
        return;

    const auto noteSettings = processorRef.getNoteSettings();
    const bool snap = processorRef.getParamValue(Params::SNAP_MIDI) > 0.5f;
    const int note = snap ? Scale::snap(noteNumber, noteSettings) : Scale::applyOctaveShift(noteNumber, noteSettings);

    Ball ball;

    if (! cellCentres.empty())
    {
        const auto centre = cellCentres[static_cast<size_t>(random.nextInt(static_cast<int>(cellCentres.size())))];
        ball.setXY(centre.x, centre.y);
    }
    else
    {
        // under 3 sites there's no voronoi yet, so use a site
        auto point = sitesTree.getChild(random.nextInt(sitesTree.getNumChildren()));
        ball.setX(static_cast<float>(point.getProperty("x")));
        ball.setY(static_cast<float>(point.getProperty("y")));
    }

    // louder notes make bigger balls
    setupBall(ball, note, 0.5f + static_cast<float>(velocity) / 127.f);
    ball.noteVelocity = static_cast<float>(velocity);

    addBall(ball);
}
