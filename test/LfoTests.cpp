#include <gtest/gtest.h>
#include <memory>

#include "synth/Lfo.h"
#include "Voronoise/Params.h"
#include "Voronoise/PluginProcessor.h"
#include "Voronoise/PluginEditor.h"

namespace
{
   constexpr double SAMPLE_RATE = 48000.0;
   constexpr int BLOCK = 480; // 10ms

   void setParam(VoronoiseAudioProcessor &processor, const juce::String &id, float realValue)
   {
      auto *param = processor.apvts.getParameter(id);
      ASSERT_NE(param, nullptr) << id;
      param->setValueNotifyingHost(param->convertTo0to1(realValue));
   }

   void run(VoronoiseAudioProcessor &processor, double seconds, bool noteAtStart = false)
   {
      juce::AudioBuffer<float> buffer(2, BLOCK);
      const int blocks = static_cast<int>(seconds * SAMPLE_RATE / BLOCK);
      for (int i = 0; i < blocks; i++)
      {
         juce::MidiBuffer midi;
         if (noteAtStart && i == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
         processor.processBlock(buffer, midi);
      }
   }

   // an lfo that sits at a fixed height makes the maths easy to check
   void makeFlat(VoronoiseAudioProcessor &processor, int lfo, float height)
   {
      LfoShape flat;
      flat.setPoints({ { 0.f, height, 0.f }, { 1.f, height, 0.f } });
      processor.setLfoShape(lfo, flat);
   }

   struct Fixture
   {
      juce::ScopedJuceInitialiser_GUI juceInit;
      VoronoiseAudioProcessor processor;
      Fixture() { processor.prepareToPlay(SAMPLE_RATE, BLOCK); }
   };
}

//==============================================================================
TEST(LfoShapeTest, PresetsHitTheirPeaks)
{
   LfoShape shape;
   shape.setPreset(LfoShape::Preset::Sine);
   EXPECT_NEAR(shape.evaluate(0.f), 0.5f, 1e-4f);
   EXPECT_NEAR(shape.evaluate(0.25f), 1.f, 1e-4f);
   EXPECT_NEAR(shape.evaluate(0.75f), 0.f, 1e-4f);
   // close to a real sine in between
   for (float x = 0.f; x <= 1.f; x += 0.05f)
      EXPECT_NEAR(shape.evaluate(x), 0.5f + 0.5f * std::sin(juce::MathConstants<float>::twoPi * x), 0.06f) << x;

   shape.setPreset(LfoShape::Preset::Square);
   EXPECT_FLOAT_EQ(shape.evaluate(0.25f), 1.f);
   EXPECT_FLOAT_EQ(shape.evaluate(0.75f), 0.f);

   shape.setPreset(LfoShape::Preset::SawUp);
   EXPECT_NEAR(shape.evaluate(0.3f), 0.3f, 1e-4f);

   juce::Random random(5);
   shape.setPreset(LfoShape::Preset::Steps, &random);
   // flat inside each step
   EXPECT_FLOAT_EQ(shape.evaluate(0.01f), shape.evaluate(0.12f));
}

TEST(LfoShapeTest, EditingKeepsPointsInOrder)
{
   LfoShape shape;
   shape.setPreset(LfoShape::Preset::Triangle); // 0, 0.5, 1

   const int added = shape.addPoint(0.25f, 0.9f);
   EXPECT_EQ(added, 1);
   EXPECT_EQ(shape.getPoints().size(), 4u);
   EXPECT_NEAR(shape.evaluate(0.25f), 0.9f, 1e-4f);

   // can't drag past a neighbour
   shape.movePoint(added, 0.8f, 0.2f);
   EXPECT_FLOAT_EQ(shape.getPoints()[1].x, 0.5f);

   // ends only move up and down and can't be removed
   shape.movePoint(0, 0.3f, 0.7f);
   EXPECT_FLOAT_EQ(shape.getPoints()[0].x, 0.f);
   EXPECT_FLOAT_EQ(shape.getPoints()[0].y, 0.7f);
   shape.removePoint(0);
   shape.removePoint(static_cast<int>(shape.getPoints().size()) - 1);
   EXPECT_EQ(shape.getPoints().size(), 4u);

   shape.removePoint(1);
   EXPECT_EQ(shape.getPoints().size(), 3u);
}

TEST(LfoShapeTest, CurveBendsTheSegment)
{
   LfoShape shape;
   shape.setPreset(LfoShape::Preset::SawUp);
   shape.setCurve(0, 1.f);
   EXPECT_LT(shape.evaluate(0.5f), 0.1f); // slow start
   shape.setCurve(0, -1.f);
   EXPECT_GT(shape.evaluate(0.5f), 0.9f); // fast start
   EXPECT_NEAR(shape.evaluate(1.f), 1.f, 1e-4f);
}

TEST(LfoShapeTest, SavesAndLoads)
{
   LfoShape shape;
   shape.setPreset(LfoShape::Preset::Square);
   shape.addPoint(0.7f, 0.4f);
   shape.setCurve(0, 0.3f);

   LfoShape loaded;
   loaded.fromValueTree(shape.toValueTree());
   EXPECT_TRUE(loaded == shape);

   LfoTable table;
   table.fill(shape);
   for (float x = 0.f; x <= 1.f; x += 0.1f)
      if (std::abs(x - 0.5f) > 0.01f) // the jump itself gets smeared over one table step
         EXPECT_NEAR(table.lookup(x), shape.evaluate(x), 0.01f) << x;
}

//==============================================================================
TEST(ModulationTest, LfoPushesTheParameter)
{
   Fixture f;
   makeFlat(f.processor, 0, 1.f); // lfo stuck at the top
   setParam(f.processor, Params::GRAVITY, 900.f);

   f.processor.addModulation(0, Params::GRAVITY, 0.5f);
   run(f.processor, 0.05);

   // half the 0 - 3000 range on top of the knob
   EXPECT_NEAR(f.processor.getParamValue(Params::GRAVITY), 900.f + 1500.f, 5.f);
   EXPECT_NEAR(f.processor.getBaseValue(Params::GRAVITY), 900.f, 1.f); // the knob itself doesn't move

   // negative depth pulls it down, and it can't go past the end of the range
   f.processor.setModulationDepth(0, Params::GRAVITY, -1.f);
   run(f.processor, 0.05);
   EXPECT_NEAR(f.processor.getParamValue(Params::GRAVITY), 0.f, 1.f);

   f.processor.removeModulation(0, Params::GRAVITY);
   EXPECT_NEAR(f.processor.getParamValue(Params::GRAVITY), 900.f, 1.f);
   EXPECT_TRUE(f.processor.getModulations().empty());
}

TEST(ModulationTest, SeveralLfosAddUpAndChoicesRound)
{
   Fixture f;
   makeFlat(f.processor, 0, 1.f);
   makeFlat(f.processor, 1, 1.f);

   // lines: both = 0, voronoi only = 1, delaunay only = 2
   f.processor.addModulation(0, Params::LINES, 0.3f);
   f.processor.addModulation(1, Params::LINES, 0.4f);
   run(f.processor, 0.05);
   EXPECT_EQ(f.processor.getParamValue(Params::LINES), 1.f); // 0.7 of the way -> rounds to 1.4 -> 1

   f.processor.setModulationDepth(1, Params::LINES, 0.7f);
   run(f.processor, 0.05);
   EXPECT_EQ(f.processor.getParamValue(Params::LINES), 2.f);

   // dropping the same lfo twice doesn't double it, bad ids are ignored
   f.processor.addModulation(0, Params::LINES, 0.9f);
   f.processor.addModulation(0, "notAParam", 0.9f);
   f.processor.addModulation(7, Params::LINES, 0.9f);
   EXPECT_EQ(f.processor.getModulations().size(), 2u);
   EXPECT_EQ(f.processor.getModulationsFor(Params::LINES).size(), 2u);
}

TEST(ModulationTest, RateSyncAndModes)
{
   Fixture f;
   f.processor.setLfoShape(0, [] { LfoShape s; s.setPreset(LfoShape::Preset::SawUp); return s; }());

   // free at 1hz: half a second is half way through
   setParam(f.processor, Params::lfoRate(0), 1.f);
   run(f.processor, 0.5);
   EXPECT_NEAR(f.processor.getLfoPhase(0), 0.5f, 0.02f);
   EXPECT_NEAR(f.processor.getLfoOutput(0), 0.5f, 0.02f);

   // synced to quarter notes at the default 120bpm = 2hz, so 0.25s later it's back where it was
   setParam(f.processor, Params::lfoSync(0), 1.f);
   setParam(f.processor, Params::lfoDivision(0), 4.f); // 1/4
   const float before = f.processor.getLfoPhase(0);
   run(f.processor, 0.25);
   EXPECT_NEAR(std::fmod(f.processor.getLfoPhase(0) - before + 1.f, 1.f), 0.5f, 0.03f);

   // retrigger: a note puts it back at the start
   setParam(f.processor, Params::lfoSync(0), 0.f);
   setParam(f.processor, Params::lfoMode(0), static_cast<float>(Params::LfoMode::Retrigger));
   run(f.processor, 0.3);
   run(f.processor, 0.1, true);
   EXPECT_NEAR(f.processor.getLfoPhase(0), 0.1f, 0.02f);

   // envelope: runs once and sits at the end
   setParam(f.processor, Params::lfoMode(0), static_cast<float>(Params::LfoMode::Envelope));
   run(f.processor, 0.1, true);
   run(f.processor, 2.0);
   EXPECT_FLOAT_EQ(f.processor.getLfoPhase(0), 1.f);
   EXPECT_NEAR(f.processor.getLfoOutput(0), 1.f, 0.01f);
}

TEST(ModulationTest, SmoothingSlowsJumps)
{
   Fixture f;
   f.processor.setLfoShape(0, [] { LfoShape s; s.setPreset(LfoShape::Preset::Square); return s; }());
   setParam(f.processor, Params::lfoRate(0), 0.5f); // 1s high, 1s low
   setParam(f.processor, Params::lfoSmooth(0), 1.f);

   // half a second into the high part it's still catching up from 0
   run(f.processor, 0.05);
   EXPECT_LT(f.processor.getLfoOutput(0), 0.5f);
   run(f.processor, 0.9);
   EXPECT_GT(f.processor.getLfoOutput(0), 0.95f);
}

TEST(ModulationTest, LfoChangesBallPhysics)
{
   Fixture f;
   auto editor = std::unique_ptr<juce::AudioProcessorEditor>(f.processor.createEditor());
   auto &canvas = dynamic_cast<VoronoiseAudioProcessorEditor &>(*editor).getCanvas();

   // lfo at the top with full negative depth turns gravity off
   makeFlat(f.processor, 2, 1.f);
   f.processor.addModulation(2, Params::GRAVITY, -1.f);
   setParam(f.processor, Params::LAUNCH_SPEED, 0.f);
   run(f.processor, 0.05);

   canvas.dropBall({ 100.f, 50.f });
   for (int i = 0; i < 30; i++)
      canvas.updateBalls(1.f / 60.f);
   EXPECT_FLOAT_EQ(canvas.getBalls().front().getY(), 50.f);
}

TEST(ModulationTest, SavedWithTheProject)
{
   juce::MemoryBlock saved;
   {
      Fixture f;
      f.processor.addModulation(1, Params::PULSE_WIDTH, -0.3f);
      LfoShape shape;
      shape.setPreset(LfoShape::Preset::Square);
      f.processor.setLfoShape(2, shape);
      f.processor.getStateInformation(saved);
   }

   Fixture f;
   f.processor.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
   const auto mods = f.processor.getModulations();
   ASSERT_EQ(mods.size(), 1u);
   EXPECT_EQ(mods[0].lfo, 1);
   EXPECT_EQ(mods[0].paramID, Params::PULSE_WIDTH);
   EXPECT_NEAR(mods[0].depth, -0.3f, 1e-4f);

   LfoShape square;
   square.setPreset(LfoShape::Preset::Square);
   EXPECT_TRUE(f.processor.getLfoShape(2) == square);
}

//==============================================================================
TEST(LfoUiTest, DroppingATabOnAKnobModulatesIt)
{
   Fixture f;
   auto editor = std::unique_ptr<juce::AudioProcessorEditor>(f.processor.createEditor());

   UI::ParamControl knob(f.processor, Params::BOUNCINESS);
   knob.setSize(80, 90);
   const juce::DragAndDropTarget::SourceDetails details("lfo:2", nullptr, { 40, 40 });
   ASSERT_TRUE(knob.isInterestedInDragSource(details));
   EXPECT_FALSE(knob.isInterestedInDragSource({ "something else", nullptr, { 40, 40 } }));

   knob.itemDropped(details);
   const auto mods = f.processor.getModulationsFor(Params::BOUNCINESS);
   ASSERT_EQ(mods.size(), 1u);
   EXPECT_EQ(mods[0].lfo, 2);

   // it paints its ring without trouble
   juce::Image image(juce::Image::ARGB, 80, 90, true);
   juce::Graphics g(image);
   knob.paintEntireComponent(g, false);

   // the lfo's destination list shows it
   auto &panel = dynamic_cast<VoronoiseAudioProcessorEditor &>(*editor).getMainPanel().getLfoPanel();
   panel.selectLfo(2);
   EXPECT_EQ(panel.getDestinations().getNumRows(), 1);
   panel.selectLfo(0);
   EXPECT_EQ(panel.getDestinations().getNumRows(), 0);
}

TEST(LfoUiTest, ShapeEditorCoordinatesRoundTrip)
{
   Fixture f;
   UI::LfoShapeEditor shapeEditor(f.processor);
   shapeEditor.setSize(400, 160);

   for (auto [x, y] : { std::pair{ 0.f, 0.f }, { 0.5f, 0.5f }, { 1.f, 1.f }, { 0.3f, 0.8f } })
   {
      const auto back = shapeEditor.fromScreen(shapeEditor.toScreen(x, y));
      EXPECT_NEAR(back.x, x, 1e-4f);
      EXPECT_NEAR(back.y, y, 1e-4f);
   }

   // the sine's first point is grabbable where it's drawn
   const auto &first = shapeEditor.getShape().getPoints().front();
   EXPECT_EQ(shapeEditor.findPoint(shapeEditor.toScreen(first.x, first.y)), 0);
   EXPECT_EQ(shapeEditor.findCurveHandle(shapeEditor.getCurveHandle(0)), 0);
}

//==============================================================================
TEST(KeyboardTest, ClickedKeysPlayAndDropBalls)
{
   Fixture f;
   auto editor = std::unique_ptr<juce::AudioProcessorEditor>(f.processor.createEditor());
   auto &ed = dynamic_cast<VoronoiseAudioProcessorEditor &>(*editor);

   // the keyboard spans the panel
   auto &keyboard = ed.getMainPanel().getKeyboard();
   EXPECT_GT(keyboard.getWidth(), 600);

   f.processor.getKeyboardState().noteOn(1, 60, 0.8f);
   juce::AudioBuffer<float> buffer(2, BLOCK);
   juce::MidiBuffer midi;
   f.processor.processBlock(buffer, midi);

   // the note reached the synth (sound) and was queued for the canvas (ball)
   EXPECT_GT(buffer.getMagnitude(0, BLOCK), 0.01f);
   VoronoiseAudioProcessor::NoteEvent event;
   ASSERT_TRUE(f.processor.noteEventFifo.pull(event));
   EXPECT_EQ(event.noteNumber, 60);

   // letting go fades out over the release (300ms by default) instead of cutting off.
   // hosts give a fresh midi buffer every block, so do the same here
   f.processor.getKeyboardState().noteOff(1, 60, 0.f);
   midi.clear();
   f.processor.processBlock(buffer, midi);
   EXPECT_GT(buffer.getMagnitude(0, BLOCK), 0.01f);
   for (int i = 0; i < 40; i++)
   {
      midi.clear();
      f.processor.processBlock(buffer, midi);
   }
   EXPECT_LT(buffer.getMagnitude(0, BLOCK), 0.001f);
}

TEST(SwitchTest, ClickedChoiceLightsStraightAway)
{
   Fixture f;
   UI::ParamControl waves(f.processor, Params::BALL_SOUND, "Ball sound", UI::ParamControl::Style::Switch);
   auto &buttons = waves.getSwitchButtons();
   ASSERT_EQ(buttons.size(), 5);

   // each click lights that button (and only that one) right away
   for (int clicked : { 2, 0, 4, 1 })
   {
      buttons[clicked]->onClick();
      for (int i = 0; i < buttons.size(); i++)
         EXPECT_EQ(buttons[i]->getToggleState(), i == clicked) << "clicked " << clicked << ", button " << i;
      EXPECT_EQ(static_cast<int>(f.processor.getBaseValue(Params::BALL_SOUND)), clicked);
   }

   // and it follows changes made elsewhere (automation, the canvas)
   auto *param = f.processor.apvts.getParameter(Params::BALL_SOUND);
   param->setValueNotifyingHost(param->convertTo0to1(3.f));
   EXPECT_TRUE(buttons[3]->getToggleState());
   EXPECT_FALSE(buttons[1]->getToggleState());
}

//==============================================================================
namespace
{
   // plays a note for a while then lets go, returns the whole output (left channel)
   std::vector<float> playNote(VoronoiseAudioProcessor &processor, double holdSeconds, double releaseSeconds, juce::uint8 velocity = 127)
   {
      std::vector<float> out;
      juce::AudioBuffer<float> buffer(2, BLOCK);
      const int holdBlocks = static_cast<int>(holdSeconds * SAMPLE_RATE / BLOCK);
      const int releaseBlocks = static_cast<int>(releaseSeconds * SAMPLE_RATE / BLOCK);
      for (int i = 0; i < holdBlocks + releaseBlocks; i++)
      {
         juce::MidiBuffer midi;
         if (i == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 69, velocity), 0);
         if (i == holdBlocks)
            midi.addEvent(juce::MidiMessage::noteOff(1, 69), 0);
         processor.processBlock(buffer, midi);
         out.insert(out.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + BLOCK);
      }
      return out;
   }

