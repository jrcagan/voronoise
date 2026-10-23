#include "Voronoise/SettingsPanel.h"
#include "Voronoise/Params.h"
#include "synth/Scale.h"

namespace Settings
{
   //==============================================================================
   ParamRow::ParamRow(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramID)
   {
      auto* param = apvts.getParameter(paramID);
      jassert(param != nullptr); // typo in a param id

      label.setText(param->getName(64), juce::dontSendNotification);
      label.setFont(juce::FontOptions(13.f));
      addAndMakeVisible(label);

      if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(param))
      {
         comboBox = std::make_unique<juce::ComboBox>();
         comboBox->addItemList(choice->choices, 1);
         addAndMakeVisible(*comboBox);
         comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, paramID, *comboBox);
      }
      else if (dynamic_cast<juce::AudioParameterBool*>(param) != nullptr)
      {
         toggle = std::make_unique<juce::ToggleButton>();
         addAndMakeVisible(*toggle);
         buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, paramID, *toggle);
      }
      else
      {
         slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
         slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 20);
         addAndMakeVisible(*slider);
         // the attachment also gives the text box the param's own text (note names, units)
         sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, paramID, *slider);
      }
   }

   void ParamRow::resized()
   {
      auto area = getLocalBounds().reduced(0, 2);
      label.setBounds(area.removeFromLeft(juce::jmin(130, area.getWidth() / 2)));

      if (slider != nullptr)
         slider->setBounds(area);
      if (comboBox != nullptr)
         comboBox->setBounds(area);
      if (toggle != nullptr)
         toggle->setBounds(area.removeFromLeft(30));
   }

   //==============================================================================
   void Page::addHeading(const juce::String& text)
   {
      auto heading = std::make_unique<juce::Label>();
      heading->setText(text, juce::dontSendNotification);
      heading->setFont(juce::FontOptions(15.f, juce::Font::bold));
      heading->setColour(juce::Label::textColourId, juce::Colours::aqua);
      addComponent(std::move(heading), ROW_HEIGHT);
   }

   void Page::addParams(juce::AudioProcessorValueTreeState& apvts, std::initializer_list<juce::String> paramIDs)
   {
      for (const auto& id : paramIDs)
         addComponent(std::make_unique<ParamRow>(apvts, id), ROW_HEIGHT);
   }

   void Page::addComponent(std::unique_ptr<juce::Component> component, int height)
   {
      addAndMakeVisible(*component);
      rows.emplace_back(std::move(component), height);
   }

   int Page::getContentHeight() const
   {
      int height = 8;
      for (const auto& row : rows)
         height += row.second + 2;
      return height + 8;
   }

   void Page::resized()
   {
      auto area = getLocalBounds().reduced(10, 8);
      for (auto& [component, height] : rows)
      {
         component->setBounds(area.removeFromTop(height));
         area.removeFromTop(2);
      }
   }

   //==============================================================================
   NoteWeights::NoteWeights(VoronoiseAudioProcessor& p) : processor(p)
   {
      for (int pc = 0; pc < 12; pc++)
      {
         auto& slider = sliders[static_cast<size_t>(pc)];
         slider.setSliderStyle(juce::Slider::LinearBarVertical);
         slider.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
         slider.setTooltip(Scale::getNoteNames()[pc] + " probability");
         addAndMakeVisible(slider);
         attachments[static_cast<size_t>(pc)] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
             processor.apvts, Params::noteWeight(pc), slider);
      }

      timerCallback();
      startTimerHz(10);
   }

   void NoteWeights::timerCallback()
   {
      const auto settings = processor.getNoteSettings();
      auto mask = Scale::getPitchClassMask(settings.type, settings.root);
      if (! settings.lockToScale)
         mask.fill(true);

      if (mask != inScale)
      {
         inScale = mask;
         for (size_t pc = 0; pc < 12; pc++)
         {
            // notes outside the scale can't play, so their probability doesn't matter
            sliders[pc].setAlpha(inScale[pc] ? 1.f : 0.3f);
            sliders[pc].setColour(juce::Slider::trackColourId, inScale[pc] ? juce::Colours::aqua : juce::Colours::grey);
         }
         repaint();
      }
   }

   void NoteWeights::paint(juce::Graphics& g)
   {
      g.setFont(juce::FontOptions(11.f));
      const auto names = Scale::getNoteNames();
      for (size_t pc = 0; pc < 12; pc++)
      {
         g.setColour(juce::Colours::white.withAlpha(inScale[pc] ? 1.f : 0.35f));
         g.drawText(names[static_cast<int>(pc)], sliders[pc].getBounds().withY(getHeight() - 16).withHeight(16),
                    juce::Justification::centred);
      }
   }

   void NoteWeights::resized()
   {
      auto area = getLocalBounds();
      area.removeFromBottom(16);
      const float width = static_cast<float>(area.getWidth()) / 12.f;
      for (size_t pc = 0; pc < 12; pc++)
      {
         const int x = static_cast<int>(width * static_cast<float>(pc));
         const int nextX = static_cast<int>(width * static_cast<float>(pc + 1));
         sliders[pc].setBounds(x + 1, area.getY(), nextX - x - 2, area.getHeight());
      }
   }

   //==============================================================================
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
      g.setFont(juce::FontOptions(12.f));
      g.setColour(juce::Colours::white);
      g.drawText(sample != nullptr ? sample->name + "  (" + juce::String(boundaries.size() - 1) + " slices)" : "No sample loaded",
                 getLocalBounds().removeFromTop(20).withTrimmedRight(76), juce::Justification::centredLeft);

      const auto area = getWaveArea();
      g.setColour(juce::Colours::black.withAlpha(0.35f));
      g.fillRoundedRectangle(area, 4.f);

      if (sample == nullptr || sample->peaks.empty())
         return;

      const int length = sample->buffer.getNumSamples();
      auto toX = [&](int position) { return proportionToX(static_cast<float>(position) / static_cast<float>(length)); };

      // waveform, one line per pixel using the loudest peak under it
      g.setColour(juce::Colours::white.withAlpha(0.7f));
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
         g.setColour(juce::Colours::orange.withAlpha(0.8f));
         g.drawVerticalLine(static_cast<int>(x), area.getY(), area.getBottom());
         g.drawText(juce::String(i + 1), juce::Rectangle<float>(x + 2.f, area.getY() + 1.f, 20.f, 12.f), juce::Justification::topLeft);
      }

      // start/end handles
      g.setColour(juce::Colours::aqua);
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

   //==============================================================================
   SettingsPanel::SettingsPanel(VoronoiseAudioProcessor& processor, std::function<void()> onLoadSample)
   {
      auto& apvts = processor.apvts;

      auto view = std::make_unique<Page>();
      view->addHeading("Lines");
      view->addParams(apvts, { Params::LINES });

      auto balls = std::make_unique<Page>();
      balls->addHeading("Physics");
      balls->addParams(apvts, { Params::GRAVITY, Params::BOUNCINESS, Params::DRAG });
      balls->addHeading("Size");
      balls->addParams(apvts, { Params::BALL_SIZE, Params::SIZE_VARIATION });
      balls->addHeading("Behaviour");
      balls->addParams(apvts, { Params::LIFETIME, Params::LAUNCH_SPEED, Params::MAX_BALLS,
                                Params::PASS_THROUGH, Params::PASS_THROUGH_PLAYS });

      auto notes = std::make_unique<Page>();
      notes->addHeading("Scale");
      notes->addParams(apvts, { Params::SCALE_LOCK, Params::SCALE_ROOT, Params::SCALE_TYPE });
      notes->addHeading("Range");
      notes->addParams(apvts, { Params::LOWEST_NOTE, Params::HIGHEST_NOTE, Params::OCTAVE_SHIFT });
      notes->addHeading("Picking");
      notes->addParams(apvts, { Params::PITCH_SOURCE, Params::SNAP_MIDI });
      notes->addHeading("Note probability");
      notes->addComponent(std::make_unique<NoteWeights>(processor), 110);

      auto sound = std::make_unique<Page>();
      sound->addHeading("Pulse");
      sound->addParams(apvts, { Params::PULSE_WIDTH });
      sound->addHeading("Sample");
      sound->addComponent(std::make_unique<SampleView>(processor, std::move(onLoadSample)), 120);
      sound->addParams(apvts, { Params::SAMPLE_START, Params::SAMPLE_END });
      sound->addHeading("Slicing");
      sound->addParams(apvts, { Params::SLICE_MODE, Params::SLICE_COUNT, Params::SENSITIVITY, Params::SLICE_PICK });
      sound->addHeading("Playback");
      sound->addParams(apvts, { Params::REVERSE, Params::TRACK_PITCH, Params::ROOT_NOTE, Params::TRANSPOSE,
                                Params::FINE_TUNE, Params::SAMPLE_GAIN, Params::FADE_IN, Params::FADE_OUT });

      addPage("View", std::move(view));
      addPage("Balls", std::move(balls));
      addPage("Notes", std::move(notes));
      addPage("Sound", std::move(sound));

      addAndMakeVisible(tabs);
   }

   void SettingsPanel::addPage(const juce::String& name, std::unique_ptr<Page> page)
   {
      // each page scrolls on its own, the viewport is owned by the tabs
      auto* viewport = new juce::Viewport();
      viewport->setViewedComponent(page.get(), false);
      viewport->setScrollBarsShown(true, false);
      tabs.addTab(name, juce::Colours::transparentBlack, viewport, true);
      pages.push_back(std::move(page));
   }

   void SettingsPanel::paint(juce::Graphics& g)
   {
      g.setColour(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId).darker(0.4f).withAlpha(0.96f));
      g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.f);
      g.setColour(juce::Colours::aqua.withAlpha(0.4f));
      g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 6.f, 1.f);
   }

   void SettingsPanel::resized()
   {
      tabs.setBounds(getLocalBounds().reduced(4));

      for (auto& page : pages)
      {
         // a bit narrower than the tabs to leave room for the scrollbar
         page->setSize(tabs.getWidth() - 12, page->getContentHeight());
      }
   }
}
