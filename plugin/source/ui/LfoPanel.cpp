#include "Voronoise/ui/LfoPanel.h"
#include "Voronoise/ui/Theme.h"

namespace UI
{
   namespace
   {
      constexpr float POINT_RADIUS = 5.f;
      constexpr float HIT_DISTANCE = 9.f;
   }

   //==============================================================================
   LfoShapeEditor::LfoShapeEditor(VoronoiseAudioProcessor& p) : processor(p)
   {
      shape = processor.getLfoShape(lfo);
      startTimerHz(30);
   }

   void LfoShapeEditor::setLfo(int newLfo)
   {
      lfo = newLfo;
      shape = processor.getLfoShape(lfo);
      dragging = Dragging::Nothing;
      repaint();
   }

   void LfoShapeEditor::setGridDivisions(int divisions)
   {
      gridDivisions = divisions;
      repaint();
   }

   juce::Rectangle<float> LfoShapeEditor::getGraphArea() const
   {
      return getLocalBounds().toFloat().reduced(10.f, 10.f);
   }

   juce::Point<float> LfoShapeEditor::toScreen(float x, float y) const
   {
      const auto area = getGraphArea();
      return { area.getX() + x * area.getWidth(), area.getBottom() - y * area.getHeight() };
   }

   juce::Point<float> LfoShapeEditor::fromScreen(juce::Point<float> position) const
   {
      const auto area = getGraphArea();
      return { juce::jlimit(0.f, 1.f, (position.x - area.getX()) / area.getWidth()),
               juce::jlimit(0.f, 1.f, (area.getBottom() - position.y) / area.getHeight()) };
   }

   juce::Point<float> LfoShapeEditor::getCurveHandle(int segment) const
   {
      const auto& points = shape.getPoints();
      const auto& a = points[static_cast<size_t>(segment)];
      const auto& b = points[static_cast<size_t>(segment + 1)];
      const float x = (a.x + b.x) * 0.5f;
      const float y = a.y + (b.y - a.y) * LfoShape::applyCurve(0.5f, a.curve);
      return toScreen(x, y);
   }

   int LfoShapeEditor::findPoint(juce::Point<float> position) const
   {
      const auto& points = shape.getPoints();
      int best = -1;
      float bestDistance = HIT_DISTANCE;
      for (size_t i = 0; i < points.size(); i++)
      {
         const float distance = toScreen(points[i].x, points[i].y).getDistanceFrom(position);
         if (distance < bestDistance)
         {
            best = static_cast<int>(i);
            bestDistance = distance;
         }
      }
      return best;
   }

   int LfoShapeEditor::findCurveHandle(juce::Point<float> position) const
   {
      const auto& points = shape.getPoints();
      for (size_t i = 0; i + 1 < points.size(); i++)
      {
         // vertical jumps have nothing to bend
         if (points[i + 1].x - points[i].x < 1e-4f || std::abs(points[i + 1].y - points[i].y) < 1e-4f)
            continue;
         if (getCurveHandle(static_cast<int>(i)).getDistanceFrom(position) < HIT_DISTANCE)
            return static_cast<int>(i);
      }
      return -1;
   }

   float LfoShapeEditor::snapX(float x) const
   {
      if (gridDivisions <= 0)
         return x;
      return std::round(x * static_cast<float>(gridDivisions)) / static_cast<float>(gridDivisions);
   }

   void LfoShapeEditor::commit()
   {
      processor.setLfoShape(lfo, shape);
      repaint();
   }

   void LfoShapeEditor::timerCallback()
   {
      // picks up shape changes from elsewhere (loading a project) when not mid edit
      if (dragging == Dragging::Nothing && ! (shape == processor.getLfoShape(lfo)))
         shape = processor.getLfoShape(lfo);
      repaint();
   }