   float peakBetween(const std::vector<float> &out, double from, double to)
   {
      float peak = 0.f;
      for (auto i = static_cast<size_t>(from * SAMPLE_RATE); i < static_cast<size_t>(to * SAMPLE_RATE) && i < out.size(); i++)
         peak = juce::jmax(peak, std::abs(out[i]));
      return peak;
   }
}

TEST(EnvelopeTest, AttackSustainAndReleaseShapeTheNote)
{
   Fixture f;
   setParam(f.processor, Params::AMP_ATTACK, 200.f);
   setParam(f.processor, Params::AMP_DECAY, 100.f);
   setParam(f.processor, Params::AMP_SUSTAIN, 0.5f);
   setParam(f.processor, Params::AMP_RELEASE, 400.f);
   setParam(f.processor, Params::KEYS_LEVEL, 0.f); // unity, so the peak is 1

   const auto out = playNote(f.processor, 1.0, 1.0);

   // fades in over the attack
   EXPECT_LT(peakBetween(out, 0.0, 0.05), 0.3f);
   EXPECT_NEAR(peakBetween(out, 0.18, 0.22), 1.f, 0.05f);
   // settles at the sustain level
   EXPECT_NEAR(peakBetween(out, 0.6, 0.9), 0.5f, 0.03f);
   // still going a little after letting go, silent once the release is over
   EXPECT_GT(peakBetween(out, 1.05, 1.1), 0.2f);
   EXPECT_LT(peakBetween(out, 1.45, 2.0), 0.001f);
}

