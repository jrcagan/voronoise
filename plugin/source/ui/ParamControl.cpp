#include "Voronoise/ui/ParamControl.h"
#include "Voronoise/ui/Theme.h"

namespace UI
{
   bool getDraggedLfo(const juce::var& description, int& lfo)
   {
      const auto text = description.toString();
      if (! text.startsWith("lfo:"))
         return false;

      lfo = text.fromFirstOccurrenceOf(":", false, false).getIntValue();
      return lfo >= 0 && lfo < Params::NUM_LFOS;
   }

   //==============================================================================
   void ModSlider::mouseDown(const juce::MouseEvent& e)
   {
      rightClicking = e.mods.isPopupMenu();
      if (rightClicking)
      {
         if (onRightClick)
            onRightClick();
         return;
      }
      juce::Slider::mouseDown(e);
   }

   void ModSlider::mouseDrag(const juce::MouseEvent& e)
   {
      if (! rightClicking)
         juce::Slider::mouseDrag(e);
   }

   void ModSlider::mouseUp(const juce::MouseEvent& e)
   {
      if (! rightClicking)
         juce::Slider::mouseUp(e);
      rightClicking = false;
   }

   //==============================================================================
   ParamControl::ParamControl(VoronoiseAudioProcessor& p, const juce::String& id, const juce::String& name, Style s)
       : processor(p), paramID(id), style(s)
   {
      param = processor.apvts.getParameter(paramID);
      jassert(param != nullptr); // typo in a param id

      displayName = name.isNotEmpty() ? name : param->getName(32);

      auto* choice = dynamic_cast<juce::AudioParameterChoice*>(param);
      if (style == Style::Auto)
      {
         if (dynamic_cast<juce::AudioParameterBool*>(param) != nullptr)
            style = Style::Toggle;
         else if (choice != nullptr)
            style = choice->choices.size() <= 3 ? Style::Switch : Style::Menu;
         else
            style = Style::Knob;
      }

      auto& apvts = processor.apvts;

      switch (style)
      {
         case Style::Knob:
         case Style::Fader:
         {
            slider = std::make_unique<ModSlider>(style == Style::Knob ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearVertical,
                                                 juce::Slider::NoTextBox);
            slider->setColour(juce::Slider::trackColourId, Theme::accent);
            slider->onRightClick = [this] { showMenu(); };
            slider->onDragStart = [this] { announceValue(); };
            slider->onValueChange = [this] { announceValue(); repaint(); };
            addAndMakeVisible(*slider);
            sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(apvts, paramID, *slider);
            slider->setDoubleClickReturnValue(true, param->convertFrom0to1(param->getDefaultValue()));
            slider->setMouseDragSensitivity(120); // pixels of drag for the whole range
            if (param->getNormalisableRange().start < 0.f && param->getNormalisableRange().end > 0.f)
               slider->getProperties().set("bipolar", true);
            break;
         }
         case Style::Toggle:
            toggle = std::make_unique<juce::TextButton>(displayName.toUpperCase());
            toggle->setClickingTogglesState(true);
            toggle->onClick = [this] { announceValue(); };
            addAndMakeVisible(*toggle);
            buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(apvts, paramID, *toggle);
            break;
         case Style::Menu:
            menu = std::make_unique<juce::ComboBox>();
            if (choice != nullptr)
               menu->addItemList(choice->choices, 1);
            menu->onChange = [this] { announceValue(); };
            addAndMakeVisible(*menu);
            menuAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(apvts, paramID, *menu);
            break;
         case Style::Switch:
         {
            const auto names = choice != nullptr ? choice->choices : juce::StringArray { "Off", "On" };
            for (int i = 0; i < names.size(); i++)
            {
               auto* button = switchButtons.add(new juce::TextButton(names[i].toUpperCase()));
               button->setClickingTogglesState(false);
               button->getProperties().set("switch", true);
               button->onClick = [this, i]
               {
                  if (switchAttachment != nullptr)
                     switchAttachment->setValueAsCompleteGesture(static_cast<float>(i));
                  announceValue();
               };
               addAndMakeVisible(button);
            }
            // use the value handed to us: this runs before the apvts has stored the new value,
            // so reading it back here would light the previous choice
            switchAttachment = std::make_unique<juce::ParameterAttachment>(*param, [this](float value) { updateSwitchButtons(juce::roundToInt(value)); });
            switchAttachment->sendInitialUpdate();
            break;
         }
         case Style::Auto:
         default:
            break;
      }

      setTooltip(displayName);
      refreshModulations();
      // hear about hovering over the child control too
      addMouseListener(this, true);
      startTimerHz(30);
   }

