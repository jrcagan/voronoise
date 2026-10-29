#pragma once

#include <JuceHeader.h>

// colours and drawing shared by the whole panel
namespace Theme
{
   // one palette: deep blue-black surfaces, blue-grey text and a single blue accent with its shades
   inline const juce::Colour panel        { 0xff070a12 };
   inline const juce::Colour panelLight   { 0xff0b101c };
   inline const juce::Colour section      { 0xff0a0f1b };
   inline const juce::Colour sectionEdge  { 0xff19223a };
   inline const juce::Colour well         { 0xff04060c }; // recessed displays
   inline const juce::Colour control      { 0xff111829 }; // knob bodies, buttons
   inline const juce::Colour controlEdge  { 0xff243052 };
   inline const juce::Colour label        { 0xffc2cde6 };
   inline const juce::Colour labelDim     { 0xff56648a };
   inline const juce::Colour accent       { 0xff4d8dff };
   inline const juce::Colour highlight    { 0xffb3ccff }; // the accent at its lightest
   inline const juce::Colour screen       { 0xff03060d };
   inline const juce::Colour phosphor     { 0xff6ea6ff };
   inline const juce::Colour lcdText      { 0xff8cb6ff };
   inline const juce::Colour keyWhite     { 0xff6f93d6 }; // the on-screen keys, a lighter blue so they read as keys
   inline const juce::Colour keyBlack     { 0xff16233f };

   // three shades of the accent: blue, ice and violet-blue
   inline juce::Colour lfoColour(int lfo)
   {
      constexpr juce::uint32 COLOURS[] = { 0xff4d8dff, 0xff8fd3ff, 0xff9b8cff };
      return juce::Colour(COLOURS[juce::jlimit(0, 2, lfo)]);
   }

   juce::Font labelFont(float height = 11.f);
   juce::Font titleFont(float height = 12.f);
   juce::Font monoFont(float height = 13.f);

   // flat box with a hairline edge and its title in the top left
   void drawSection(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title);

   // the panel background, a faint vertical fade
   void drawPanel(juce::Graphics& g, juce::Rectangle<float> area);

   // recessed display window: well colour with a hairline edge
   void drawWell(juce::Graphics& g, juce::Rectangle<float> area, float corner = 4.f);

   class LookAndFeel : public juce::LookAndFeel_V4
   {
   public:
      LookAndFeel();

      void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                            float startAngle, float endAngle, juce::Slider&) override;
      void drawLinearSlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                            float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;
      void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                                bool isHighlighted, bool isDown) override;
      void drawButtonText(juce::Graphics&, juce::TextButton&, bool isHighlighted, bool isDown) override;
      void drawComboBox(juce::Graphics&, int width, int height, bool isDown, int buttonX, int buttonY,
                        int buttonW, int buttonH, juce::ComboBox&) override;
      juce::Font getComboBoxFont(juce::ComboBox&) override;
      void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
      juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
      juce::Font getPopupMenuFont() override;
      void drawScrollbar(juce::Graphics&, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                         int thumbStartPosition, int thumbSize, bool isMouseOver, bool isMouseDown) override;
      void drawCornerResizer(juce::Graphics&, int w, int h, bool isMouseOver, bool isMouseDragging) override;
   };
}
