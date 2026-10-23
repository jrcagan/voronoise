#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <vector>
#include "PluginProcessor.h"

namespace Settings
{
   // one label + control hooked up to a parameter, the control depends on the parameter type
   class ParamRow : public juce::Component
   {
   public:
      ParamRow(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID);
      void resized() override;

   private:
      juce::Label label;
      std::unique_ptr<juce::Slider> slider;
      std::unique_ptr<juce::ComboBox> comboBox;
      std::unique_ptr<juce::ToggleButton> toggle;

      std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
      std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
      std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
   };

   // a column of rows and headings, sized to fit so it can sit in a viewport
   class Page : public juce::Component
   {
   public:
      void addHeading(const juce::String& text);
      void addParams(juce::AudioProcessorValueTreeState& apvts, std::initializer_list<juce::String> paramIDs);
      void addComponent(std::unique_ptr<juce::Component> component, int height);

      int getContentHeight() const;
      void resized() override;

      static constexpr int ROW_HEIGHT = 28;

   private:
      std::vector<std::pair<std::unique_ptr<juce::Component>, int>> rows;
   };

   // the 12 note probability sliders, notes outside the scale are dimmed
   class NoteWeights : public juce::Component, private juce::Timer
   {
   public:
      explicit NoteWeights(VoronoiseAudioProcessor& processor);
      void paint(juce::Graphics& g) override;
      void resized() override;

   private:
      void timerCallback() override;

      VoronoiseAudioProcessor& processor;
      std::array<juce::Slider, 12> sliders;
      std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 12> attachments;
      std::array<bool, 12> inScale {};
   };

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
      juce::TextButton loadButton { "Load..." };
      SampleData::Ptr sample;

      juce::RangedAudioParameter* startParam = nullptr;
      juce::RangedAudioParameter* endParam = nullptr;
      juce::RangedAudioParameter* dragging = nullptr;

      // what was drawn last time, to only repaint when something changes
      std::vector<int> boundaries;
   };

   class SettingsPanel : public juce::Component
   {
   public:
      SettingsPanel(VoronoiseAudioProcessor& processor, std::function<void()> onLoadSample);
      void paint(juce::Graphics& g) override;
      void resized() override;

   private:
      void addPage(const juce::String& name, std::unique_ptr<Page> page);

      // pages first so they outlive the viewports the tabs own
      std::vector<std::unique_ptr<Page>> pages;
      juce::TabbedComponent tabs { juce::TabbedButtonBar::TabsAtTop };
   };
}
