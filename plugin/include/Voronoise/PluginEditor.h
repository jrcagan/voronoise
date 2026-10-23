#pragma once

#include <JuceHeader.h>
#include <vector>
#include <map>
#include "PluginProcessor.h"
#include "geometry/Utils.h"
#include "geometry/Voronoi.h"
#include "primitives/Ball.h"
#include "Voronoise/Params.h"
#include "Voronoise/SettingsPanel.h"


//==============================================================================
class VoronoiseAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                            private juce::Timer,
                                            private juce::ValueTree::Listener
{
public:
    explicit VoronoiseAudioProcessorEditor (VoronoiseAudioProcessor&);
    ~VoronoiseAudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& event) override;
    void mouseDoubleClick (const juce::MouseEvent& event) override;
    void spawnBall(int noteNumber, int velocity);
    void dropBall(juce::Point<float> position);
    void updateBalls(float dt);

    // what clicking in the window does
    enum class Mode {
        DrawSites,
        DropBalls
    };
    void setMode(Mode newMode);
    Mode getMode() const { return mode; }

    // sound new balls make when they hit an edge
    void setSound(Waveform newSound);
    Waveform getSound() const { return sound; }

    // note a hand dropped ball plays, higher on screen is higher pitch
    int getNoteForHeight(float y) const;

    // picks up setting changes and state swaps (loading a project), runs on the timer
    void syncWithProcessor();

    void setSettingsVisible(bool shouldBeVisible);
    bool isSettingsVisible() const;

    const std::vector<Ball>& getBalls() const { return balls; }
    const std::vector<GeoUtils::Edge>& getEdges() const { return edges; }

private:
    void timerCallback() override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int index) override;

    // recomputes the triangulation/voronoi from the sites, only when they or the size change
    void rebuildGeometry();
    void addBall(Ball ball);
    // size, speed, lifetime, note and colour from the settings
    void setupBall(Ball& ball, int noteNumber, float sizeScale);

    Params::Lines getLinesSetting() const;
    Ball::Physics getPhysics();

    VoronoiseAudioProcessor& processorRef;
    std::vector<GeoUtils::Point> points;
    std::vector<GeoUtils::Triangle> triangles;
    std::map<GeoUtils::Point, Voronoi::Cell, GeoUtils::PointComparator> voronoiCells;
    std::vector<GeoUtils::Edge> edges; // voronoi + delaunay edges the balls bounce off and play on
    std::vector<Ball> balls;
    juce::ValueTree valueTree;

    Mode mode = Mode::DrawSites;
    juce::TextButton drawButton { "Draw" };
    juce::TextButton ballButton { "Ball" };

    Waveform sound = Waveform::Sine;
    juce::ComboBox soundBox;
    juce::TextButton loadSampleButton { "Load sample" };
    std::unique_ptr<juce::FileChooser> fileChooser;
    void chooseSample();
    void updateSampleButtonText();

    juce::TextButton settingsButton { "Settings" };
    std::unique_ptr<Settings::SettingsPanel> settingsPanel;

    Params::Lines lines = Params::Lines::Both;

    juce::Random random;
    double lastTimerMs = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoronoiseAudioProcessorEditor)
};
