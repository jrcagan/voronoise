#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <vector>
#include "Voronoise/PluginProcessor.h"
#include "Voronoise/ui/Canvas.h"
#include "Voronoise/ui/LfoPanel.h"
#include "Voronoise/ui/ParamControl.h"
#include "Voronoise/ui/SampleView.h"
#include "Voronoise/ui/WavetableView.h"

namespace UI
{
   // the little computer the balls live in: crt screen, status line and function keys
   class Computer : public juce::Component, private juce::Timer
   {
   public:
      Computer(VoronoiseAudioProcessor& processor, std::function<void()> onLoadSample);

      Canvas& getCanvas() { return canvas; }

      void paint(juce::Graphics& g) override;
      void paintOverChildren(juce::Graphics& g) override;
      void resized() override;

      static constexpr int STATUS_HEIGHT = 20;

   private:
      void timerCallback() override;
      juce::Rectangle<int> getGlassArea() const;

      Canvas canvas;
      juce::TextButton drawKey { "DRAW" }, ballKey { "BALL" }, clearBallsKey { "CLR BALLS" },
                       clearSitesKey { "CLR SITES" }, loadKey { "LOAD SMP" };
      bool cursorOn = true;
      int ticks = 0;
   };

   // a box of controls with a title, laid out in rows
   class Section : public juce::Component
   {
   public:
      explicit Section(const juce::String& title);

      // each row is a list of controls with a relative width (0 = gap) and its height
      struct Item {
         juce::Component* component = nullptr;
         float width = 1.f;
      };
      void addRow(std::vector<Item> items, int height);
      ParamControl* add(std::unique_ptr<ParamControl> control);
      juce::Component* addOwned(std::unique_ptr<juce::Component> component);

      void paint(juce::Graphics& g) override;
      void resized() override;

      static constexpr int TITLE_HEIGHT = 26;

   private:
      juce::String title;
      std::vector<std::pair<std::vector<Item>, int>> rows;
      std::vector<std::unique_ptr<juce::Component>> owned;
   };

   // picture of the amp envelope that follows the knobs (and any lfos on them)
   // and lets you drag its points: peak = attack, decay corner = decay + sustain,
   // note off = sustain, end = release
   class EnvelopeDisplay : public juce::Component, private juce::Timer
   {
   public:
      explicit EnvelopeDisplay(VoronoiseAudioProcessor& processor);
      void paint(juce::Graphics& g) override;

      void mouseMove(const juce::MouseEvent& e) override;
      void mouseExit(const juce::MouseEvent& e) override;
      void mouseDown(const juce::MouseEvent& e) override;
      void mouseDrag(const juce::MouseEvent& e) override;
      void mouseUp(const juce::MouseEvent& e) override;
      void mouseDoubleClick(const juce::MouseEvent& e) override;

      enum class Handle {
         None,
         Peak,
         Decay,
         NoteOff,
         End
      };

      // public so tests can drag points without a mouse
      Handle findHandle(juce::Point<float> position) const;
      juce::Point<float> getHandlePosition(Handle handle) const;
      void beginDrag(Handle handle);
      void dragBy(float dx, float dy);
      void endDrag();
      void resetHandle(Handle handle);
      void refresh() { timerCallback(); } // re-read the knobs now instead of on the next timer tick

      static constexpr float PIXELS_PER_DOUBLING = 25.f; // how far to drag to double or halve a time

      // share of the graph's width each stage gets at its longest (they add up to 1)
      static constexpr float ATTACK_WIDTH = 0.24f;
      static constexpr float DECAY_WIDTH = 0.24f;
      static constexpr float HOLD_WIDTH = 0.14f;
      static constexpr float RELEASE_WIDTH = 0.38f;

   private:
      struct Shape {
         juce::Point<float> start, peak, decay, noteOff, end;
      };
      Shape getShape() const;
      juce::Rectangle<float> getGraphArea() const;
      std::vector<juce::RangedAudioParameter*> getParamsFor(Handle handle) const;
      void setValue(const char* id, float value);
      void timerCallback() override;

      VoronoiseAudioProcessor& processor;
      std::array<float, 4> shown {}; // a, d, s, r last drawn

      Handle hovered = Handle::None;
      Handle dragging = Handle::None;
      std::array<float, 4> dragStart {}; // knob values when the drag started
   };

   // a column of components, each with a fixed height, for spots a section row can't do
   class Stack : public juce::Component
   {
   public:
      void add(juce::Component* component, int height);
      void resized() override;

   private:
      std::vector<std::pair<juce::Component*, int>> items;
   };

   // the twelve note probability faders, notes outside the scale get dimmed
   class NoteFaders : public juce::Component, private juce::Timer
   {
   public:
      explicit NoteFaders(VoronoiseAudioProcessor& processor);
      void resized() override;

   private:
      void timerCallback() override;

      VoronoiseAudioProcessor& processor;
      std::vector<std::unique_ptr<ParamControl>> faders;
      std::array<bool, 12> inScale {};
   };

   class MainPanel : public juce::Component, public ParamDisplay, private juce::Timer
   {
   public:
      explicit MainPanel(VoronoiseAudioProcessor& processor);

      // designed at this size, the editor scales it to fit the window
      static constexpr int WIDTH = 1280;
      static constexpr int HEIGHT = 1000;

      Canvas& getCanvas() { return computer.getCanvas(); }
      LfoPanel& getLfoPanel() { return lfoPanel; }
      WavetableView& getWavetableView() { return *wavetableView; }
      juce::MidiKeyboardComponent& getKeyboard() { return keyboard; }

      enum class Page {
         Synth,
         Balls,
         Notes,
         Sample
      };
      void showPage(Page page);
      Page getPage() const { return page; }

      void showParameter(const juce::String& name, const juce::String& value) override;
      juce::String getDisplayText() const;

      void paint(juce::Graphics& g) override;
      void resized() override;

   private:
      void timerCallback() override;
      void chooseSample();
      void buildSections();

      VoronoiseAudioProcessor& processor;
      Computer computer;
      LfoPanel lfoPanel;
      juce::MidiKeyboardComponent keyboard;

      std::unique_ptr<Section> geometry, midi, hitRandom;
      std::unique_ptr<Section> synthPage, ballsPage, notesPage, samplePage;
      WavetableView* wavetableView = nullptr; // owned by the synth page
      juce::TextButton synthTab { "SYNTH" }, ballsTab { "BALLS" }, notesTab { "NOTES" }, sampleTab { "SAMPLE" };
      Page page = Page::Synth;

      std::unique_ptr<juce::FileChooser> fileChooser;

      juce::String displayName, displayValue;
      double displayUntil = 0.0;

      juce::Rectangle<int> headerArea, displayArea, deckArea, keysArea;
   };
}