TEST(EnvelopeTest, NoClicksAtTheStartOrEnd)
{
   Fixture f;
   setParam(f.processor, Params::KEYS_LEVEL, 0.f);
   const auto out = playNote(f.processor, 0.3, 0.6);

   // a 440hz sine at full level moves at most ~0.06 per sample, a click would jump far more
   float biggestStep = 0.f;
   for (size_t i = 1; i < out.size(); i++)
      biggestStep = juce::jmax(biggestStep, std::abs(out[i] - out[i - 1]));
   EXPECT_LT(biggestStep, 0.07f);
   EXPECT_EQ(out.front(), 0.f);
   EXPECT_LT(std::abs(out.back()), 1e-4f);
}

TEST(EnvelopeTest, VelocityAndLevelScaleIt)
{
   Fixture loud, soft, quiet;
   setParam(quiet.processor, Params::KEYS_LEVEL, -12.f);
   for (auto *fixture : { &loud, &soft, &quiet })
      setParam(fixture->processor, Params::AMP_SUSTAIN, 1.f);
   setParam(loud.processor, Params::KEYS_LEVEL, 0.f);
   setParam(soft.processor, Params::KEYS_LEVEL, 0.f);

   const float full = peakBetween(playNote(loud.processor, 0.5, 0.1), 0.2, 0.4);
   const float half = peakBetween(playNote(soft.processor, 0.5, 0.1, 64), 0.2, 0.4);
   const float down = peakBetween(playNote(quiet.processor, 0.5, 0.1), 0.2, 0.4);

   EXPECT_NEAR(half / full, 64.f / 127.f, 0.02f);
   EXPECT_NEAR(down / full, juce::Decibels::decibelsToGain(-12.f), 0.02f);
}

