#include "Voronoise/ui/WavetableView.h"
#include "Voronoise/ui/ParamControl.h"
#include "Voronoise/ui/Theme.h"

namespace UI
{
   namespace
   {
      constexpr int HEADER_HEIGHT = 24;
      constexpr double COMMIT_INTERVAL_MS = 60.0; // how often a drag rebuilds the table while drawing
   }

   WavetableView::WavetableView(VoronoiseAudioProcessor& p) : processor(p)
   {
      drawButton.setClickingTogglesState(true);
      drawButton.setTooltip("Draw on the frame instead of looking at the whole table");
      drawButton.onClick = [this] { setDrawMode(drawButton.getToggleState()); };

      previousButton.onClick = [this] { goToFrame(getEditFrame() - 1); };
      nextButton.onClick = [this] { goToFrame(getEditFrame() + 1); };
      addButton.setTooltip("Copy this frame");
      addButton.onClick = [this] { addFrame(); };
      removeButton.setTooltip("Delete this frame");
      removeButton.onClick = [this] { removeFrame(); };
      tableButton.setTooltip("Presets, load audio, tools");
      tableButton.onClick = [this] { showTableMenu(); };

      frameLabel.setFont(Theme::monoFont(11.f));
      frameLabel.setColour(juce::Label::textColourId, Theme::lcdText);
      frameLabel.setJustificationType(juce::Justification::centred);

      for (auto* c : std::initializer_list<juce::Component*> { &drawButton, &previousButton, &frameLabel, &nextButton, &addButton, &removeButton, &tableButton })
         addAndMakeVisible(c);

      updateHeader();
      startTimerHz(30);
   }

   juce::Rectangle<float> WavetableView::getWaveArea() const
   {
      return getLocalBounds().toFloat().withTrimmedTop(static_cast<float>(HEADER_HEIGHT) + 4.f);
   }

   void WavetableView::resized()
   {
      auto header = getLocalBounds().removeFromTop(HEADER_HEIGHT);
      drawButton.setBounds(header.removeFromLeft(52));
      header.removeFromLeft(6);
      previousButton.setBounds(header.removeFromLeft(22));
      frameLabel.setBounds(header.removeFromLeft(74));
      nextButton.setBounds(header.removeFromLeft(22));
      header.removeFromLeft(6);
      addButton.setBounds(header.removeFromLeft(22));
      removeButton.setBounds(header.removeFromLeft(22));
      tableButton.setBounds(header.removeFromRight(60));
   }

   void WavetableView::updateHeader()
   {
      const auto table = processor.getWavetable();
      frameLabel.setText("FRAME " + juce::String(getEditFrame() + 1) + "/" + juce::String(table->getNumFrames()), juce::dontSendNotification);
      removeButton.setEnabled(table->getNumFrames() > 1);
      addButton.setEnabled(table->getNumFrames() < Wavetable::MAX_FRAMES);
   }

   void WavetableView::timerCallback()
   {
      // keep a drag's edits flowing to the sound without rebuilding on every mouse move
      if (dirty && juce::Time::getMillisecondCounterHiRes() - lastCommitMs > COMMIT_INTERVAL_MS)
         commit();

      const float position = processor.getParamValue(Params::WT_POSITION);
      auto* table = processor.getWavetable().get();
      if (! juce::approximatelyEqual(position, shownPosition) || table != shownTable)
      {
         shownPosition = position;
         shownTable = table;
         updateHeader();
         repaint();
      }
   }

   int WavetableView::getEditFrame() const
   {
      const int frames = processor.getWavetable()->getNumFrames();
      return juce::roundToInt(processor.getBaseValue(Params::WT_POSITION) * static_cast<float>(frames - 1));
   }

   void WavetableView::goToFrame(int frame)
   {
      const int frames = processor.getWavetable()->getNumFrames();
      frame = juce::jlimit(0, frames - 1, frame);
      auto* param = processor.apvts.getParameter(Params::WT_POSITION);
      param->beginChangeGesture();
      param->setValueNotifyingHost(frames > 1 ? static_cast<float>(frame) / static_cast<float>(frames - 1) : 0.f);
      param->endChangeGesture();
      updateHeader();
      repaint();
   }

   void WavetableView::setDrawMode(bool shouldDraw)
   {
      drawMode = shouldDraw;
      drawButton.setToggleState(drawMode, juce::dontSendNotification);
      repaint();
   }

   void WavetableView::addFrame()
   {
      const auto table = processor.getWavetable();
      if (table->getNumFrames() >= Wavetable::MAX_FRAMES)
         return;

      const int frame = getEditFrame();
      auto frames = table->getFrames();
      frames.insert(frames.begin() + frame + 1, frames[static_cast<size_t>(frame)]);
      processor.setWavetable(new Wavetable(frames, table->getName()));
      goToFrame(frame + 1);
   }

