#include "Voronoise/ui/MainPanel.h"
#include "Voronoise/ui/Theme.h"
#include "synth/Scale.h"

namespace UI
{
   //==============================================================================
   Computer::Computer(VoronoiseAudioProcessor& p, std::function<void()> onLoadSample) : canvas(p)
   {
      addAndMakeVisible(canvas);

      for (auto* key : { &drawKey, &ballKey })
      {
         key->setClickingTogglesState(true);
         key->setRadioGroupId(2001);
      }
      drawKey.setToggleState(true, juce::dontSendNotification);
      drawKey.onClick = [this] { if (drawKey.getToggleState()) canvas.setMode(Canvas::Mode::DrawSites); };
      ballKey.onClick = [this] { if (ballKey.getToggleState()) canvas.setMode(Canvas::Mode::DropBalls); };
      clearBallsKey.onClick = [this] { canvas.clearBalls(); };
      clearSitesKey.onClick = [this] { canvas.clearSites(); };
      loadKey.onClick = std::move(onLoadSample);

      drawKey.setTooltip("Double click the screen to place sites");
      ballKey.setTooltip("Click the screen to drop balls");

      for (auto* key : { &drawKey, &ballKey, &clearBallsKey, &clearSitesKey, &loadKey })
         addAndMakeVisible(key);

      startTimerHz(4);
   }

   juce::Rectangle<int> Computer::getGlassArea() const
   {
      return getLocalBounds().reduced(16).withTrimmedBottom(40);
   }

   void Computer::resized()
   {
      // the screen is exactly the size of the world the balls live in
      const auto glass = getGlassArea();
      canvas.setBounds(glass.reduced(6).withTrimmedBottom(STATUS_HEIGHT)
                           .withSizeKeepingCentre(static_cast<int>(BallWorld::WIDTH), static_cast<int>(BallWorld::HEIGHT)));

      auto keys = getLocalBounds().reduced(16, 10).removeFromBottom(32);
      keys.removeFromLeft(150); // room for the name badge
      for (auto* key : { &drawKey, &ballKey, &clearBallsKey, &clearSitesKey, &loadKey })
      {
         key->setBounds(keys.removeFromLeft(key == &drawKey || key == &ballKey ? 72 : 92));
         keys.removeFromLeft(6);
      }
   }

   void Computer::timerCallback()
   {
      // keep the keys honest if the mode got changed some other way
      drawKey.setToggleState(canvas.getMode() == Canvas::Mode::DrawSites, juce::dontSendNotification);
      ballKey.setToggleState(canvas.getMode() == Canvas::Mode::DropBalls, juce::dontSendNotification);

      cursorOn = ! cursorOn;
      ticks++;
      repaint(getGlassArea().removeFromBottom(STATUS_HEIGHT + 6));
   }

   void Computer::paint(juce::Graphics& g)
   {
      const auto bounds = getLocalBounds().toFloat();

      // housing, the same flat box as the sections
      Theme::drawSection(g, bounds, {});

      // bezel around the screen
      const auto glass = getGlassArea().toFloat();
      Theme::drawWell(g, glass.expanded(4.f), 6.f);
      g.setColour(Theme::screen);
      g.fillRoundedRectangle(glass, 4.f);

      // name badge and power light in the strip under the screen
      auto strip = getLocalBounds().reduced(16, 10).removeFromBottom(32).toFloat();
      g.setFont(Theme::titleFont(11.f));
      g.setColour(Theme::label);
      g.drawText("VX-1", strip.removeFromLeft(40.f), juce::Justification::centredLeft);
      g.setFont(Theme::labelFont(8.f));
      g.setColour(Theme::labelDim);
      g.drawText("GEOMETRY COMPUTER", strip.removeFromLeft(100.f), juce::Justification::centredLeft);

      const auto led = juce::Rectangle<float>(8.f, 8.f).withCentre({ bounds.getRight() - 28.f, strip.getCentreY() });
      g.setColour(Theme::accent.withAlpha(0.25f));
      g.fillEllipse(led.expanded(3.f));
      g.setColour(Theme::accent);
      g.fillEllipse(led.reduced(1.f));
      g.setFont(Theme::labelFont(8.f));
      g.setColour(Theme::labelDim);
      g.drawText("PWR", led.translated(-30.f, 0.f).withWidth(24.f).expanded(0.f, 4.f), juce::Justification::centredRight);
   }

