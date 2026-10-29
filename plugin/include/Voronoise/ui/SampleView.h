#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>
#include "Voronoise/PluginProcessor.h"

namespace UI
{
   // waveform of the loaded sample with the region and slices drawn on top,
   // drag the edges of the region to move the start and end
   class SampleView : public juce::Component, private juce::Timer
   {
   public:
      SampleView(VoronoiseAudioProcessor& processor, std::function<void()> onLoadClicked);

      void paint(juce::Graphics& g) override;
      void resized() override;
      void mouseDown(const juce::MouseEvent& event) override;
      void mouseDrag(const juce::MouseEvent& event) override;
      void mouseUp(const juce::MouseEvent& event) override;
      void mouseMove(const juce::MouseEvent& event) override;

   private:
      void timerCallback() override;
      juce::Rectangle<float> getWaveArea() const;
      float xToProportion(float x) const;
      float proportionToX(float proportion) const;

      VoronoiseAudioProcessor& processor;
      juce::TextButton loadButton { "LOAD" };
      SampleData::Ptr sample;

      juce::RangedAudioParameter* startParam = nullptr;
      juce::RangedAudioParameter* endParam = nullptr;
      juce::RangedAudioParameter* dragging = nullptr;

      // what was drawn last time, to only repaint when something changes
      std::vector<int> boundaries;
   };
}