   ParamControl::~ParamControl()
   {
      removeMouseListener(this);
   }

   void ParamControl::updateSwitchButtons(int selected)
   {
      for (int i = 0; i < switchButtons.size(); i++)
      {
         // radio look without radio groups (they'd fight the attachment)
         switchButtons[i]->setToggleState(i == selected, juce::dontSendNotification);
      }
   }

   void ParamControl::announceValue()
   {
      if (auto* display = findParentComponentOfClass<ParamDisplay>())
         display->showParameter(displayName, param->getCurrentValueAsText());
   }

   void ParamControl::resized()
   {
      auto area = getLocalBounds();

      switch (style)
      {
         case Style::Knob:
         {
            area.removeFromBottom(LABEL_HEIGHT);
            // room around the knob for up to three mod rings
            const int size = juce::jmin(area.getWidth(), area.getHeight());
            slider->setBounds(area.withSizeKeepingCentre(size, size).reduced(9));
            break;
         }
         case Style::Fader:
            area.removeFromBottom(LABEL_HEIGHT);
            slider->setBounds(area.reduced(2, 4));
            break;
         case Style::Toggle:
            toggle->setBounds(area.withSizeKeepingCentre(area.getWidth(), juce::jmin(area.getHeight(), 34)).reduced(2));
            break;
         case Style::Menu:
         {
            area.removeFromBottom(LABEL_HEIGHT + 4);
            // sits just above its label like the knobs' do
            menu->setBounds(area.removeFromBottom(juce::jmin(area.getHeight(), 24)).reduced(2, 0));
            break;
         }
         case Style::Switch:
         {
            area.removeFromBottom(LABEL_HEIGHT);
            auto row = area.withSizeKeepingCentre(area.getWidth(), juce::jmin(area.getHeight(), 30));
            const int width = row.getWidth() / juce::jmax(1, switchButtons.size());
            for (auto* button : switchButtons)
               button->setBounds(row.removeFromLeft(width).reduced(1, 0));
            break;
         }
         case Style::Auto:
         default:
            break;
      }
   }

   void ParamControl::paint(juce::Graphics& g)
   {
      if (style == Style::Toggle)
         return;

      // name, or the value while it's being touched
      const bool showValue = hovering || (slider != nullptr && slider->isMouseButtonDown());
      g.setFont(Theme::labelFont(style == Style::Fader ? 10.f : 11.f));
      g.setColour(showValue ? Theme::accent : Theme::label);
      g.drawFittedText(showValue && style != Style::Fader ? param->getCurrentValueAsText() : displayName.toUpperCase(),
                       getLocalBounds().removeFromBottom(LABEL_HEIGHT), juce::Justification::centred, 1, 0.8f);
   }