   void LfoShapeEditor::paint(juce::Graphics& g)
   {
      const auto bounds = getLocalBounds().toFloat();
      const auto area = getGraphArea();
      const auto colour = Theme::lfoColour(lfo);

      // screen
      g.setColour(juce::Colour(0xff0b0d10));
      g.fillRoundedRectangle(bounds, 5.f);
      g.setColour(juce::Colours::black);
      g.drawRoundedRectangle(bounds.reduced(0.5f), 5.f, 1.f);

      // grid
      const int columns = gridDivisions > 0 ? gridDivisions : 8;
      for (int i = 0; i <= columns; i++)
      {
         const float x = area.getX() + area.getWidth() * static_cast<float>(i) / static_cast<float>(columns);
         g.setColour(juce::Colours::white.withAlpha(i % 4 == 0 ? 0.12f : 0.05f));
         g.drawVerticalLine(static_cast<int>(x), area.getY(), area.getBottom());
      }
      for (int i = 0; i <= 4; i++)
      {
         const float y = area.getY() + area.getHeight() * static_cast<float>(i) / 4.f;
         g.setColour(juce::Colours::white.withAlpha(i == 2 ? 0.1f : 0.05f));
         g.drawHorizontalLine(static_cast<int>(y), area.getX(), area.getRight());
      }

      // the shape, sampled per pixel so the curves show properly
      juce::Path line, fill;
      const int steps = juce::jmax(2, static_cast<int>(area.getWidth()));
      for (int i = 0; i <= steps; i++)
      {
         const float x = static_cast<float>(i) / static_cast<float>(steps);
         const auto point = toScreen(x, shape.evaluate(x));
         if (i == 0)
         {
            line.startNewSubPath(point);
            fill.startNewSubPath(area.getX(), area.getBottom());
         }
         line.lineTo(point);
         fill.lineTo(point);
      }
      fill.lineTo(area.getRight(), area.getBottom());
      fill.closeSubPath();

      g.setGradientFill(juce::ColourGradient(colour.withAlpha(0.35f), area.getX(), area.getY(),
                                             colour.withAlpha(0.02f), area.getX(), area.getBottom(), false));
      g.fillPath(fill);
      g.setColour(colour);
      g.strokePath(line, juce::PathStrokeType(2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

      // bend handles between points
      const auto& points = shape.getPoints();
      for (size_t i = 0; i + 1 < points.size(); i++)
      {
         if (points[i + 1].x - points[i].x < 1e-4f || std::abs(points[i + 1].y - points[i].y) < 1e-4f)
            continue;
         const auto handle = getCurveHandle(static_cast<int>(i));
         g.setColour(colour.withAlpha(static_cast<int>(i) == hoverCurve ? 1.f : 0.6f));
         g.drawEllipse(juce::Rectangle<float>(7.f, 7.f).withCentre(handle), 1.2f);
      }

      // points
      for (size_t i = 0; i < points.size(); i++)
      {
         const auto p = toScreen(points[i].x, points[i].y);
         const bool hot = static_cast<int>(i) == hoverPoint || (dragging == Dragging::Point && static_cast<int>(i) == dragIndex);
         g.setColour(hot ? juce::Colours::white : colour.brighter(0.4f));
         g.fillEllipse(juce::Rectangle<float>(POINT_RADIUS * 2.f, POINT_RADIUS * 2.f).withCentre(p));
         g.setColour(juce::Colours::black.withAlpha(0.7f));
         g.drawEllipse(juce::Rectangle<float>(POINT_RADIUS * 2.f, POINT_RADIUS * 2.f).withCentre(p), 1.f);
      }

      // playhead
      const float phase = processor.getLfoPhase(lfo);
      const auto head = toScreen(phase, shape.evaluate(phase));
      g.setColour(juce::Colours::white.withAlpha(0.35f));
      g.drawVerticalLine(static_cast<int>(head.x), area.getY(), area.getBottom());
      g.setColour(juce::Colours::white);
      g.fillEllipse(juce::Rectangle<float>(6.f, 6.f).withCentre(head));

      // hint
      g.setFont(Theme::labelFont(9.f));
      g.setColour(Theme::labelDim);
      g.drawText("DOUBLE CLICK: ADD / REMOVE POINT   DRAG O: BEND", bounds.reduced(10.f, 3.f), juce::Justification::topRight);
   }

   void LfoShapeEditor::mouseMove(const juce::MouseEvent& e)
   {
      hoverPoint = findPoint(e.position);
      hoverCurve = hoverPoint < 0 ? findCurveHandle(e.position) : -1;
      setMouseCursor(hoverPoint >= 0 || hoverCurve >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::CrosshairCursor);
      repaint();
   }

   void LfoShapeEditor::mouseDown(const juce::MouseEvent& e)
   {
      const int point = findPoint(e.position);

      if (e.mods.isPopupMenu())
      {
         if (point >= 0)
         {
            shape.removePoint(point);
            commit();
         }
         return;
      }

      if (point >= 0)
      {
         dragging = Dragging::Point;
         dragIndex = point;
         return;
      }

      const int curve = findCurveHandle(e.position);
      if (curve >= 0)
      {
         dragging = Dragging::Curve;
         dragIndex = curve;
         dragStartCurve = shape.getPoints()[static_cast<size_t>(curve)].curve;
      }
   }

   void LfoShapeEditor::mouseDrag(const juce::MouseEvent& e)
   {
      if (dragging == Dragging::Point)
      {
         const auto value = fromScreen(e.position);
         // shift skips the grid for fine moves
         shape.movePoint(dragIndex, e.mods.isShiftDown() ? value.x : snapX(value.x), value.y);
         commit();
      }
      else if (dragging == Dragging::Curve)
      {
         const auto& points = shape.getPoints();
         const auto& a = points[static_cast<size_t>(dragIndex)];
         const auto& b = points[static_cast<size_t>(dragIndex + 1)];

         // dragging the handle down always bends the middle of the line down
         const float rising = b.y > a.y ? 1.f : -1.f;
         const float amount = static_cast<float>(e.getDistanceFromDragStartY()) / 80.f;
         shape.setCurve(dragIndex, dragStartCurve + amount * rising);
         commit();
      }
   }

   void LfoShapeEditor::mouseUp(const juce::MouseEvent&)
   {
      dragging = Dragging::Nothing;
      dragIndex = -1;
   }

   void LfoShapeEditor::mouseDoubleClick(const juce::MouseEvent& e)
   {
      const int point = findPoint(e.position);
      if (point >= 0)
      {
         shape.removePoint(point);
         commit();
         return;
      }

      const int curve = findCurveHandle(e.position);
      if (curve >= 0)
      {
         shape.setCurve(curve, 0.f);
         commit();
         return;
      }

      const auto value = fromScreen(e.position);
      shape.addPoint(snapX(value.x), value.y);
      commit();
   }

   //==============================================================================
   LfoTab::LfoTab(VoronoiseAudioProcessor& p, int l) : processor(p), lfo(l)
   {
      setMouseCursor(juce::MouseCursor::DraggingHandCursor);
      setTooltip("Click to edit, drag onto any knob to modulate it");
      startTimerHz(30);
   }

   void LfoTab::setSelected(bool shouldBeSelected)
   {
      selected = shouldBeSelected;
      repaint();
   }

   void LfoTab::timerCallback()
   {
      repaint();
   }

   void LfoTab::paint(juce::Graphics& g)
   {
      const auto bounds = getLocalBounds().toFloat().reduced(1.f);
      const auto colour = Theme::lfoColour(lfo);

      g.setColour(selected ? juce::Colour(0xff0b0d10) : juce::Colour(0xff2c2d32));
      g.fillRoundedRectangle(bounds, 4.f);
      g.setColour(selected ? colour : Theme::sectionEdge);
      g.drawRoundedRectangle(bounds, 4.f, 1.f);

      // grip dots so it reads as draggable
      g.setColour(colour.withAlpha(0.8f));
      for (int row = 0; row < 3; row++)
         for (int column = 0; column < 2; column++)
            g.fillEllipse(bounds.getX() + 7.f + 4.f * static_cast<float>(column), bounds.getCentreY() - 5.f + 4.f * static_cast<float>(row), 2.f, 2.f);

      g.setFont(Theme::titleFont(11.f));
      g.setColour(selected ? juce::Colours::white : Theme::label);
      g.drawText("LFO " + juce::String(lfo + 1), bounds.withTrimmedLeft(18.f).withTrimmedRight(12.f), juce::Justification::centredLeft);

      // live output meter on the right edge
      const float output = processor.getLfoOutput(lfo);
      const auto meter = bounds.withLeft(bounds.getRight() - 6.f).reduced(1.f, 4.f);
      g.setColour(juce::Colours::black.withAlpha(0.6f));
      g.fillRect(meter);
      g.setColour(colour);
      g.fillRect(meter.withTop(meter.getBottom() - output * meter.getHeight()));
   }

   void LfoTab::mouseDown(const juce::MouseEvent&)
   {
      dragStarted = false;
      if (onSelect)
         onSelect(lfo);
   }

   void LfoTab::mouseDrag(const juce::MouseEvent& e)
   {
      if (dragStarted || e.getDistanceFromDragStart() < 5)
         return;

      if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
      {
         dragStarted = true;

         // little pill that follows the mouse
         juce::Image image(juce::Image::ARGB, 64, 22, true);
         {
            juce::Graphics g(image);
            g.setColour(Theme::lfoColour(lfo));
            g.fillRoundedRectangle(0.f, 0.f, 64.f, 22.f, 11.f);
            g.setColour(juce::Colours::black);
            g.setFont(Theme::titleFont(11.f));
            g.drawText("LFO " + juce::String(lfo + 1), 0, 0, 64, 22, juce::Justification::centred);
         }
         container->startDragging("lfo:" + juce::String(lfo), this, juce::ScaledImage(image), true);
      }
   }

   //==============================================================================
   DestinationList::Row::Row(VoronoiseAudioProcessor& p, int lfoIndex, const juce::String& id, float depthValue) : paramID(id)
   {
      auto* param = p.apvts.getParameter(paramID);
      name.setText(param != nullptr ? param->getName(24) : paramID, juce::dontSendNotification);
      name.setFont(Theme::labelFont(11.f));
      name.setColour(juce::Label::textColourId, Theme::label);
      name.setMinimumHorizontalScale(0.7f);
      addAndMakeVisible(name);

      depth.setRange(-1.0, 1.0, 0.001);
      depth.setValue(depthValue, juce::dontSendNotification);
      depth.setDoubleClickReturnValue(true, 0.5);
      depth.getProperties().set("bipolar", true);
      depth.setColour(juce::Slider::trackColourId, Theme::lfoColour(lfoIndex));
      depth.setTooltip("Depth");
      depth.onValueChange = [&p, lfoIndex, this] { p.setModulationDepth(lfoIndex, paramID, static_cast<float>(depth.getValue())); };
      addAndMakeVisible(depth);

      remove.setTooltip("Remove");
      remove.onClick = [&p, lfoIndex, this]
      {
         // removing rebuilds the list (and deletes this row), so do it after the click finishes
         juce::MessageManager::callAsync([&p, lfoIndex, target = paramID] { p.removeModulation(lfoIndex, target); });
      };
      addAndMakeVisible(remove);
   }

   void DestinationList::Row::resized()
   {
      auto area = getLocalBounds().reduced(2, 1);
      remove.setBounds(area.removeFromRight(20).reduced(0, 2));
      auto top = area.removeFromTop(area.getHeight() / 2);
      name.setBounds(top);
      depth.setBounds(area);
   }

   void DestinationList::Row::paint(juce::Graphics& g)
   {
      g.setColour(juce::Colours::white.withAlpha(0.04f));
      g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.f), 3.f);
   }

