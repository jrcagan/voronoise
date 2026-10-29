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
      g.setColour(section);
      g.fillRoundedRectangle(area, 4.f);
      g.setColour(sectionEdge);
      g.drawRoundedRectangle(area.reduced(0.5f), 4.f, 1.f);

      if (title.isNotEmpty())
      {
         auto titleArea = area.reduced(12.f, 0.f).removeFromTop(24.f);
         g.setFont(titleFont());
         g.setColour(label);
         g.drawText(title.toUpperCase(), titleArea, juce::Justification::centredLeft);

         // hairline after the title
         const float lineX = titleArea.getX() + juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), title.toUpperCase()) + 10.f;
         g.setColour(sectionEdge);
         g.drawHorizontalLine(static_cast<int>(titleArea.getCentreY()), lineX, area.getRight() - 12.f);
      }
   }

   void drawPanel(juce::Graphics& g, juce::Rectangle<float> area)
   {
      g.setGradientFill(juce::ColourGradient(panelLight, area.getX(), area.getY(), panel, area.getX(), area.getBottom(), false));
      g.fillRect(area);
   }

   void drawWell(juce::Graphics& g, juce::Rectangle<float> area, float corner)
   {
      g.setColour(well);
      g.fillRoundedRectangle(area, corner);
      g.setColour(sectionEdge);
      g.drawRoundedRectangle(area.reduced(0.5f), corner, 1.f);
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
      setColour(juce::PopupMenu::backgroundColourId, section);
      setColour(juce::PopupMenu::textColourId, label);
      setColour(juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha(0.25f));
      setColour(juce::PopupMenu::highlightedTextColourId, highlight);
      setColour(juce::ComboBox::textColourId, lcdText);
      setColour(juce::ComboBox::backgroundColourId, well);
      setColour(juce::ComboBox::outlineColourId, sectionEdge);
      setColour(juce::ComboBox::arrowColourId, labelDim);
      setColour(juce::Label::textColourId, label);
      setColour(juce::TextButton::textColourOffId, label);
      setColour(juce::TextButton::textColourOnId, highlight);
      setColour(juce::Slider::textBoxTextColourId, label);
      setColour(juce::TooltipWindow::backgroundColourId, section);
      setColour(juce::TooltipWindow::textColourId, label);
      setColour(juce::TooltipWindow::outlineColourId, sectionEdge);
      setColour(juce::ScrollBar::thumbColourId, controlEdge);
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
      g.setColour(sectionEdge);
      g.strokePath(track, juce::PathStrokeType(2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

      // value arc, from the middle for bipolar knobs
      const float arcStart = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
      juce::Path value;
      value.addCentredArc(centre.x, centre.y, ringRadius, ringRadius, 0.f, juce::jmin(arcStart, angle), juce::jmax(arcStart, angle), true);
      g.setColour(slider.isEnabled() ? accent : labelDim);
      g.strokePath(value, juce::PathStrokeType(2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

      // flat body with a hairline edge
      const auto body = juce::Rectangle<float>(knobRadius * 2.f, knobRadius * 2.f).withCentre(centre);
      g.setGradientFill(juce::ColourGradient(control.interpolatedWith(controlEdge, 0.5f), centre.x, body.getY(), control.darker(0.3f), centre.x, body.getBottom(), false));
      g.fillEllipse(body);
      g.setColour(controlEdge);
      g.drawEllipse(body.reduced(0.5f), 1.f);

      // pointer from near the middle out to the edge
      juce::Path pointer;
      pointer.addRoundedRectangle(-1.f, -knobRadius + 3.f, 2.f, knobRadius * 0.5f, 1.f);
      g.setColour(slider.isEnabled() ? highlight : labelDim);
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
      auto slot = vertical ? bounds.withSizeKeepingCentre(3.f, bounds.getHeight()) : bounds.withSizeKeepingCentre(bounds.getWidth(), 3.f);
      g.setColour(sectionEdge);
      g.fillRoundedRectangle(slot, 1.5f);

      // lit part
      auto lit = vertical ? slot.withTop(sliderPos) : slot.withRight(sliderPos);
      if (slider.getProperties()["bipolar"])
      {
         const float middle = vertical ? slot.getCentreY() : slot.getCentreX();
         lit = vertical ? slot.withTop(juce::jmin(middle, sliderPos)).withBottom(juce::jmax(middle, sliderPos))
                        : slot.withLeft(juce::jmin(middle, sliderPos)).withRight(juce::jmax(middle, sliderPos));
      }
      g.setColour(fillColour);
      g.fillRoundedRectangle(lit, 1.5f);

      // flat fader cap with a lit line through the middle
      const auto cap = vertical ? juce::Rectangle<float>(juce::jmin(bounds.getWidth(), 20.f), 8.f).withCentre({ bounds.getCentreX(), sliderPos })
                                : juce::Rectangle<float>(8.f, juce::jmin(bounds.getHeight(), 16.f)).withCentre({ sliderPos, bounds.getCentreY() });
      g.setColour(control);
      g.fillRoundedRectangle(cap, 2.f);
      g.setColour(controlEdge);
      g.drawRoundedRectangle(cap.reduced(0.5f), 2.f, 1.f);
      g.setColour(highlight);
      if (vertical)
         g.drawHorizontalLine(static_cast<int>(cap.getCentreY()), cap.getX() + 3.f, cap.getRight() - 3.f);
      else
         g.drawVerticalLine(static_cast<int>(cap.getCentreX()), cap.getY() + 3.f, cap.getBottom() - 3.f);
   }

   void LookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                          bool isHighlighted, bool isDown)
   {
      auto bounds = button.getLocalBounds().toFloat().reduced(1.f);
      const bool on = button.getToggleState() && hasLed(button);
      const auto onColour = button.getProperties().contains("ledColour")
                                ? juce::Colour(static_cast<juce::uint32>(static_cast<juce::int64>(button.getProperties()["ledColour"])))
                                : accent;

      // flat key: tinted with the accent when it's on
      auto fill = control;
      if (isHighlighted)
         fill = fill.interpolatedWith(controlEdge, 0.4f);
      if (isDown)
         fill = fill.darker(0.3f);
      g.setColour(fill);
      g.fillRoundedRectangle(bounds, 3.f);

      if (on)
      {
         g.setColour(onColour.withAlpha(0.16f));
         g.fillRoundedRectangle(bounds, 3.f);
      }

      g.setColour(on ? onColour.withAlpha(0.8f) : controlEdge);
      g.drawRoundedRectangle(bounds.reduced(0.5f), 3.f, 1.f);
   }

   void LookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool isDown)
   {
      auto area = button.getLocalBounds().reduced(4, 2);
      if (isDown)
         area.translate(0, 1);

      g.setFont(getTextButtonFont(button, button.getHeight()));
      g.setColour(button.getToggleState() && hasLed(button) ? highlight : label);
      g.drawFittedText(button.getButtonText(), area, juce::Justification::centred, 1);
   }

   juce::Font LookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
   {
      return labelFont(juce::jmin(11.f, static_cast<float>(buttonHeight) * 0.5f));
   }

   void LookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int, int, juce::ComboBox& box)
   {
      // small display window
      drawWell(g, juce::Rectangle<int>(0, 0, width, height).toFloat(), 3.f);

      // down arrow
      juce::Path arrow;
      const float ax = static_cast<float>(width) - 12.f, ay = static_cast<float>(height) * 0.5f;
      arrow.addTriangle(ax - 4.f, ay - 2.f, ax + 4.f, ay - 2.f, ax, ay + 3.f);
      g.setColour(box.findColour(juce::ComboBox::arrowColourId).withAlpha(box.isEnabled() ? 1.f : 0.4f));
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
      g.setColour(controlEdge.withAlpha(isMouseOver ? 1.f : 0.7f));
      g.fillRoundedRectangle(thumb.toFloat(), 3.f);
   }

   void LookAndFeel::drawCornerResizer(juce::Graphics& g, int w, int h, bool isMouseOver, bool isMouseDragging)
   {
      // three short diagonal grip lines in the corner
      g.setColour(isMouseOver || isMouseDragging ? accent : controlEdge);
      const float size = static_cast<float>(juce::jmin(w, h));
      for (float i = 0.3f; i < 1.f; i += 0.3f)
         g.drawLine(static_cast<float>(w) - size * i, static_cast<float>(h), static_cast<float>(w), static_cast<float>(h) - size * i, 1.f);
   }
}