TEST(EnvelopeTest, DraggingPointsSetsTheKnobs)
{
   Fixture f;
   using Handle = UI::EnvelopeDisplay::Handle;
   UI::EnvelopeDisplay display(f.processor);
   display.setSize(180, 90);

   // every point can be grabbed where it's drawn
   for (auto handle : { Handle::Peak, Handle::Decay, Handle::NoteOff, Handle::End })
      EXPECT_EQ(display.findHandle(display.getHandlePosition(handle)), handle);
   EXPECT_EQ(display.findHandle({ -50.f, -50.f }), Handle::None);

   const float doubling = UI::EnvelopeDisplay::PIXELS_PER_DOUBLING;

   // peak right one doubling (5ms -> 10ms), way left takes it to 0
   display.beginDrag(Handle::Peak);
   display.dragBy(doubling, 0.f);
   display.endDrag();
   EXPECT_NEAR(f.processor.getBaseValue(Params::AMP_ATTACK), 10.f, 0.2f);
   display.beginDrag(Handle::Peak);
   display.dragBy(-500.f, 0.f);
   display.endDrag();
   EXPECT_NEAR(f.processor.getBaseValue(Params::AMP_ATTACK), 0.f, 0.2f);

   // decay corner: sideways is decay time, down is less sustain
   display.beginDrag(Handle::Decay);
   display.dragBy(-doubling, 37.f); // half the 74px graph height
   display.endDrag();
   EXPECT_NEAR(f.processor.getBaseValue(Params::AMP_DECAY), 100.f, 1.f);
   EXPECT_NEAR(f.processor.getBaseValue(Params::AMP_SUSTAIN), 0.3f, 0.02f);

   // note off only moves sustain, and can't go past the top
   display.beginDrag(Handle::NoteOff);
   display.dragBy(80.f, -500.f);
   display.endDrag();
   EXPECT_FLOAT_EQ(f.processor.getBaseValue(Params::AMP_SUSTAIN), 1.f);
   EXPECT_NEAR(f.processor.getBaseValue(Params::AMP_DECAY), 100.f, 1.f);

   // end point is release, double click puts it back
   display.beginDrag(Handle::End);
   display.dragBy(2.f * doubling, 0.f);
   display.endDrag();
   EXPECT_NEAR(f.processor.getBaseValue(Params::AMP_RELEASE), 1200.f, 5.f);
   display.resetHandle(Handle::End);
   EXPECT_NEAR(f.processor.getBaseValue(Params::AMP_RELEASE), 300.f, 1.f);
}

