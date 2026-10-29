#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>
#include "Voronoise/PluginProcessor.h"

namespace UI
{
   // the wavetable: all the frames stacked up in 3d with the one you're hearing lit up, or in draw
   // mode one big frame you can draw on. the frame being edited is whichever one wt position is nearest
   class WavetableView : public juce::Component, private juce::Timer
   {
   public:
      explicit WavetableView(VoronoiseAudioProcessor& processor);

      void paint(juce::Graphics& g) override;
      void resized() override;
      void mouseDown(const juce::MouseEvent& e) override;
      void mouseDrag(const juce::MouseEvent& e) override;
      void mouseUp(const juce::MouseEvent& e) override;

      void setDrawMode(bool shouldDraw);
      bool isDrawMode() const { return drawMode; }

      int getEditFrame() const; // nearest frame to the wt position knob
      void goToFrame(int frame); // moves the wt position knob onto that frame
      void addFrame();           // copy of the edit frame, right after it
      void removeFrame();
      void applyPreset(Wavetable::Preset preset);
      void smoothFrame();
      void normalise();

      // draws a straight stroke into the edit frame, in component coordinates
      void drawStroke(juce::Point<float> from, juce::Point<float> to);
      void commit(); // hands the edited frame to the processor

      juce::Rectangle<float> getWaveArea() const;

   private:
      void timerCallback() override;
      void showTableMenu();
      void chooseAudio();
      void updateHeader();

      VoronoiseAudioProcessor& processor;

      juce::TextButton drawButton { "DRAW" }, previousButton { "<" }, nextButton { ">" },
                       addButton { "+" }, removeButton { "-" }, tableButton { "TABLE" };
      juce::Label frameLabel;
      std::unique_ptr<juce::FileChooser> fileChooser;
      juce::Random random;

      bool drawMode = false;
      std::vector<float> editing; // edit frame while drawing
      int editingFrame = -1;
      bool dirty = false;
      double lastCommitMs = 0.0;
      juce::Point<float> lastDrawPoint;

      float shownPosition = -1.f; // modulated position last painted
      Wavetable* shownTable = nullptr;
   };
}