   void WavetableView::removeFrame()
   {
      const auto table = processor.getWavetable();
      if (table->getNumFrames() <= 1)
         return;

      const int frame = getEditFrame();
      auto frames = table->getFrames();
      frames.erase(frames.begin() + frame);
      processor.setWavetable(new Wavetable(frames, table->getName()));
      goToFrame(juce::jmin(frame, static_cast<int>(frames.size()) - 1));
   }

   void WavetableView::applyPreset(Wavetable::Preset preset)
   {
      processor.setWavetable(Wavetable::createPreset(preset, &random));
      goToFrame(0);
   }

   void WavetableView::smoothFrame()
   {
      const auto table = processor.getWavetable();
      auto frame = table->getFrame(getEditFrame());
      // a few passes of a small blur, wrapping around the cycle
      for (int pass = 0; pass < 4; pass++)
      {
         auto copy = frame;
         const auto n = copy.size();
         for (size_t i = 0; i < n; i++)
            frame[i] = 0.25f * copy[(i + n - 1) % n] + 0.5f * copy[i] + 0.25f * copy[(i + 1) % n];
      }
      processor.setWavetable(new Wavetable(*table, getEditFrame(), frame));
   }

   void WavetableView::normalise()
   {
      const auto table = processor.getWavetable();
      auto frames = table->getFrames();
      float peak = 0.f;
      for (const auto& frame : frames)
         for (float v : frame)
            peak = juce::jmax(peak, std::abs(v));
      if (peak < 1e-6f)
         return;
      for (auto& frame : frames)
         for (auto& v : frame)
            v /= peak;
      processor.setWavetable(new Wavetable(frames, table->getName()));
   }