   DestinationList::DestinationList(VoronoiseAudioProcessor& p) : processor(p)
   {
      viewport.setViewedComponent(&content, false);
      viewport.setScrollBarsShown(true, false);
      viewport.setScrollBarThickness(6);
      addAndMakeVisible(viewport);
      rebuild();
      startTimerHz(10);
   }

   void DestinationList::setLfo(int newLfo)
   {
      lfo = newLfo;
      rebuild();
   }

   void DestinationList::timerCallback()
   {
      if (processor.getModulationVersion() == seenVersion)
         return;

      // depth changes come through here too, only rebuild if the set of targets changed
      // (rebuilding would kill a slider mid drag)
      std::vector<VoronoiseAudioProcessor::Modulation> mine;
      for (const auto& mod : processor.getModulations())
         if (mod.lfo == lfo)
            mine.push_back(mod);

      bool sameTargets = static_cast<int>(mine.size()) == rows.size();
      for (size_t i = 0; sameTargets && i < mine.size(); i++)
         sameTargets = rows[static_cast<int>(i)]->paramID == mine[i].paramID;

      if (! sameTargets)
      {
         rebuild();
         return;
      }

      seenVersion = processor.getModulationVersion();
      for (size_t i = 0; i < mine.size(); i++)
      {
         auto& slider = rows[static_cast<int>(i)]->depth;
         if (! slider.isMouseButtonDown())
            slider.setValue(mine[i].depth, juce::dontSendNotification);
      }
   }