   void Computer::paintOverChildren(juce::Graphics& g)
   {
      const auto glass = getGlassArea().toFloat();

      // status line along the bottom of the screen
      auto status = glass.reduced(6.f).removeFromBottom(static_cast<float>(STATUS_HEIGHT));
      g.setColour(Theme::screen.darker(0.5f));
      g.fillRect(status);
      g.setColour(Theme::phosphor.withAlpha(0.25f));
      g.drawHorizontalLine(static_cast<int>(status.getY()), status.getX(), status.getRight());

      const auto waveNames = getWaveformNames();
      const juce::String text = juce::String("VORONOISE/OS")
                                + "   SITES " + juce::String(canvas.getNumSites()).paddedLeft('0', 2)
                                + "   BALLS " + juce::String(canvas.getNumBalls()).paddedLeft('0', 3)
                                + "   MODE " + (canvas.getMode() == Canvas::Mode::DrawSites ? "DRAW" : "BALL")
                                + "   WAVE " + waveNames[static_cast<int>(canvas.getSound())].toUpperCase()
                                + "   > " + (cursorOn ? "_" : " ");
      g.setFont(Theme::monoFont(11.f));
      g.setColour(Theme::phosphor);
      g.drawText(text, status.reduced(8.f, 0.f), juce::Justification::centredLeft);

      // glass: a faint reflection and rounded corners
      g.setGradientFill(juce::ColourGradient(Theme::highlight.withAlpha(0.04f), glass.getX(), glass.getY(),
                                             Theme::highlight.withAlpha(0.f), glass.getX() + glass.getWidth() * 0.45f,
                                             glass.getY() + glass.getHeight() * 0.6f, false));
      g.fillRoundedRectangle(glass, 4.f);
      g.setColour(Theme::well);
      g.drawRoundedRectangle(glass.reduced(1.f), 4.f, 2.f);
   }

   //==============================================================================
   Section::Section(const juce::String& t) : title(t) {}

   void Section::addRow(std::vector<Item> items, int height)
   {
      for (auto& item : items)
         if (item.component != nullptr && item.component->getParentComponent() != this)
            addAndMakeVisible(item.component);
      rows.emplace_back(std::move(items), height);
   }

   ParamControl* Section::add(std::unique_ptr<ParamControl> control)
   {
      auto* raw = control.get();
      owned.push_back(std::move(control));
      return raw;
   }

   juce::Component* Section::addOwned(std::unique_ptr<juce::Component> component)
   {
      auto* raw = component.get();
      owned.push_back(std::move(component));
      return raw;
   }

   void Section::paint(juce::Graphics& g)
   {
      Theme::drawSection(g, getLocalBounds().toFloat(), title);
   }

   void Section::resized()
   {
      auto area = getLocalBounds().reduced(10, 6);
      area.removeFromTop(TITLE_HEIGHT - 6);

      for (auto& [items, height] : rows)
      {
         auto row = area.removeFromTop(height);
         float total = 0.f;
         for (const auto& item : items)
            total += item.width;

         const float unit = total > 0.f ? static_cast<float>(row.getWidth()) / total : 0.f;
         float x = static_cast<float>(row.getX());
         for (const auto& item : items)
         {
            const float width = item.width * unit;
            if (item.component != nullptr)
               item.component->setBounds(juce::Rectangle<float>(x, static_cast<float>(row.getY()), width, static_cast<float>(row.getHeight()))
                                             .toNearestInt().reduced(2, 0));
            x += width;
         }
      }
   }

   //==============================================================================
   EnvelopeDisplay::EnvelopeDisplay(VoronoiseAudioProcessor& p) : processor(p)
   {
      timerCallback();
      startTimerHz(20);
   }

   void EnvelopeDisplay::timerCallback()
   {
      const std::array<float, 4> now { processor.getParamValue(Params::AMP_ATTACK), processor.getParamValue(Params::AMP_DECAY),
                                       processor.getParamValue(Params::AMP_SUSTAIN), processor.getParamValue(Params::AMP_RELEASE) };
      if (now != shown)
      {
         shown = now;
         repaint();
      }
   }

   juce::Rectangle<float> EnvelopeDisplay::getGraphArea() const
   {
      return getLocalBounds().toFloat().reduced(8.f, 8.f);
   }

