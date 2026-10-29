#include "Voronoise/ui/SampleView.h"
#include "Voronoise/ui/Theme.h"

namespace UI
{
   SampleView::SampleView(VoronoiseAudioProcessor& p, std::function<void()> onLoadClicked) : processor(p)
   {
      startParam = processor.apvts.getParameter(Params::SAMPLE_START);
      endParam = processor.apvts.getParameter(Params::SAMPLE_END);

      loadButton.onClick = std::move(onLoadClicked);
      addAndMakeVisible(loadButton);

      timerCallback();
      startTimerHz(15);
   }

   void SampleView::timerCallback()
   {
      auto newSample = processor.getCurrentSample();
      std::vector<int> newBoundaries;
      if (newSample != nullptr)
         newBoundaries = SampleSlicer::getBoundaries(*newSample, processor.getSliceSettings());

      if (newSample != sample || newBoundaries != boundaries)
      {
         sample = newSample;
         boundaries = std::move(newBoundaries);
         repaint();
      }
   }

   juce::Rectangle<float> SampleView::getWaveArea() const
   {
      return getLocalBounds().toFloat().withTrimmedTop(24.f);
   }

   float SampleView::xToProportion(float x) const
   {
      const auto area = getWaveArea();
      return juce::jlimit(0.f, 1.f, (x - area.getX()) / area.getWidth());
   }

   float SampleView::proportionToX(float proportion) const
   {
      const auto area = getWaveArea();
      return area.getX() + proportion * area.getWidth();
   }

   void SampleView::resized()
   {
      loadButton.setBounds(getLocalBounds().removeFromTop(20).removeFromRight(70));
   }

   void SampleView::paint(juce::Graphics& g)
   {
      g.setFont(Theme::monoFont(12.f));
      g.setColour(Theme::lcdText);
      g.drawText(sample != nullptr ? sample->name + "  (" + juce::String(boundaries.size() - 1) + " slices)" : "NO SAMPLE - LOAD ONE TO SLICE IT",
                 getLocalBounds().removeFromTop(20).withTrimmedRight(76), juce::Justification::centredLeft);

      const auto area = getWaveArea();
      Theme::drawWell(g, area, 4.f);

      if (sample == nullptr || sample->peaks.empty())
         return;

      const int length = sample->buffer.getNumSamples();
      auto toX = [&](int position) { return proportionToX(static_cast<float>(position) / static_cast<float>(length)); };

      // waveform, one line per pixel using the loudest peak under it
      g.setColour(Theme::phosphor.withAlpha(0.75f));
      const float centreY = area.getCentreY();
      const auto numPeaks = sample->peaks.size();
      for (int px = 0; px < static_cast<int>(area.getWidth()); px++)
      {
         const auto first = static_cast<size_t>(static_cast<float>(px) * static_cast<float>(numPeaks) / area.getWidth());
         const auto last = juce::jmax(first + 1, static_cast<size_t>(static_cast<float>(px + 1) * static_cast<float>(numPeaks) / area.getWidth()));
         float peak = 0.f;
         for (size_t i = first; i < juce::jmin(last, numPeaks); i++)
            peak = juce::jmax(peak, sample->peaks[i]);

         const float halfHeight = juce::jmin(1.f, peak) * area.getHeight() * 0.45f;
         g.drawVerticalLine(static_cast<int>(area.getX()) + px, centreY - halfHeight, centreY + halfHeight);
      }

      if (boundaries.size() < 2)
         return;

      // darken everything outside the region
      const float regionStart = toX(boundaries.front());
      const float regionEnd = toX(boundaries.back());
      g.setColour(juce::Colours::black.withAlpha(0.55f));
      g.fillRect(area.withRight(regionStart));
      g.fillRect(area.withLeft(regionEnd));

      // slice lines with their number
      g.setFont(juce::FontOptions(10.f));
      for (size_t i = 0; i + 1 < boundaries.size(); i++)
      {
         const float x = toX(boundaries[i]);
         g.setColour(Theme::accent.withAlpha(0.8f));
         g.drawVerticalLine(static_cast<int>(x), area.getY(), area.getBottom());
         g.drawText(juce::String(i + 1), juce::Rectangle<float>(x + 2.f, area.getY() + 1.f, 20.f, 12.f), juce::Justification::topLeft);
      }

      // start/end handles
      g.setColour(Theme::highlight);
      g.fillRect(regionStart - 1.f, area.getY(), 3.f, area.getHeight());
      g.fillRect(regionEnd - 2.f, area.getY(), 3.f, area.getHeight());
   }

   void SampleView::mouseMove(const juce::MouseEvent& event)
   {
      const bool nearHandle = std::abs(event.position.x - proportionToX(startParam->getValue())) < 6.f
                              || std::abs(event.position.x - proportionToX(endParam->getValue())) < 6.f;
      setMouseCursor(nearHandle ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
   }

   void SampleView::mouseDown(const juce::MouseEvent& event)
   {
      if (sample == nullptr || ! getWaveArea().contains(event.position))
         return;

      // grab whichever handle is closer to the click
      const float startDistance = std::abs(event.position.x - proportionToX(startParam->getValue()));
      const float endDistance = std::abs(event.position.x - proportionToX(endParam->getValue()));
      dragging = startDistance <= endDistance ? startParam : endParam;

      dragging->beginChangeGesture();
      mouseDrag(event);
   }

   void SampleView::mouseDrag(const juce::MouseEvent& event)
   {
      if (dragging == nullptr)
         return;

      // start and end can't cross
      float proportion = xToProportion(event.position.x);
      if (dragging == startParam)
         proportion = juce::jmin(proportion, endParam->getValue() - 0.001f);
      else
         proportion = juce::jmax(proportion, startParam->getValue() + 0.001f);

      // start/end go 0 - 1 so the normalised value is the same as the real one
      dragging->setValueNotifyingHost(juce::jlimit(0.f, 1.f, proportion));
      timerCallback();
   }

   void SampleView::mouseUp(const juce::MouseEvent&)
   {
      if (dragging != nullptr)
         dragging->endChangeGesture();
      dragging = nullptr;
   }

}
