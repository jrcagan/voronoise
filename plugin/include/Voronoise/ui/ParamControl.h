#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <vector>
#include "Voronoise/PluginProcessor.h"

namespace UI
{
   // something on the panel that shows the last touched setting (the lcd in the header)
   struct ParamDisplay
   {
      virtual ~ParamDisplay() = default;
      virtual void showParameter(const juce::String& name, const juce::String& value) = 0;
   };

   // lfo tabs drag a description like "lfo:0" onto controls
   bool getDraggedLfo(const juce::var& description, int& lfo);

   // slider that hands right clicks to its owner instead of starting a drag
   class ModSlider : public juce::Slider
   {
   public:
      using juce::Slider::Slider;
      std::function<void()> onRightClick;

      void mouseDown(const juce::MouseEvent& e) override;
      void mouseDrag(const juce::MouseEvent& e) override;
      void mouseUp(const juce::MouseEvent& e) override;

   private:
      bool rightClicking = false;
   };

   // one setting on the panel. picks a knob / switch / led button / menu / fader from the parameter type,
   // takes lfo drops, and draws a ring per lfo routed to it
   class ParamControl : public juce::Component, public juce::DragAndDropTarget, public juce::SettableTooltipClient, private juce::Timer
   {
   public:
      enum class Style {
         Auto,
         Knob,
         Toggle, // led button with the name on it
         Switch, // a row of buttons, one per choice
         Menu,
         Fader
      };

      ParamControl(VoronoiseAudioProcessor& processor, const juce::String& paramID,
                   const juce::String& displayName = {}, Style style = Style::Auto);
      ~ParamControl() override;

      const juce::String& getParamID() const { return paramID; }
      Style getStyle() const { return style; }
      juce::OwnedArray<juce::TextButton>& getSwitchButtons() { return switchButtons; }

      void paint(juce::Graphics& g) override;
      void paintOverChildren(juce::Graphics& g) override;
      void resized() override;
      void mouseDown(const juce::MouseEvent& e) override;
      void mouseEnter(const juce::MouseEvent& e) override;
      void mouseExit(const juce::MouseEvent& e) override;

      bool isInterestedInDragSource(const SourceDetails& details) override;
      void itemDragEnter(const SourceDetails& details) override;
      void itemDragExit(const SourceDetails& details) override;
      void itemDropped(const SourceDetails& details) override;

      static constexpr int LABEL_HEIGHT = 16;

   private:
      void timerCallback() override;
      void showMenu();
      void refreshModulations();
      void announceValue();
      void updateSwitchButtons(int selected);

      VoronoiseAudioProcessor& processor;
      juce::String paramID;
      juce::String displayName;
      Style style;
      juce::RangedAudioParameter* param = nullptr;

      std::unique_ptr<ModSlider> slider;
      std::unique_ptr<juce::TextButton> toggle;
      std::unique_ptr<juce::ComboBox> menu;
      juce::OwnedArray<juce::TextButton> switchButtons;

      std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
      std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
      std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> menuAttachment;
      std::unique_ptr<juce::ParameterAttachment> switchAttachment;

      std::vector<VoronoiseAudioProcessor::Modulation> modulations;
      int seenModulationVersion = -1;
      int dragHoverLfo = -1;
      bool hovering = false;
   };
}
