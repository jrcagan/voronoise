#pragma once

#include <JuceHeader.h>

// colours and drawing shared by the whole panel
namespace Theme
{
   inline const juce::Colour panel        { 0xff2a2b30 };
   inline const juce::Colour panelLight   { 0xff3a3b41 };
   inline const juce::Colour section      { 0xff1f2024 };
   inline const juce::Colour sectionEdge  { 0xff45464d };
   inline const juce::Colour label        { 0xffc3c6ce };
   inline const juce::Colour labelDim     { 0xff7e818b };
   inline const juce::Colour accent       { 0xffff8a2b };
   inline const juce::Colour screen       { 0xff081115 };
   inline const juce::Colour phosphor     { 0xff6ff7d8 };
   inline const juce::Colour lcdText      { 0xffffb347 };

   inline juce::Colour lfoColour(int lfo)
   {
      constexpr juce::uint32 COLOURS[] = { 0xff35d0ff, 0xffff4fa3, 0xffb6f23e };
      return juce::Colour(COLOURS[juce::jlimit(0, 2, lfo)]);
   }

   juce::Font labelFont(float height = 11.f);
   juce::Font titleFont(float height = 12.f);
   juce::Font monoFont(float height = 13.f);

   // dark recessed box with a title engraved in the top left, like the sections on a hardware synth
   void drawSection(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title);

   // wooden end cheek
   void drawWood(juce::Graphics& g, juce::Rectangle<float> area, int seed);

   // brushed metal-ish panel background
   void drawPanel(juce::Graphics& g, juce::Rectangle<float> area);

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
   };
}