   void ParamControl::paintOverChildren(juce::Graphics& g)
   {
      // highlight while an lfo is dragged over it
      if (dragHoverLfo >= 0)
      {
         g.setColour(Theme::lfoColour(dragHoverLfo).withAlpha(0.18f));
         g.fillRoundedRectangle(getLocalBounds().toFloat(), 6.f);
         g.setColour(Theme::lfoColour(dragHoverLfo));
         g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.f), 6.f, 1.5f);
      }

      if (modulations.empty())
         return;

      const float base = param->getValue();
      const float now = processor.getModulatedNormalised(paramID);

      if (style == Style::Knob)
      {
         const auto rotary = slider->getRotaryParameters();
         const auto bounds = slider->getBounds().toFloat();
         const auto centre = bounds.getCentre();
         const float innerRadius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 3.f;
         auto toAngle = [&](float v) { return rotary.startAngleRadians + v * (rotary.endAngleRadians - rotary.startAngleRadians); };

         for (size_t i = 0; i < modulations.size() && i < 3; i++)
         {
            const auto& mod = modulations[i];
            const float radius = innerRadius + 4.5f + 3.f * static_cast<float>(i);
            const float target = juce::jlimit(0.f, 1.f, base + mod.depth);

            // how far this lfo can push the knob
            juce::Path range;
            range.addCentredArc(centre.x, centre.y, radius, radius, 0.f, toAngle(juce::jmin(base, target)), toAngle(juce::jmax(base, target)), true);
            g.setColour(Theme::lfoColour(mod.lfo).withAlpha(0.85f));
            g.strokePath(range, juce::PathStrokeType(2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
         }

         // where it actually is right now
         const float angle = toAngle(now) - juce::MathConstants<float>::halfPi;
         const float dotRadius = innerRadius + 4.5f;
         g.setColour(juce::Colours::white);
         g.fillEllipse(juce::Rectangle<float>(5.f, 5.f).withCentre({ centre.x + dotRadius * std::cos(angle), centre.y + dotRadius * std::sin(angle) }));
      }
      else if (style == Style::Fader)
      {
         // tick on the side of the fader at the modulated value
         const auto bounds = slider->getBounds().toFloat();
         const float y = bounds.getBottom() - now * bounds.getHeight();
         g.setColour(Theme::lfoColour(modulations.front().lfo));
         g.fillRect(bounds.getRight() - 3.f, y - 1.f, 3.f, 2.f);
      }
      else
      {
         // little dots in the corner for switches, toggles and menus
         for (size_t i = 0; i < modulations.size(); i++)
         {
            g.setColour(Theme::lfoColour(modulations[i].lfo));
            g.fillEllipse(static_cast<float>(getWidth()) - 7.f - 7.f * static_cast<float>(i), 2.f, 5.f, 5.f);
         }
      }
   }

   void ParamControl::timerCallback()
   {
      if (processor.getModulationVersion() != seenModulationVersion)
         refreshModulations();

      // switches and toggles show what the lfo is doing to them by lighting the modulated choice
      if (! modulations.empty())
      {
         if (style == Style::Switch)
         {
            const int modulated = juce::roundToInt(processor.getParamValue(paramID));
            for (int i = 0; i < switchButtons.size(); i++)
               switchButtons[i]->setToggleState(i == modulated, juce::dontSendNotification);
         }
         repaint();
      }
   }

   void ParamControl::refreshModulations()
   {
      seenModulationVersion = processor.getModulationVersion();
      modulations = processor.getModulationsFor(paramID);
      if (modulations.empty() && style == Style::Switch)
         updateSwitchButtons(juce::roundToInt(processor.getBaseValue(paramID)));
      repaint();
   }

   void ParamControl::mouseEnter(const juce::MouseEvent&)
   {
      hovering = true;
      repaint();
   }

   void ParamControl::mouseExit(const juce::MouseEvent&)
   {
      hovering = isMouseOver(true);
      repaint();
   }

   void ParamControl::mouseDown(const juce::MouseEvent& e)
   {
      // right clicks on the label (the slider handles its own)
      if (e.mods.isPopupMenu() && e.eventComponent == this)
         showMenu();
   }

   void ParamControl::showMenu()
   {
      juce::PopupMenu menuItems;
      menuItems.addSectionHeader(displayName);
      menuItems.addItem(1, "Reset to default");

      refreshModulations();
      for (size_t i = 0; i < modulations.size(); i++)
         menuItems.addItem(100 + static_cast<int>(i), "Remove LFO " + juce::String(modulations[i].lfo + 1));

      if (modulations.empty())
         menuItems.addItem(2, "Drag an LFO tab here to modulate", false, false);

      menuItems.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                              [safeThis = juce::Component::SafePointer(this)](int result)
                              {
                                 if (safeThis == nullptr || result == 0)
                                    return;

                                 if (result == 1)
                                 {
                                    safeThis->param->beginChangeGesture();
                                    safeThis->param->setValueNotifyingHost(safeThis->param->getDefaultValue());
                                    safeThis->param->endChangeGesture();
                                 }
                                 else if (result >= 100)
                                 {
                                    const auto index = static_cast<size_t>(result - 100);
                                    if (index < safeThis->modulations.size())
                                       safeThis->processor.removeModulation(safeThis->modulations[index].lfo, safeThis->paramID);
                                 }
                              });
   }

   bool ParamControl::isInterestedInDragSource(const SourceDetails& details)
   {
      int lfo = 0;
      return getDraggedLfo(details.description, lfo);
   }

   void ParamControl::itemDragEnter(const SourceDetails& details)
   {
      int lfo = 0;
      if (getDraggedLfo(details.description, lfo))
         dragHoverLfo = lfo;
      repaint();
   }

   void ParamControl::itemDragExit(const SourceDetails&)
   {
      dragHoverLfo = -1;
      repaint();
   }

   void ParamControl::itemDropped(const SourceDetails& details)
   {
      dragHoverLfo = -1;
      int lfo = 0;
      if (getDraggedLfo(details.description, lfo))
      {
         // half the knob's range is a good starting depth, tweak it in the lfo's destination list
         processor.addModulation(lfo, paramID, 0.5f);
         refreshModulations();
         if (auto* display = findParentComponentOfClass<ParamDisplay>())
            display->showParameter("LFO " + juce::String(lfo + 1) + " >", displayName);
      }
      repaint();
   }
}
