#include "Voronoise/ui/Theme.h"

namespace Theme
{
   juce::Font labelFont(float height)
   {
      return juce::Font(juce::FontOptions(height, juce::Font::bold)).withExtraKerningFactor(0.08f);
   }

   juce::Font titleFont(float height)
   {
      return juce::Font(juce::FontOptions(height, juce::Font::bold)).withExtraKerningFactor(0.18f);
   }

   juce::Font monoFont(float height)
   {
      return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), height, juce::Font::plain));
   }

   void drawSection(juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title)
   {
      // soft drop shadow so it looks set into the panel
      g.setColour(juce::Colours::black.withAlpha(0.35f));
      g.fillRoundedRectangle(area.translated(0.f, 2.f), 6.f);

      g.setGradientFill(juce::ColourGradient(section.brighter(0.06f), area.getX(), area.getY(),
                                             section.darker(0.15f), area.getX(), area.getBottom(), false));
      g.fillRoundedRectangle(area, 6.f);

      g.setColour(sectionEdge);
      g.drawRoundedRectangle(area.reduced(0.5f), 6.f, 1.f);

      if (title.isNotEmpty())
      {
         auto titleArea = area.reduced(12.f, 0.f).removeFromTop(24.f);
         g.setFont(titleFont());
         g.setColour(label);
         g.drawText(title.toUpperCase(), titleArea, juce::Justification::centredLeft);

         // engraved line after the title
         const float lineX = titleArea.getX() + juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), title.toUpperCase()) + 10.f;
         g.setColour(sectionEdge);
         g.drawHorizontalLine(static_cast<int>(titleArea.getCentreY()), lineX, area.getRight() - 12.f);
      }
   }

   void drawWood(juce::Graphics& g, juce::Rectangle<float> area, int seed)
   {
      g.setGradientFill(juce::ColourGradient(juce::Colour(0xff6e4426), area.getX(), area.getY(),
                                             juce::Colour(0xff3b2313), area.getRight(), area.getY(), false));
      g.fillRect(area);

      // grain: wobbly vertical lines, seeded so it looks the same every repaint
      juce::Random random(seed);
      for (int i = 0; i < 40; i++)
      {
         const float x = area.getX() + random.nextFloat() * area.getWidth();
         const float wobble = 0.5f + random.nextFloat() * 2.f;
         const float phase = random.nextFloat() * 6.f;
         juce::Path grain;
         grain.startNewSubPath(x, area.getY());
         for (float y = area.getY(); y <= area.getBottom(); y += 8.f)
            grain.lineTo(x + wobble * std::sin(phase + y * 0.02f), y);

         g.setColour((random.nextBool() ? juce::Colours::black : juce::Colour(0xffa0703f)).withAlpha(0.08f + 0.1f * random.nextFloat()));
         g.strokePath(grain, juce::PathStrokeType(0.6f + random.nextFloat()));
      }

      // edges catch the light
      g.setColour(juce::Colours::white.withAlpha(0.08f));
      g.drawVerticalLine(static_cast<int>(area.getX()), area.getY(), area.getBottom());
      g.setColour(juce::Colours::black.withAlpha(0.5f));
      g.drawVerticalLine(static_cast<int>(area.getRight()) - 1, area.getY(), area.getBottom());
   }

   void drawPanel(juce::Graphics& g, juce::Rectangle<float> area)
   {
      g.setGradientFill(juce::ColourGradient(panelLight, area.getX(), area.getY(), panel.darker(0.3f), area.getX(), area.getBottom(), false));
      g.fillRect(area);

      // faint horizontal brushing
      juce::Random random(7);
      for (float y = area.getY(); y < area.getBottom(); y += 2.f)
      {
         g.setColour(juce::Colours::white.withAlpha(0.012f + 0.012f * random.nextFloat()));
         g.drawHorizontalLine(static_cast<int>(y), area.getX(), area.getRight());
      }
   }

   namespace
   {
      // toggles, radio buttons and switch segments get an led strip, plain buttons don't
      bool hasLed(juce::Button& button)
      {
         return button.getClickingTogglesState() || button.getRadioGroupId() != 0 || button.getToggleState()
                || static_cast<bool>(button.getProperties()["switch"]);
      }
   }

   //==============================================================================
   LookAndFeel::LookAndFeel()
   {
      setColour(juce::ResizableWindow::backgroundColourId, panel);
      setColour(juce::PopupMenu::backgroundColourId, juce::Colour(0xff1b1c20));
      setColour(juce::PopupMenu::textColourId, label);
      setColour(juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha(0.8f));
      setColour(juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
      setColour(juce::ComboBox::textColourId, lcdText);
      setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff111215));
      setColour(juce::ComboBox::outlineColourId, sectionEdge);
      setColour(juce::ComboBox::arrowColourId, label);
      setColour(juce::Label::textColourId, label);
      setColour(juce::TextButton::textColourOffId, label);
      setColour(juce::TextButton::textColourOnId, accent);
      setColour(juce::Slider::textBoxTextColourId, label);
      setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xff111215));
      setColour(juce::TooltipWindow::textColourId, label);
      setColour(juce::TooltipWindow::outlineColourId, sectionEdge);
      setColour(juce::ScrollBar::thumbColourId, sectionEdge);
   }

   void LookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                      float startAngle, float endAngle, juce::Slider& slider)
   {
      const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
      const float size = juce::jmin(bounds.getWidth(), bounds.getHeight());
      const auto centre = bounds.getCentre();
      const float ringRadius = size * 0.5f - 3.f;
      const float knobRadius = ringRadius - 6.f;
      const float angle = startAngle + sliderPos * (endAngle - startAngle);
      const bool bipolar = slider.getProperties()["bipolar"];

      // track
      juce::Path track;
      track.addCentredArc(centre.x, centre.y, ringRadius, ringRadius, 0.f, startAngle, endAngle, true);
      g.setColour(juce::Colours::black.withAlpha(0.6f));
      g.strokePath(track, juce::PathStrokeType(3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

      // value arc, from the middle for bipolar knobs
      const float arcStart = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
      juce::Path value;
      value.addCentredArc(centre.x, centre.y, ringRadius, ringRadius, 0.f, juce::jmin(arcStart, angle), juce::jmax(arcStart, angle), true);
      g.setColour(slider.isEnabled() ? accent : labelDim);
      g.strokePath(value, juce::PathStrokeType(3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

      // body: shadow, dark skirt, then a brushed cap
      g.setColour(juce::Colours::black.withAlpha(0.5f));
      g.fillEllipse(juce::Rectangle<float>(knobRadius * 2.f, knobRadius * 2.f).withCentre(centre.translated(0.f, 2.f)));

      g.setGradientFill(juce::ColourGradient(juce::Colour(0xff4a4b52), centre.x, centre.y - knobRadius,
                                             juce::Colour(0xff121316), centre.x, centre.y + knobRadius, false));
      g.fillEllipse(juce::Rectangle<float>(knobRadius * 2.f, knobRadius * 2.f).withCentre(centre));

      const float capRadius = knobRadius * 0.72f;
      g.setGradientFill(juce::ColourGradient(juce::Colour(0xffd9dbe0), centre.x - capRadius, centre.y - capRadius,
                                             juce::Colour(0xff6c6e75), centre.x + capRadius, centre.y + capRadius, true));
      g.fillEllipse(juce::Rectangle<float>(capRadius * 2.f, capRadius * 2.f).withCentre(centre));
      g.setColour(juce::Colours::black.withAlpha(0.35f));
      g.drawEllipse(juce::Rectangle<float>(capRadius * 2.f, capRadius * 2.f).withCentre(centre), 1.f);

      // pointer line from the cap out to the skirt edge
      juce::Path pointer;
      pointer.addRoundedRectangle(-1.5f, -knobRadius + 2.f, 3.f, knobRadius * 0.55f, 1.5f);
      g.setColour(juce::Colours::white);
      g.fillPath(pointer, juce::AffineTransform::rotation(angle).translated(centre));
   }

   void LookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                      float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle style, juce::Slider& slider)
   {
      if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearHorizontal)
      {
         LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
         return;
      }

      const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
      const bool vertical = style == juce::Slider::LinearVertical;
      const auto fillColour = slider.findColour(juce::Slider::trackColourId);

      // slot
      auto slot = vertical ? bounds.withSizeKeepingCentre(4.f, bounds.getHeight()) : bounds.withSizeKeepingCentre(bounds.getWidth(), 4.f);
      g.setColour(juce::Colours::black.withAlpha(0.7f));
      g.fillRoundedRectangle(slot, 2.f);

      // lit part
      auto lit = vertical ? slot.withTop(sliderPos) : slot.withRight(sliderPos);
      if (slider.getProperties()["bipolar"])
      {
         const float middle = vertical ? slot.getCentreY() : slot.getCentreX();
         lit = vertical ? slot.withTop(juce::jmin(middle, sliderPos)).withBottom(juce::jmax(middle, sliderPos))
                        : slot.withLeft(juce::jmin(middle, sliderPos)).withRight(juce::jmax(middle, sliderPos));
      }
      g.setColour(fillColour);
      g.fillRoundedRectangle(lit, 2.f);

      // fader cap
      const auto cap = vertical ? juce::Rectangle<float>(juce::jmin(bounds.getWidth(), 22.f), 10.f).withCentre({ bounds.getCentreX(), sliderPos })
                                : juce::Rectangle<float>(10.f, juce::jmin(bounds.getHeight(), 18.f)).withCentre({ sliderPos, bounds.getCentreY() });
      g.setGradientFill(juce::ColourGradient(juce::Colour(0xffdadce1), cap.getX(), cap.getY(), juce::Colour(0xff6a6c73), cap.getX(), cap.getBottom(), false));
      g.fillRoundedRectangle(cap, 2.f);
      g.setColour(juce::Colours::black.withAlpha(0.6f));
      if (vertical)
         g.drawHorizontalLine(static_cast<int>(cap.getCentreY()), cap.getX() + 2.f, cap.getRight() - 2.f);
      else
         g.drawVerticalLine(static_cast<int>(cap.getCentreX()), cap.getY() + 2.f, cap.getBottom() - 2.f);
   }

   void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                          bool isHighlighted, bool isDown)
   {
      auto bounds = button.getLocalBounds().toFloat().reduced(1.f);
      const bool on = button.getToggleState();

      // hardware key: raised gradient, pressed in when down
      g.setColour(juce::Colours::black.withAlpha(0.5f));
      g.fillRoundedRectangle(bounds.translated(0.f, isDown ? 0.5f : 1.5f), 4.f);

      auto top = juce::Colour(0xff4b4c53), bottom = juce::Colour(0xff26272b);
      if (isHighlighted)
         top = top.brighter(0.1f);
      g.setGradientFill(juce::ColourGradient(isDown ? bottom : top, bounds.getX(), bounds.getY(),
                                             isDown ? top : bottom, bounds.getX(), bounds.getBottom(), false));
      g.fillRoundedRectangle(bounds, 4.f);
      g.setColour(juce::Colours::white.withAlpha(0.07f));
      g.drawHorizontalLine(static_cast<int>(bounds.getY()) + 1, bounds.getX() + 3.f, bounds.getRight() - 3.f);
      g.setColour(juce::Colours::black.withAlpha(0.6f));
      g.drawRoundedRectangle(bounds, 4.f, 1.f);

      // led strip across the top when it's on
      if (hasLed(button))
      {
         const auto ledColour = button.getProperties().contains("ledColour")
                                    ? juce::Colour(static_cast<juce::uint32>(static_cast<juce::int64>(button.getProperties()["ledColour"])))
                                    : accent;
         auto led = bounds.reduced(bounds.getWidth() * 0.3f, 0.f).withHeight(3.f).translated(0.f, 4.f);
         if (on)
         {
            g.setColour(ledColour.withAlpha(0.35f));
            g.fillRoundedRectangle(led.expanded(3.f, 2.f), 3.f);
         }
         g.setColour(on ? ledColour : juce::Colours::black.withAlpha(0.6f));
         g.fillRoundedRectangle(led, 1.5f);
      }
   }

   void LookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool isDown)
   {
      auto area = button.getLocalBounds().reduced(4, 2);
      if (hasLed(button))
         area.removeFromTop(6);
      if (isDown)
         area.translate(0, 1);

      g.setFont(getTextButtonFont(button, button.getHeight()));
      g.setColour(button.getToggleState() ? juce::Colours::white : label);
      g.drawFittedText(button.getButtonText(), area, juce::Justification::centred, 1);
   }

   juce::Font LookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
   {
      return labelFont(juce::jmin(11.f, static_cast<float>(buttonHeight) * 0.5f));
   }

   void LookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
   {
      // small lcd window
      auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(0.5f);
      g.setColour(juce::Colour(0xff0e0f12));
      g.fillRoundedRectangle(bounds, 3.f);
      g.setColour(juce::Colours::black);
      g.drawRoundedRectangle(bounds, 3.f, 1.f);
      g.setColour(juce::Colours::white.withAlpha(0.06f));
      g.drawHorizontalLine(height - 1, 3.f, static_cast<float>(width) - 3.f);

      // down arrow
      juce::Path arrow;
      const float ax = static_cast<float>(width) - 12.f, ay = static_cast<float>(height) * 0.5f;
      arrow.addTriangle(ax - 4.f, ay - 2.f, ax + 4.f, ay - 2.f, ax, ay + 3.f);
      g.setColour(box.findColour(juce::ComboBox::arrowColourId).withAlpha(box.isEnabled() ? 0.9f : 0.3f));
      g.fillPath(arrow);
   }

   juce::Font LookAndFeel::getComboBoxFont(juce::ComboBox&)
   {
      return monoFont(12.f);
   }

   void LookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& text)
   {
      text.setBounds(4, 1, box.getWidth() - 22, box.getHeight() - 2);
      text.setFont(getComboBoxFont(box));
   }

   juce::Font LookAndFeel::getPopupMenuFont()
   {
      return labelFont(13.f);
   }

   void LookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar&, int x, int y, int width, int height, bool isScrollbarVertical,
                                   int thumbStartPosition, int thumbSize, bool isMouseOver, bool)
   {
      const auto thumb = isScrollbarVertical ? juce::Rectangle<int>(x + 2, thumbStartPosition, width - 4, thumbSize)
                                             : juce::Rectangle<int>(thumbStartPosition, y + 2, thumbSize, height - 4);
      g.setColour(sectionEdge.withAlpha(isMouseOver ? 1.f : 0.7f));
      g.fillRoundedRectangle(thumb.toFloat(), 3.f);
   }
}
