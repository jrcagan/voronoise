#pragma once

#include <JuceHeader.h>
#include <map>
#include <vector>
#include "Voronoise/PluginProcessor.h"
#include "Voronoise/Params.h"
#include "geometry/Utils.h"
#include "geometry/Voronoi.h"
#include "primitives/Ball.h"

namespace UI
{
   // the screen inside the computer: sites, voronoi/delaunay lines and the bouncing balls
   class Canvas : public juce::Component, private juce::Timer, private juce::ValueTree::Listener
   {
   public:
      explicit Canvas(VoronoiseAudioProcessor& processor);
      ~Canvas() override;

      void paint(juce::Graphics& g) override;
      void resized() override;
      void mouseDown(const juce::MouseEvent& event) override;
      void mouseDoubleClick(const juce::MouseEvent& event) override;

      void spawnBall(int noteNumber, int velocity);
      void dropBall(juce::Point<float> position);
      void updateBalls(float dt);
      void clearBalls();
      void clearSites();

      // what clicking on the screen does
      enum class Mode {
         DrawSites,
         DropBalls
      };
      void setMode(Mode newMode);
      Mode getMode() const { return mode; }

      // sound new balls make when they hit an edge (it's a parameter so lfos can change it too)
      void setSound(Waveform newSound);
      Waveform getSound() const;

      // note a hand dropped ball plays, higher on screen is higher pitch
      int getNoteForHeight(float y) const;

      // picks up setting changes and state swaps (loading a project), runs on the timer
      void syncWithProcessor();

      const std::vector<Ball>& getBalls() const { return balls; }
      const std::vector<GeoUtils::Edge>& getEdges() const { return edges; }
      const std::vector<juce::Point<float>>& getCellCentres() const { return cellCentres; }
      int getNumSites() const { return static_cast<int>(points.size()); }

   private:
      void timerCallback() override;
      void valueTreeChildAdded(juce::ValueTree& parent, juce::ValueTree& child) override;
      void valueTreeChildRemoved(juce::ValueTree& parent, juce::ValueTree& child, int index) override;

      // recomputes the triangulation/voronoi from the sites, only when they or the size change
      void rebuildGeometry();
      void addBall(Ball ball);
      // size, speed, lifetime, note and colour from the settings
      void setupBall(Ball& ball, int noteNumber, float sizeScale);

      Params::Lines getLinesSetting() const;
      Ball::Physics getPhysics();
      static juce::Point<float> getCentroid(const std::vector<GeoUtils::Point>& vertices);

      VoronoiseAudioProcessor& processor;
      std::vector<GeoUtils::Point> points;
      std::vector<GeoUtils::Triangle> triangles;
      std::map<GeoUtils::Point, Voronoi::Cell, GeoUtils::PointComparator> voronoiCells;
      std::vector<GeoUtils::Edge> edges; // voronoi + delaunay edges the balls bounce off and play on
      std::vector<juce::Point<float>> cellCentres; // where midi balls drop from
      std::vector<Ball> balls;
      juce::ValueTree valueTree;

      Mode mode = Mode::DrawSites;
      Params::Lines lines = Params::Lines::Both;

      juce::Random random;
      double lastTimerMs = 0.0;

      JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Canvas)
   };
}