TEST(EnvelopeTest, GraphDoesNotRescaleWhenReleaseGrows)
{
   Fixture f;
   using Handle = UI::EnvelopeDisplay::Handle;
   UI::EnvelopeDisplay display(f.processor);
   display.setSize(180, 90);

   const auto peak = display.getHandlePosition(Handle::Peak);
   const auto decay = display.getHandlePosition(Handle::Decay);
   const auto endBefore = display.getHandlePosition(Handle::End);

   // default envelope leaves empty room on the right
   EXPECT_LT(endBefore.x, 172.f - 20.f);

   // longer release: the end moves right, everything before it stays put
   setParam(f.processor, Params::AMP_RELEASE, 8000.f);
   display.refresh();
   EXPECT_FLOAT_EQ(display.getHandlePosition(Handle::Peak).x, peak.x);
   EXPECT_FLOAT_EQ(display.getHandlePosition(Handle::Decay).x, decay.x);
   EXPECT_GT(display.getHandlePosition(Handle::End).x, endBefore.x + 20.f);

   // and even maxed out it stays inside the graph
   setParam(f.processor, Params::AMP_ATTACK, 5000.f);
   setParam(f.processor, Params::AMP_DECAY, 5000.f);
   setParam(f.processor, Params::AMP_RELEASE, 10000.f);
   display.refresh();
   EXPECT_LE(display.getHandlePosition(Handle::End).x, 172.f + 0.01f);
}
