#pragma once

#include <JuceHeader.h>
#include <vector>
#include <map>
#include "PluginProcessor.h"
#include "geometry/Utils.h"
#include "geometry/Voronoi.h"
#include "primitives/Ball.h"


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

    const std::vector<Ball>& getBalls() const { return balls; }
    const std::vector<GeoUtils::Edge>& getEdges() const { return edges; }

private:
    void timerCallback() override;
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree& child) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int index) override;

    // recomputes the triangulation/voronoi from the sites, only when they or the size change
    void rebuildGeometry();
    void addBall(Ball ball);

    VoronoiseAudioProcessor& processorRef;
    std::vector<GeoUtils::Point> points;
    std::vector<GeoUtils::Triangle> triangles;
    std::map<GeoUtils::Point, Voronoi::Cell, GeoUtils::PointComparator> voronoiCells;
    std::vector<GeoUtils::Edge> edges; // voronoi edges the balls bounce off
    std::vector<Ball> balls;
    juce::ValueTree valueTree;

    Mode mode = Mode::DrawSites;
    juce::TextButton drawButton { "Draw" };
    juce::TextButton ballButton { "Ball" };

    juce::Random random;
    double lastTimerMs = 0.0;

    static constexpr size_t MAX_BALLS = 200;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VoronoiseAudioProcessorEditor)
};