   EnvelopeDisplay::Shape EnvelopeDisplay::getShape() const
   {
      const auto area = getGraphArea();
      auto& apvts = processor.apvts;

      // fixed scale: each stage owns a slice of the width and sits as far along it as its knob is
      // turned, so nothing rescales when one stage gets longer and there's always room to grow
      auto knob = [&apvts](const char* id, float value) { return apvts.getParameter(id)->convertTo0to1(value); };
      const float attack = ATTACK_WIDTH * knob(Params::AMP_ATTACK, shown[0]);
      const float decay = DECAY_WIDTH * knob(Params::AMP_DECAY, shown[1]);
      const float release = RELEASE_WIDTH * knob(Params::AMP_RELEASE, shown[3]);

      auto xAt = [&](float proportion) { return area.getX() + area.getWidth() * proportion; };
      auto yAt = [&](float level) { return area.getBottom() - area.getHeight() * level; };

      const float sustain = shown[2];
      return { { xAt(0.f), yAt(0.f) }, { xAt(attack), yAt(1.f) }, { xAt(attack + decay), yAt(sustain) },
               { xAt(attack + decay + HOLD_WIDTH), yAt(sustain) }, { xAt(attack + decay + HOLD_WIDTH + release), yAt(0.f) } };
   }

   juce::Point<float> EnvelopeDisplay::getHandlePosition(Handle handle) const
   {
      const auto shape = getShape();
      switch (handle)
      {
         case Handle::Peak:    return shape.peak;
         case Handle::Decay:   return shape.decay;
         case Handle::NoteOff: return shape.noteOff;
         case Handle::End:     return shape.end;
         case Handle::None:
         default:              return {};
      }
   }

   EnvelopeDisplay::Handle EnvelopeDisplay::findHandle(juce::Point<float> position) const
   {
      Handle best = Handle::None;
      float bestDistance = 10.f;
      for (auto handle : { Handle::Peak, Handle::Decay, Handle::NoteOff, Handle::End })
      {
         const float distance = getHandlePosition(handle).getDistanceFrom(position);
         if (distance < bestDistance)
         {
            best = handle;
            bestDistance = distance;
         }
      }
      return best;
   }

   std::vector<juce::RangedAudioParameter*> EnvelopeDisplay::getParamsFor(Handle handle) const
   {
      auto& apvts = processor.apvts;
      switch (handle)
      {
         case Handle::Peak:    return { apvts.getParameter(Params::AMP_ATTACK) };
         case Handle::Decay:   return { apvts.getParameter(Params::AMP_DECAY), apvts.getParameter(Params::AMP_SUSTAIN) };
         case Handle::NoteOff: return { apvts.getParameter(Params::AMP_SUSTAIN) };
         case Handle::End:     return { apvts.getParameter(Params::AMP_RELEASE) };
         case Handle::None:
         default:              return {};
      }
   }

   void EnvelopeDisplay::setValue(const char* id, float value)
   {
      auto* param = processor.apvts.getParameter(id);
      param->setValueNotifyingHost(param->convertTo0to1(param->getNormalisableRange().snapToLegalValue(value)));
   }

   void EnvelopeDisplay::beginDrag(Handle handle)
   {
      dragging = handle;
      dragStart = { processor.getBaseValue(Params::AMP_ATTACK), processor.getBaseValue(Params::AMP_DECAY),
                    processor.getBaseValue(Params::AMP_SUSTAIN), processor.getBaseValue(Params::AMP_RELEASE) };
      for (auto* param : getParamsFor(handle))
         param->beginChangeGesture();
   }

   void EnvelopeDisplay::dragBy(float dx, float dy)
   {
      // times scale with the drag (every 25px doubles or halves them), so tiny attacks and
      // long releases are equally easy to set. levels follow the mouse directly
      auto scaled = [dx](float startTime) { return juce::jmax(startTime, 1.f) * std::exp2(dx / PIXELS_PER_DOUBLING); };
      const float sustain = dragStart[2] - dy / getGraphArea().getHeight();

      switch (dragging)
      {
         case Handle::Peak:
            setValue(Params::AMP_ATTACK, dx < 0.f && dragStart[0] < 1.f ? 0.f : scaled(dragStart[0]));
            break;
         case Handle::Decay:
            setValue(Params::AMP_DECAY, scaled(dragStart[1]));
            setValue(Params::AMP_SUSTAIN, sustain);
            break;
         case Handle::NoteOff:
            setValue(Params::AMP_SUSTAIN, sustain);
            break;
         case Handle::End:
            setValue(Params::AMP_RELEASE, scaled(dragStart[3]));
            break;
         case Handle::None:
         default:
            break;
      }

      timerCallback();
      if (auto* display = findParentComponentOfClass<ParamDisplay>())
      {
         auto params = getParamsFor(dragging);
         if (! params.empty())
            display->showParameter(params.front()->getName(32), params.front()->getCurrentValueAsText()
                                   + (params.size() > 1 ? "  /  " + params.back()->getCurrentValueAsText() : juce::String()));
      }
   }

