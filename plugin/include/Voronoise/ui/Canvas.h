#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>
#include "Voronoise/PluginProcessor.h"
#include "Voronoise/Params.h"
#include "world/BallWorld.h"

namespace UI
{
   // the screen inside the computer. it draws the world (the balls bounce on the audio thread)
   // and turns clicks into sites and ball drops
   class Canvas : public juce::Component, private juce::Timer
   {
   public:
      explicit Canvas(VoronoiseAudioProcessor& processor);
      ~Canvas() override;

      void paint(juce::Graphics& g) override;
      void mouseDown(const juce::MouseEvent& event) override;
      void mouseDrag(const juce::MouseEvent& event) override;
      void mouseUp(const juce::MouseEvent& event) override;
      void mouseMove(const juce::MouseEvent& event) override;
      void mouseExit(const juce::MouseEvent& event) override;
      void mouseDoubleClick(const juce::MouseEvent& event) override;

      // sites can be grabbed and dragged around in draw mode, the lines follow as it goes
      int findSiteAt(juce::Point<float> position) const; // index in the state's sites, -1 if none's there
      bool beginDrag(juce::Point<float> position);       // false if there's no site to grab
      void dragTo(juce::Point<float> position);
      void endDrag();
      bool isDragging() const { return draggedSite >= 0; }
      static constexpr float GRAB_RADIUS = 8.f;

      // these go to the audio thread and happen on its next block
      void spawnBall(int noteNumber, int velocity);
      void dropBall(juce::Point<float> position);
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

      // picks up the newest ball positions and any lines changes, runs on the timer
      void syncWithProcessor();

      const std::vector<GeoUtils::Edge>& getEdges() const { return processor.getWorld().getGeometry()->edges; }
      const std::vector<juce::Point<float>>& getCellCentres() const { return processor.getWorld().getGeometry()->cellCentres; }
      int getNumSites() const { return static_cast<int>(processor.getWorld().getGeometry()->sites.size()); }
      int getNumBalls() const { return snapshot->numBalls; }

   private:
      void timerCallback() override;

      VoronoiseAudioProcessor& processor;
      std::unique_ptr<BallWorld::Snapshot> snapshot; // too big to keep inline comfortably
      Mode mode = Mode::DrawSites;
      int draggedSite = -1;
      int hoveredSite = -1;
      juce::Point<float> grabOffset; // from the mouse to the site, so it doesn't jump to the pointer

      JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Canvas)
   };
}