   void DestinationList::rebuild()
   {
      seenVersion = processor.getModulationVersion();
      rows.clear();
      for (const auto& mod : processor.getModulations())
      {
         if (mod.lfo != lfo)
            continue;
         content.addAndMakeVisible(rows.add(new Row(processor, lfo, mod.paramID, mod.depth)));
      }
      resized();
      repaint();
   }

   void DestinationList::paint(juce::Graphics& g)
   {
      g.setColour(juce::Colour(0xff0b0d10));
      g.fillRoundedRectangle(getLocalBounds().toFloat(), 5.f);
      g.setColour(juce::Colours::black);
      g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 5.f, 1.f);

      if (rows.isEmpty())
      {
         g.setFont(Theme::labelFont(11.f));
         g.setColour(Theme::labelDim);
         g.drawFittedText("DRAG THE LFO " + juce::String(lfo + 1) + " TAB\nONTO ANY KNOB\nTO MODULATE IT",
                          getLocalBounds().reduced(10), juce::Justification::centred, 4);
      }
   }

   void DestinationList::resized()
   {
      viewport.setBounds(getLocalBounds().reduced(4));
      constexpr int ROW_HEIGHT = 36;
      content.setSize(viewport.getWidth() - 8, rows.size() * ROW_HEIGHT);
      for (int i = 0; i < rows.size(); i++)
         rows[i]->setBounds(0, i * ROW_HEIGHT, content.getWidth(), ROW_HEIGHT);
   }

   //==============================================================================
   LfoPanel::LfoPanel(VoronoiseAudioProcessor& p) : processor(p), editor(p), destinations(p)
   {
      for (int i = 0; i < Params::NUM_LFOS; i++)
      {
         auto& tab = tabs[static_cast<size_t>(i)];
         tab = std::make_unique<LfoTab>(processor, i);
         tab->onSelect = [this](int lfo) { selectLfo(lfo); };
         addAndMakeVisible(*tab);
      }

      addAndMakeVisible(editor);
      addAndMakeVisible(destinations);

      // presets just load a shape, so they're a plain menu rather than a parameter
      presetBox.setTextWhenNothingSelected("SHAPE...");
      presetBox.addItemList(LfoShape::getPresetNames(), 1);
      presetBox.onChange = [this]
      {
         const int id = presetBox.getSelectedId();
         if (id <= 0)
            return;
         LfoShape shape;
         shape.setPreset(static_cast<LfoShape::Preset>(id - 1), &random);
         processor.setLfoShape(selectedLfo, shape);
         editor.setLfo(selectedLfo);
         presetBox.setSelectedId(0, juce::dontSendNotification);
      };
      addAndMakeVisible(presetBox);

      gridBox.addItem("GRID OFF", 1);
      gridBox.addItem("GRID 4", 2);
      gridBox.addItem("GRID 8", 3);
      gridBox.addItem("GRID 16", 4);
      gridBox.addItem("GRID 32", 5);
      gridBox.setSelectedId(3, juce::dontSendNotification);
      gridBox.onChange = [this]
      {
         constexpr int DIVISIONS[] = { 0, 4, 8, 16, 32 };
         editor.setGridDivisions(DIVISIONS[juce::jlimit(0, 4, gridBox.getSelectedId() - 1)]);
      };
      addAndMakeVisible(gridBox);

      selectLfo(0);
      startTimerHz(10);
   }

   void LfoPanel::buildControls()
   {
      using Style = ParamControl::Style;
      rate = std::make_unique<ParamControl>(processor, Params::lfoRate(selectedLfo), "Rate");
      sync = std::make_unique<ParamControl>(processor, Params::lfoSync(selectedLfo), "Sync");
      division = std::make_unique<ParamControl>(processor, Params::lfoDivision(selectedLfo), "Division", Style::Menu);
      mode = std::make_unique<ParamControl>(processor, Params::lfoMode(selectedLfo), "Mode", Style::Menu);
      phase = std::make_unique<ParamControl>(processor, Params::lfoPhase(selectedLfo), "Phase");
      smooth = std::make_unique<ParamControl>(processor, Params::lfoSmooth(selectedLfo), "Smooth");

      for (auto* control : { rate.get(), sync.get(), division.get(), mode.get(), phase.get(), smooth.get() })
         addAndMakeVisible(control);

      resized();
   }

   void LfoPanel::selectLfo(int lfo)
   {
      selectedLfo = juce::jlimit(0, Params::NUM_LFOS - 1, lfo);
      for (int i = 0; i < Params::NUM_LFOS; i++)
         tabs[static_cast<size_t>(i)]->setSelected(i == selectedLfo);

      editor.setLfo(selectedLfo);
      destinations.setLfo(selectedLfo);
      buildControls();
      repaint();
   }

   void LfoPanel::timerCallback()
   {
      // rate or division, whichever one the sync switch isn't using gets dimmed
      const bool synced = processor.getBaseValue(Params::lfoSync(selectedLfo)) > 0.5f;
      rate->setAlpha(synced ? 0.35f : 1.f);
      division->setAlpha(synced ? 1.f : 0.35f);
   }

   void LfoPanel::paint(juce::Graphics& g)
   {
      Theme::drawSection(g, getLocalBounds().toFloat(), {});

      g.setFont(Theme::titleFont());
      g.setColour(Theme::label);
      g.drawText("MODULATION", getLocalBounds().reduced(12, 0).removeFromTop(30), juce::Justification::centredLeft);

      g.setFont(Theme::labelFont(10.f));
      g.setColour(Theme::labelDim);
      g.drawText("DESTINATIONS", destinations.getBounds().translated(0, -16).withHeight(14), juce::Justification::centredLeft);
   }

   void LfoPanel::resized()
   {
      auto area = getLocalBounds().reduced(12, 0);

      auto header = area.removeFromTop(30).reduced(0, 4);
      header.removeFromLeft(100);
      for (auto& tab : tabs)
      {
         tab->setBounds(header.removeFromLeft(84));
         header.removeFromLeft(6);
      }
      gridBox.setBounds(header.removeFromRight(86).reduced(0, 1));
      header.removeFromRight(6);
      presetBox.setBounds(header.removeFromRight(100).reduced(0, 1));

      area.removeFromBottom(8);
      auto right = area.removeFromRight(196);
      right.removeFromTop(18);
      destinations.setBounds(right);
      area.removeFromRight(12);

      auto knobs = area.removeFromBottom(80);
      editor.setBounds(area.withTrimmedBottom(6));

      if (rate == nullptr)
         return;

      const int cell = knobs.getWidth() / 6;
      for (auto* control : { rate.get(), sync.get(), division.get(), mode.get(), phase.get(), smooth.get() })
      {
         auto bounds = knobs.removeFromLeft(cell);
         if (control == sync.get())
            bounds = bounds.withSizeKeepingCentre(bounds.getWidth() - 8, 34).translated(0, -6);
         else if (control != rate.get() && control != phase.get() && control != smooth.get())
            bounds = bounds.withSizeKeepingCentre(bounds.getWidth(), 48).translated(0, -6);
         control->setBounds(bounds);
      }
   }
}