   void EnvelopeDisplay::endDrag()
   {
      for (auto* param : getParamsFor(dragging))
         param->endChangeGesture();
      dragging = Handle::None;
   }

   void EnvelopeDisplay::resetHandle(Handle handle)
   {
      for (auto* param : getParamsFor(handle))
      {
         param->beginChangeGesture();
         param->setValueNotifyingHost(param->getDefaultValue());
         param->endChangeGesture();
      }
      timerCallback();
   }

   void EnvelopeDisplay::mouseMove(const juce::MouseEvent& e)
   {
      const auto handle = findHandle(e.position);
      if (handle != hovered)
      {
         hovered = handle;
         setMouseCursor(hovered == Handle::None ? juce::MouseCursor::NormalCursor
                        : hovered == Handle::NoteOff ? juce::MouseCursor::UpDownResizeCursor
                        : hovered == Handle::Decay ? juce::MouseCursor::UpDownLeftRightResizeCursor
                                                   : juce::MouseCursor::LeftRightResizeCursor);
         repaint();
      }
   }

   void EnvelopeDisplay::mouseExit(const juce::MouseEvent&)
   {
      hovered = Handle::None;
      repaint();
   }

   void EnvelopeDisplay::mouseDown(const juce::MouseEvent& e)
   {
      const auto handle = findHandle(e.position);
      if (handle != Handle::None)
         beginDrag(handle);
   }

   void EnvelopeDisplay::mouseDrag(const juce::MouseEvent& e)
   {
      if (dragging != Handle::None)
         dragBy(static_cast<float>(e.getDistanceFromDragStartX()), static_cast<float>(e.getDistanceFromDragStartY()));
   }

   void EnvelopeDisplay::mouseUp(const juce::MouseEvent&)
   {
      if (dragging != Handle::None)
         endDrag();
   }

   void EnvelopeDisplay::mouseDoubleClick(const juce::MouseEvent& e)
   {
      resetHandle(findHandle(e.position));
   }