   void WavetableView::showTableMenu()
   {
      juce::PopupMenu presets;
      const auto names = Wavetable::getPresetNames();
      for (int i = 0; i < names.size(); i++)
         presets.addItem(100 + i, names[i]);

      juce::PopupMenu menu;
      menu.addSubMenu("Presets", presets);
      menu.addItem(1, "Load audio into table...");
      menu.addSeparator();
      menu.addItem(2, "Smooth this frame");
      menu.addItem(3, "Normalise table");

      menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&tableButton),
                         [safeThis = juce::Component::SafePointer(this)](int result)
                         {
                            if (safeThis == nullptr || result == 0)
                               return;
                            if (result >= 100)
                               safeThis->applyPreset(static_cast<Wavetable::Preset>(result - 100));
                            else if (result == 1)
                               safeThis->chooseAudio();
                            else if (result == 2)
                               safeThis->smoothFrame();
                            else if (result == 3)
                               safeThis->normalise();
                         });
   }

   void WavetableView::chooseAudio()
   {
      fileChooser = std::make_unique<juce::FileChooser>("Load audio into the wavetable", juce::File(), processor.getSampleWildcard());
      fileChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                               [safeThis = juce::Component::SafePointer(this)](const juce::FileChooser& chooser)
                               {
                                  const auto file = chooser.getResult();
                                  if (safeThis == nullptr || file == juce::File())
                                     return;

                                  if (safeThis->processor.loadWavetable(file))
                                  {
                                     safeThis->goToFrame(0);
                                     if (auto* display = safeThis->findParentComponentOfClass<ParamDisplay>())
                                        display->showParameter("Wavetable", file.getFileNameWithoutExtension() + " ("
                                                               + juce::String(safeThis->processor.getWavetable()->getNumFrames()) + " frames)");
                                  }
                                  else
                                  {
                                     juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, "Couldn't load audio",
                                                                            "Voronoise can't make a wavetable from " + file.getFileName());
                                  }
                               });
   }

   //==============================================================================
   void WavetableView::drawStroke(juce::Point<float> from, juce::Point<float> to)
   {
      const auto area = getWaveArea().reduced(6.f);
      const auto table = processor.getWavetable();
      const int frame = getEditFrame();

      if (editingFrame != frame || editing.size() != static_cast<size_t>(Wavetable::FRAME_SIZE))
      {
         editing = table->getFrame(frame);
         editingFrame = frame;
      }

      auto toIndex = [&](float x) { return juce::jlimit(0, Wavetable::FRAME_SIZE - 1,
                                                        static_cast<int>((x - area.getX()) / area.getWidth() * Wavetable::FRAME_SIZE)); };
      auto toValue = [&](float y) { return juce::jlimit(-1.f, 1.f, 1.f - 2.f * (y - area.getY()) / area.getHeight()); };

      // fill every sample between the two points so fast strokes don't leave gaps
      const int a = toIndex(from.x), b = toIndex(to.x);
      const float va = toValue(from.y), vb = toValue(to.y);
      const int lo = juce::jmin(a, b), hi = juce::jmax(a, b);
      for (int i = lo; i <= hi; i++)
      {
         const float t = hi == lo ? 1.f : static_cast<float>(i - a) / static_cast<float>(b - a);
         editing[static_cast<size_t>(i)] = va + (vb - va) * juce::jlimit(0.f, 1.f, t);
      }

      dirty = true;
      repaint();
   }

   void WavetableView::commit()
   {
      if (! dirty || editingFrame < 0)
         return;

      processor.setWavetable(new Wavetable(*processor.getWavetable(), editingFrame, editing));
      dirty = false;
      lastCommitMs = juce::Time::getMillisecondCounterHiRes();
   }

   void WavetableView::mouseDown(const juce::MouseEvent& e)
   {
      if (! drawMode || ! getWaveArea().contains(e.position))
         return;

      editingFrame = -1; // start from the frame as it is now
      lastDrawPoint = e.position;
      drawStroke(e.position, e.position);
   }

   void WavetableView::mouseDrag(const juce::MouseEvent& e)
   {
      if (! drawMode || editingFrame < 0)
         return;

      drawStroke(lastDrawPoint, e.position);
      lastDrawPoint = e.position;
   }

   void WavetableView::mouseUp(const juce::MouseEvent&)
   {
      commit();
      editingFrame = -1;
   }

   //==============================================================================
   void WavetableView::paint(juce::Graphics& g)
   {
      const auto area = getWaveArea();
      Theme::drawWell(g, area, 5.f);

      const auto table = processor.getWavetable();
      const int frames = table->getNumFrames();
      const float position = processor.getParamValue(Params::WT_POSITION);

      auto wavePath = [](const std::vector<float>& points, juce::Rectangle<float> box) {
         juce::Path path;
         for (size_t i = 0; i < points.size(); i++)
         {
            const float x = box.getX() + box.getWidth() * static_cast<float>(i) / static_cast<float>(points.size() - 1);
            const float y = box.getCentreY() - points[i] * box.getHeight() * 0.5f;
            if (i == 0)
               path.startNewSubPath(x, y);
            else
               path.lineTo(x, y);
         }
         return path;
      };

      if (drawMode)
      {
         // one big flat frame with a centre line, and the stroke being drawn on top
         auto box = area.reduced(6.f);
         g.setColour(juce::Colours::white.withAlpha(0.08f));
         g.drawHorizontalLine(static_cast<int>(box.getCentreY()), box.getX(), box.getRight());
         for (int i = 1; i < 4; i++)
            g.drawVerticalLine(static_cast<int>(box.getX() + box.getWidth() * static_cast<float>(i) / 4.f), box.getY(), box.getBottom());

         const auto& frame = editingFrame >= 0 ? editing : table->getFrame(getEditFrame());
         std::vector<float> points(256);
         for (size_t i = 0; i < points.size(); i++)
            points[i] = frame[i * frame.size() / points.size()];
         points.push_back(frame.front());

         auto path = wavePath(points, box);
         g.setColour(Theme::accent.withAlpha(0.25f));
         g.strokePath(path, juce::PathStrokeType(5.f));
         g.setColour(Theme::accent);
         g.strokePath(path, juce::PathStrokeType(1.8f));

         g.setFont(Theme::labelFont(9.f));
         g.setColour(Theme::labelDim);
         g.drawText("CLICK AND DRAG TO DRAW", area.reduced(8.f, 4.f), juce::Justification::topRight);
         return;
      }

      // 3d stack: back frames up and to the right, front frames down and to the left
      const float depthX = area.getWidth() * 0.28f, depthY = area.getHeight() * 0.42f;
      const auto front = juce::Rectangle<float>(area.getX() + 8.f, area.getY() + depthY + 4.f,
                                                area.getWidth() - depthX - 16.f, area.getHeight() - depthY - 10.f);
      auto boxFor = [&](float depth) { return front.translated(depthX * depth, -depthY * depth); };

      // too many frames would just be a smear, so draw at most 24 of them
      const int step = juce::jmax(1, frames / 24);
      for (int f = frames - 1; f >= 0; f -= step)
      {
         const float depth = frames > 1 ? static_cast<float>(f) / static_cast<float>(frames - 1) : 0.f;
         g.setColour(Theme::phosphor.withAlpha(0.12f + 0.12f * (1.f - depth)));
         g.strokePath(wavePath(table->getFrameAt(depth, 128), boxFor(depth)), juce::PathStrokeType(1.f));
      }

      // the wave you're hearing right now, wherever the position (and any lfo on it) is
      auto playing = wavePath(table->getFrameAt(position, 200), boxFor(position));
      g.setColour(Theme::phosphor.withAlpha(0.25f));
      g.strokePath(playing, juce::PathStrokeType(5.f));
      g.setColour(Theme::phosphor);
      g.strokePath(playing, juce::PathStrokeType(2.f));

      g.setFont(Theme::monoFont(10.f));
      g.setColour(Theme::lcdText.withAlpha(0.8f));
      g.drawText(table->getName().toUpperCase(), area.reduced(8.f, 4.f), juce::Justification::topLeft);
   }
}
