#pragma once

#include <JuceHeader.h>
#include <array>
#include <functional>
#include <memory>
#include "Voronoise/PluginProcessor.h"
#include "Voronoise/ui/ParamControl.h"

namespace UI
{
   // draw the lfo shape: drag points, drag the little handles between them to bend the curve,
   // double click to add or remove points
   class LfoShapeEditor : public juce::Component, private juce::Timer
   {
   public:
      explicit LfoShapeEditor(VoronoiseAudioProcessor& processor);

      void setLfo(int newLfo);
      void setGridDivisions(int divisions); // 0 = no snapping
      const LfoShape& getShape() const { return shape; }

      void paint(juce::Graphics& g) override;
      void mouseDown(const juce::MouseEvent& e) override;
      void mouseDrag(const juce::MouseEvent& e) override;
      void mouseUp(const juce::MouseEvent& e) override;
      void mouseDoubleClick(const juce::MouseEvent& e) override;
      void mouseMove(const juce::MouseEvent& e) override;

      // these are public so tests can drive the editor without a mouse
      juce::Point<float> toScreen(float x, float y) const;
      juce::Point<float> fromScreen(juce::Point<float> position) const;
      juce::Point<float> getCurveHandle(int segment) const;
      int findPoint(juce::Point<float> position) const;
      int findCurveHandle(juce::Point<float> position) const;

   private:
      void timerCallback() override;
      void commit();
      float snapX(float x) const;
      juce::Rectangle<float> getGraphArea() const;

      VoronoiseAudioProcessor& processor;
      LfoShape shape;
      int lfo = 0;
      int gridDivisions = 8;

      enum class Dragging {
         Nothing,
         Point,
         Curve
      };
      Dragging dragging = Dragging::Nothing;
      int dragIndex = -1;
      float dragStartCurve = 0.f;
      int hoverPoint = -1;
      int hoverCurve = -1;
   };

   // tab for one lfo, click to edit it, drag it onto a control to modulate that control
   class LfoTab : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
   {
   public:
      LfoTab(VoronoiseAudioProcessor& processor, int lfo);

      std::function<void(int)> onSelect;
      void setSelected(bool shouldBeSelected);

      void paint(juce::Graphics& g) override;
      void mouseDown(const juce::MouseEvent& e) override;
      void mouseDrag(const juce::MouseEvent& e) override;

   private:
      void timerCallback() override;

      VoronoiseAudioProcessor& processor;
      int lfo;
      bool selected = false;
      bool dragStarted = false;
   };

   // everything one lfo is routed to, with a depth slider for each
   class DestinationList : public juce::Component, private juce::Timer
   {
   public:
      explicit DestinationList(VoronoiseAudioProcessor& processor);

      void setLfo(int newLfo);
      void paint(juce::Graphics& g) override;
      void resized() override;
      int getNumRows() const { return rows.size(); }

   private:
      struct Row : public juce::Component
      {
         Row(VoronoiseAudioProcessor& processor, int lfoIndex, const juce::String& paramID, float depth);
         void resized() override;
         void paint(juce::Graphics& g) override;

         juce::String paramID;
         juce::Label name;
         juce::Slider depth { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
         juce::TextButton remove { "x" };
      };

      void timerCallback() override;
      void rebuild();

      VoronoiseAudioProcessor& processor;
      int lfo = 0;
      int seenVersion = -1;
      juce::OwnedArray<Row> rows;
      juce::Viewport viewport;
      juce::Component content;
   };

   // the whole lfo deck: tabs, shape editor, rate/mode knobs and destinations
   class LfoPanel : public juce::Component, private juce::Timer
   {
   public:
      explicit LfoPanel(VoronoiseAudioProcessor& processor);

      void selectLfo(int lfo);
      int getSelectedLfo() const { return selectedLfo; }
      LfoShapeEditor& getEditor() { return editor; }
      DestinationList& getDestinations() { return destinations; }

      void paint(juce::Graphics& g) override;
      void resized() override;

   private:
      void timerCallback() override;
      void buildControls();

      VoronoiseAudioProcessor& processor;
      int selectedLfo = 0;

      std::array<std::unique_ptr<LfoTab>, Params::NUM_LFOS> tabs;
      LfoShapeEditor editor;
      DestinationList destinations;
      juce::ComboBox presetBox;
      juce::ComboBox gridBox;
      juce::Random random;

      std::unique_ptr<ParamControl> rate, sync, division, mode, phase, smooth;
   };
}