   void EnvelopeDisplay::paint(juce::Graphics& g)
   {
      const auto bounds = getLocalBounds().toFloat();
      Theme::drawWell(g, bounds);

      const auto area = getGraphArea();
      const auto shape = getShape();

      juce::Path line;
      line.startNewSubPath(shape.start);
      line.lineTo(shape.peak);
      line.lineTo(shape.decay);
      line.lineTo(shape.noteOff);
      line.lineTo(shape.end);

      juce::Path fill(line);
      fill.closeSubPath();
      g.setGradientFill(juce::ColourGradient(Theme::accent.withAlpha(0.35f), area.getX(), area.getY(),
                                             Theme::accent.withAlpha(0.03f), area.getX(), area.getBottom(), false));
      g.fillPath(fill);
      g.setColour(Theme::accent);
      g.strokePath(line, juce::PathStrokeType(2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

      // note off marker
      g.setColour(Theme::labelDim.withAlpha(0.5f));
      g.drawVerticalLine(static_cast<int>(shape.noteOff.x), area.getY(), area.getBottom());

      // grabbable points, bigger when hovered or held
      for (auto handle : { Handle::Peak, Handle::Decay, Handle::NoteOff, Handle::End })
      {
         const bool hot = handle == hovered || handle == dragging;
         const float size = hot ? 9.f : 6.f;
         const auto point = juce::Rectangle<float>(size, size).withCentre(getHandlePosition(handle));
         g.setColour(hot ? Theme::highlight : Theme::accent);
         g.fillEllipse(point);
         g.setColour(Theme::well);
         g.drawEllipse(point, 1.f);
      }
   }

   //==============================================================================
   void Stack::add(juce::Component* component, int height)
   {
      addAndMakeVisible(component);
      items.emplace_back(component, height);
   }

   void Stack::resized()
   {
      auto area = getLocalBounds();
      for (auto& [component, height] : items)
         component->setBounds(area.removeFromTop(height));
   }

   //==============================================================================
   NoteFaders::NoteFaders(VoronoiseAudioProcessor& p) : processor(p)
   {
      const auto names = Scale::getNoteNames();
      for (int pc = 0; pc < 12; pc++)
      {
         auto fader = std::make_unique<ParamControl>(processor, Params::noteWeight(pc), names[pc], ParamControl::Style::Fader);
         addAndMakeVisible(*fader);
         faders.push_back(std::move(fader));
      }
      timerCallback();
      startTimerHz(8);
   }

   void NoteFaders::resized()
   {
      auto area = getLocalBounds();
      const float width = static_cast<float>(area.getWidth()) / 12.f;
      for (size_t i = 0; i < faders.size(); i++)
      {
         const int x = static_cast<int>(width * static_cast<float>(i));
         faders[i]->setBounds(x, 0, static_cast<int>(width * static_cast<float>(i + 1)) - x, area.getHeight());
      }
   }

   void NoteFaders::timerCallback()
   {
      const auto settings = processor.getNoteSettings();
      auto mask = Scale::getPitchClassMask(settings.type, settings.root);
      if (! settings.lockToScale)
         mask.fill(true);

      if (mask == inScale)
         return;

      // notes outside the scale can't play, so their probability doesn't matter
      inScale = mask;
      for (size_t i = 0; i < faders.size(); i++)
         faders[i]->setAlpha(inScale[i] ? 1.f : 0.3f);
   }

   //==============================================================================
   MainPanel::MainPanel(VoronoiseAudioProcessor& p)
       : processor(p), computer(p, [this] { chooseSample(); }), lfoPanel(p),
         keyboard(p.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
   {
      addAndMakeVisible(computer);
      addAndMakeVisible(lfoPanel);

      // six octaves fit across the panel, so no scrolling. c4 is middle c like everywhere else
      keyboard.setAvailableRange(24, 96);
      keyboard.setScrollButtonsVisible(false);
      keyboard.setOctaveForMiddleC(4);
      keyboard.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, Theme::keyWhite);
      keyboard.setColour(juce::MidiKeyboardComponent::blackNoteColourId, Theme::keyBlack);
      keyboard.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, Theme::keyBlack);
      keyboard.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, Theme::highlight.withAlpha(0.25f));
      keyboard.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, Theme::highlight.withAlpha(0.85f));
      keyboard.setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::black.withAlpha(0.3f));
      keyboard.setColour(juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, Theme::control);
      keyboard.setColour(juce::MidiKeyboardComponent::upDownButtonArrowColourId, Theme::labelDim);
      keyboard.setColour(juce::MidiKeyboardComponent::textLabelColourId, Theme::keyBlack);
      addAndMakeVisible(keyboard);

      buildSections();

      for (auto* tab : { &synthTab, &ballsTab, &notesTab, &sampleTab })
      {
         tab->setClickingTogglesState(true);
         tab->setRadioGroupId(3001);
         addAndMakeVisible(tab);
      }
      synthTab.onClick = [this] { showPage(Page::Synth); };
      ballsTab.onClick = [this] { showPage(Page::Balls); };
      notesTab.onClick = [this] { showPage(Page::Notes); };
      sampleTab.onClick = [this] { showPage(Page::Sample); };
      showPage(Page::Synth);

      setSize(WIDTH, HEIGHT);
      startTimerHz(10);
   }

   void MainPanel::buildSections()
   {
      using Style = ParamControl::Style;
      auto make = [this](Section& section, const juce::String& id, const juce::String& name = {}, Style style = Style::Auto) {
         return section.add(std::make_unique<ParamControl>(processor, id, name, style));
      };

      geometry = std::make_unique<Section>("Geometry");
      geometry->addRow({ { make(*geometry, Params::LINES, "Lines", Style::Switch), 1.f } }, 46);
      geometry->addRow({ { make(*geometry, Params::METRIC, "Metric", Style::Switch), 1.f } }, 46);

      auto* play = (midi = std::make_unique<Section>("Play")).get();
      play->addRow({ { make(*play, Params::MIDI_DROP_CHANCE, "Midi drop"), 1.f },
                     { make(*play, Params::OCTAVE_SHIFT, "Octave"), 1.f },
                     { make(*play, Params::SNAP_MIDI, "Snap midi"), 1.f } }, 84);
      play->addRow({ { make(*play, Params::PITCH_SOURCE, "Dropped pitch", Style::Switch), 2.f },
                     { make(*play, Params::SCALE_LOCK, "Scale lock"), 1.f } }, 54);

      // every time a ball hits a line or the border, these can jump around
      hitRandom = std::make_unique<Section>("Hit random");
      hitRandom->addRow({ { make(*hitRandom, Params::RND_POSITION, "WT pos"), 1.f },
                          { make(*hitRandom, Params::RND_PITCH, "Pitch"), 1.f },
                          { make(*hitRandom, Params::RND_LEVEL, "Level"), 1.f },
                          { make(*hitRandom, Params::RND_PAN, "Pan"), 1.f },
                          { make(*hitRandom, Params::HIT_DYNAMICS, "Dynamics"), 1.f } }, 90);

      // the synth: wavetable on top, amp envelope under it
      synthPage = std::make_unique<Section>("Synth");
      auto view = std::make_unique<WavetableView>(processor);
      wavetableView = view.get();
      auto* stack = static_cast<Stack*>(synthPage->addOwned(std::make_unique<Stack>()));
      stack->add(make(*synthPage, Params::WT_POSITION, "WT pos"), 96);
      stack->add(make(*synthPage, Params::BALL_SOUND, "Ball sound", Style::Switch), 50);
      synthPage->addRow({ { synthPage->addOwned(std::move(view)), 3.2f }, { stack, 1.f } }, 150);
      synthPage->addRow({ { nullptr, 1.f } }, 8); // breathing room
      synthPage->addRow({ { synthPage->addOwned(std::make_unique<EnvelopeDisplay>(processor)), 2.4f },
                          { make(*synthPage, Params::AMP_ATTACK, "Attack"), 1.f },
                          { make(*synthPage, Params::AMP_DECAY, "Decay"), 1.f },
                          { make(*synthPage, Params::AMP_SUSTAIN, "Sustain"), 1.f },
                          { make(*synthPage, Params::AMP_RELEASE, "Release"), 1.f },
                          { make(*synthPage, Params::HIT_LENGTH, "Hit len"), 1.f },
                          { make(*synthPage, Params::KEYS_LEVEL, "Level"), 1.f } }, 98);

      ballsPage = std::make_unique<Section>("Balls");
      ballsPage->addRow({ { make(*ballsPage, Params::GRAVITY, "Gravity"), 1.f },
                          { make(*ballsPage, Params::BOUNCINESS, "Bounce"), 1.f },
                          { make(*ballsPage, Params::MIN_BOUNCE, "Min bounce"), 1.f },
                          { make(*ballsPage, Params::DRAG, "Air drag"), 1.f },
                          { make(*ballsPage, Params::PASS_THROUGH, "Pass thru"), 1.f },
                          { make(*ballsPage, Params::PASS_THROUGH_PLAYS, "Pass plays"), 1.f } }, 96);
      ballsPage->addRow({ { make(*ballsPage, Params::BALL_SIZE, "Size"), 1.f },
                          { make(*ballsPage, Params::SIZE_VARIATION, "Size var"), 1.f },
                          { make(*ballsPage, Params::LIFETIME, "Lifetime"), 1.f },
                          { make(*ballsPage, Params::LAUNCH_SPEED, "Launch"), 1.f },
                          { make(*ballsPage, Params::MAX_BALLS, "Max balls"), 1.f } }, 96);

      notesPage = std::make_unique<Section>("Notes");
      notesPage->addRow({ { make(*notesPage, Params::SCALE_ROOT, "Key", Style::Menu), 1.f },
                          { make(*notesPage, Params::SCALE_TYPE, "Scale", Style::Menu), 2.f },
                          { make(*notesPage, Params::LOWEST_NOTE, "Lowest"), 1.f },
                          { make(*notesPage, Params::HIGHEST_NOTE, "Highest"), 1.f } }, 84);
      notesPage->addRow({ { notesPage->addOwned(std::make_unique<NoteFaders>(processor)), 1.f } }, 150);

      samplePage = std::make_unique<Section>("Sample");
      samplePage->addRow({ { samplePage->addOwned(std::make_unique<SampleView>(processor, [this] { chooseSample(); })), 1.f } }, 92);
      samplePage->addRow({ { make(*samplePage, Params::SAMPLE_START, "Start"), 1.f },
                           { make(*samplePage, Params::SAMPLE_END, "End"), 1.f },
                           { make(*samplePage, Params::SLICE_COUNT, "Slices"), 1.f },
                           { make(*samplePage, Params::SENSITIVITY, "Sens"), 1.f },
                           { make(*samplePage, Params::ROOT_NOTE, "Root"), 1.f },
                           { make(*samplePage, Params::TRANSPOSE, "Transp"), 1.f },
                           { make(*samplePage, Params::FINE_TUNE, "Fine"), 1.f },
                           { make(*samplePage, Params::SAMPLE_GAIN, "Gain"), 1.f } }, 78);
      samplePage->addRow({ { make(*samplePage, Params::SLICE_MODE, "Slice by", Style::Switch), 2.f },
                           { make(*samplePage, Params::SLICE_PICK, "Slice played", Style::Switch), 3.f },
                           { make(*samplePage, Params::REVERSE, "Reverse"), 1.f },
                           { make(*samplePage, Params::TRACK_PITCH, "Track"), 1.f },
                           { make(*samplePage, Params::FADE_IN, "Fade in"), 1.f },
                           { make(*samplePage, Params::FADE_OUT, "Fade out"), 1.f } }, 74);

      for (auto* section : { geometry.get(), midi.get(), hitRandom.get(), synthPage.get(), ballsPage.get(), notesPage.get(), samplePage.get() })
         addChildComponent(section);
      hitRandom->setVisible(true);
      geometry->setVisible(true);
      midi->setVisible(true);
   }

   void MainPanel::showPage(Page newPage)
   {
      page = newPage;
      synthPage->setVisible(page == Page::Synth);
      ballsPage->setVisible(page == Page::Balls);
      notesPage->setVisible(page == Page::Notes);
      samplePage->setVisible(page == Page::Sample);

      synthTab.setToggleState(page == Page::Synth, juce::dontSendNotification);
      ballsTab.setToggleState(page == Page::Balls, juce::dontSendNotification);
      notesTab.setToggleState(page == Page::Notes, juce::dontSendNotification);
      sampleTab.setToggleState(page == Page::Sample, juce::dontSendNotification);

      // tabs sit on top of whichever page is showing
      for (auto* tab : { &synthTab, &ballsTab, &notesTab, &sampleTab })
         tab->toFront(false);
   }

   void MainPanel::chooseSample()
   {
      fileChooser = std::make_unique<juce::FileChooser>("Pick a sample", juce::File(), processor.getSampleWildcard());

      // async so the plugin window doesn't block the host
      fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                               [safeThis = juce::Component::SafePointer(this)](const juce::FileChooser& chooser)
                               {
                                  auto file = chooser.getResult();
                                  if (safeThis == nullptr || file == juce::File())
                                     return;

                                  if (safeThis->processor.loadSample(file))
                                  {
                                     safeThis->showParameter("Sample", file.getFileNameWithoutExtension());
                                     safeThis->showPage(Page::Sample);
                                  }
                                  else
                                  {
                                     juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Couldn't load sample",
                                                                            "Voronoise can't read " + file.getFileName());
                                  }
                               });
   }

   void MainPanel::showParameter(const juce::String& name, const juce::String& value)
   {
      displayName = name.toUpperCase();
      displayValue = value;
      displayUntil = juce::Time::getMillisecondCounterHiRes() + 2000.0;
      repaint(displayArea);
   }

   juce::String MainPanel::getDisplayText() const
   {
      if (juce::Time::getMillisecondCounterHiRes() < displayUntil)
         return displayName + "\n" + displayValue;

      const auto sample = processor.getSampleName();
      return juce::String("VORONOISE") + "\n" + (sample.isEmpty() ? juce::String("NO SAMPLE") : "SMP " + sample.toUpperCase());
   }

   void MainPanel::timerCallback()
   {
      repaint(displayArea);
   }

   void Keyboard::drawBlackNote(int, juce::Graphics& g, juce::Rectangle<float> area, bool isDown, bool isOver, juce::Colour noteFillColour)
   {
      auto colour = noteFillColour;
      if (isDown)
         colour = colour.overlaidWith(findColour(keyDownOverlayColourId));
      else if (isOver)
         colour = colour.overlaidWith(findColour(mouseOverKeyOverlayColourId));

      g.setColour(colour);
      g.fillRect(area);

      // a thin lit edge along the front of the key
      g.setColour(isDown ? Theme::highlight : Theme::controlEdge);
      g.fillRect(area.reduced(2.f, 0.f).removeFromBottom(2.f).translated(0.f, -2.f));
   }

   void MainPanel::paint(juce::Graphics& g)
   {
      const auto bounds = getLocalBounds().toFloat();
      Theme::drawPanel(g, bounds);
      Theme::drawSection(g, keysArea.toFloat(), "Keys");

      // logo
      auto header = headerArea.toFloat();
      g.setFont(Theme::titleFont(30.f).withExtraKerningFactor(0.3f));
      g.setColour(Theme::highlight);
      g.drawText("VORONOISE", header.withTrimmedLeft(14.f).withHeight(44.f).translated(0.f, 6.f), juce::Justification::centredLeft);
      g.setColour(Theme::accent);
      g.fillRect(header.getX() + 16.f, header.getY() + 48.f, 54.f, 2.f);
      g.setFont(Theme::labelFont(10.f).withExtraKerningFactor(0.2f));
      g.setColour(Theme::labelDim);
      g.drawText("GEOMETRIC PERCUSSION SYNTHESIZER", header.withTrimmedLeft(78.f).withTop(header.getY() + 42.f).withHeight(14.f),
                 juce::Justification::centredLeft);

      g.setFont(Theme::titleFont(12.f));
      g.setColour(Theme::label);
      g.drawText("JUNESOUND", header.withTrimmedRight(14.f).withHeight(40.f).translated(0.f, 6.f), juce::Justification::centredRight);
      g.setFont(Theme::labelFont(9.f));
      g.setColour(Theme::labelDim);
      g.drawText("WAVETABLE  /  3 LFO  /  V0.2", header.withTrimmedRight(14.f).withTop(header.getY() + 38.f).withHeight(14.f),
                 juce::Justification::centredRight);

      // lcd
      auto lcd = displayArea.toFloat();
      Theme::drawWell(g, lcd.expanded(4.f), 5.f);
      g.setColour(Theme::lcdText.withAlpha(0.03f));
      for (float y = lcd.getY(); y < lcd.getBottom(); y += 2.f)
         g.drawHorizontalLine(static_cast<int>(y), lcd.getX(), lcd.getRight());

      const auto lines = juce::StringArray::fromLines(getDisplayText());
      g.setFont(Theme::monoFont(14.f));
      g.setColour(Theme::lcdText);
      g.drawText(lines[0], lcd.reduced(12.f, 3.f).removeFromTop(lcd.getHeight() * 0.5f), juce::Justification::centredLeft);
      g.setColour(Theme::lcdText.withAlpha(0.8f));
      g.drawText(lines[1], lcd.reduced(12.f, 3.f).removeFromBottom(lcd.getHeight() * 0.5f), juce::Justification::centredLeft);
   }

   void MainPanel::resized()
   {
      headerArea = { 40, 0, WIDTH - 80, 66 };
      displayArea = { 470, 14, 340, 40 };

      computer.setBounds(40, 74, 790, 452);

      geometry->setBounds(842, 74, 398, 128);
      midi->setBounds(842, 210, 398, 176);
      hitRandom->setBounds(842, 394, 398, 132);

      lfoPanel.setBounds(40, 538, 620, 310);

      deckArea = { 672, 538, 568, 310 };
      for (auto* section : { synthPage.get(), ballsPage.get(), notesPage.get(), samplePage.get() })
         section->setBounds(deckArea);

      keysArea = { 40, 860, WIDTH - 80, 130 };
      auto keys = keysArea.reduced(12, 0).withTrimmedTop(Section::TITLE_HEIGHT).withTrimmedBottom(10);
      keyboard.setBounds(keys);
      keyboard.setKeyWidth(static_cast<float>(keys.getWidth()) / 43.f); // white keys from c1 to c7

      // page tabs in the deck's title strip
      auto tabs = deckArea.withHeight(Section::TITLE_HEIGHT).reduced(8, 3).removeFromRight(320);
      for (auto* tab : { &synthTab, &ballsTab, &notesTab, &sampleTab })
      {
         tab->setBounds(tabs.removeFromLeft(76));
         tabs.removeFromLeft(4);
      }
   }
}
