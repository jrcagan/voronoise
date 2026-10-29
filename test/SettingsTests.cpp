#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <set>

#include "synth/Scale.h"
#include "synth/Sample.h"
#include "synth/HitSynth.h"
#include "primitives/Ball.h"
#include "Voronoise/Params.h"
#include "Voronoise/PluginProcessor.h"
#include "Voronoise/PluginEditor.h"
#include "TestHelpers.h"

namespace
{
   constexpr double SAMPLE_RATE = 48000.0;

   void setParam(VoronoiseAudioProcessor &processor, const juce::String &id, float realValue)
   {
      auto *param = processor.apvts.getParameter(id);
      ASSERT_NE(param, nullptr) << id;
      param->setValueNotifyingHost(param->convertTo0to1(realValue));
   }

   // silence with short loud clicks at the given positions
   SampleData::Ptr makeClicks(const std::vector<int> &positions, int length)
   {
      SampleData::Ptr data = new SampleData();
      data->sampleRate = SAMPLE_RATE;
      data->buffer.setSize(1, length);
      data->buffer.clear();
      for (int position : positions)
         for (int i = position; i < juce::jmin(length, position + 2000); i++)
            data->buffer.setSample(0, i, 0.8f * std::sin(static_cast<float>(i) * 0.3f) * std::exp(-static_cast<float>(i - position) / 400.f));
      data->analyse();
      return data;
   }

   // value[i] = i / length, so the playback direction is easy to see
   SampleData::Ptr makeRamp(int length)
   {
      SampleData::Ptr data = new SampleData();
      data->sampleRate = SAMPLE_RATE;
      data->buffer.setSize(1, length);
      for (int i = 0; i < length; i++)
         data->buffer.setSample(0, i, static_cast<float>(i) / static_cast<float>(length));
      data->analyse();
      return data;
   }

   juce::AudioBuffer<float> render(HitSynth &synth, int numSamples)
   {
      juce::AudioBuffer<float> buffer(1, numSamples);
      buffer.clear();
      synth.render(buffer, 0, numSamples);
      return buffer;
   }

   struct EditorFixture
   {
      juce::ScopedJuceInitialiser_GUI juceInit;
      VoronoiseAudioProcessor processor;
      std::unique_ptr<juce::AudioProcessorEditor> editor;
      VoronoiseAudioProcessorEditor *editorPtr = nullptr;
      UI::Canvas *e = nullptr;
      BallWorld *w = nullptr;

      EditorFixture()
      {
         TestHelpers::prepare(processor);
         editor.reset(processor.createEditor());
         editorPtr = dynamic_cast<VoronoiseAudioProcessorEditor *>(editor.get());
         e = &editorPtr->getCanvas();
         w = &processor.getWorld();
      }

      // the balls move (and new lines reach them) as the audio runs
      void run(double seconds) { TestHelpers::runAudio(processor, seconds); }

      void addSites()
      {
         auto sites = processor.getValueTree().getChildWithName("Sites");
         for (auto [x, y] : { std::pair{ 120.0, 90.0 }, { 600.0, 105.0 }, { 380.0, 285.0 }, { 100.0, 315.0 }, { 650.0, 322.0 } })
            sites.appendChild(juce::ValueTree("Site").setProperty("x", x, nullptr).setProperty("y", y, nullptr), nullptr);
      }

      // param changes reach the editor on its timer
      void tick() { e->syncWithProcessor(); }
   };
}

//==============================================================================
TEST(ScaleTest, HeightStaysInScaleAndRange)
{
   Scale::Settings s; // c minor pentatonic, c3 - b5
   const auto mask = Scale::getPitchClassMask(s.type, s.root);

   for (float up = 0.f; up <= 1.f; up += 0.01f)
   {
      const int note = Scale::chooseByHeight(up, s);
      EXPECT_GE(note, 48);
      EXPECT_LE(note, 83);
      EXPECT_TRUE(mask[static_cast<size_t>(note % 12)]) << note;
   }

   EXPECT_EQ(Scale::chooseByHeight(0.f, s), 48);
   EXPECT_LT(Scale::chooseByHeight(0.2f, s), Scale::chooseByHeight(0.8f, s));
}

TEST(ScaleTest, KeyChangeMovesTheScale)
{
   Scale::Settings s;
   s.root = 2; // d minor pentatonic: d f g a c
   s.type = Scale::Type::MinorPentatonic;
   const auto mask = Scale::getPitchClassMask(s.type, s.root);
   std::set<int> expected = { 2, 5, 7, 9, 0 };
   for (int pc = 0; pc < 12; pc++)
      EXPECT_EQ(mask[static_cast<size_t>(pc)], expected.count(pc) == 1) << pc;
}

TEST(ScaleTest, RandomFollowsProbabilities)
{
   Scale::Settings s;
   s.type = Scale::Type::Chromatic;
   s.lowestNote = 60;
   s.highestNote = 71;
   s.weights.fill(0.f);
   s.weights[0] = 3.f; // c three times as likely as g
   s.weights[7] = 1.f;

   juce::Random random(42);
   std::map<int, int> counts;
   for (int i = 0; i < 4000; i++)
      counts[Scale::chooseRandom(random, s)]++;

   EXPECT_EQ(counts.size(), 2u); // only c and g ever come up
   EXPECT_NEAR(static_cast<double>(counts[60]) / counts[67], 3.0, 0.4);
}

TEST(ScaleTest, ZeroProbabilityNotesAreSkippedByHeight)
{
   Scale::Settings s;
   s.weights[3] = 0.f; // no d# (in c minor pentatonic)
   for (float up = 0.f; up <= 1.f; up += 0.01f)
      EXPECT_NE(Scale::chooseByHeight(up, s) % 12, 3);
}

TEST(ScaleTest, SnapAndOctaveShift)
{
   Scale::Settings s; // c minor pentatonic: c d# f g a#
   EXPECT_EQ(Scale::snap(61, s), 60); // c# -> c
   EXPECT_EQ(Scale::snap(62, s), 63); // d -> d#
   EXPECT_EQ(Scale::snap(20, s), 48); // below the range -> lowest
   EXPECT_EQ(Scale::snap(127, s), 82); // above -> highest allowed

   s.octaveShift = 1;
   EXPECT_EQ(Scale::snap(61, s), 72);
   EXPECT_EQ(Scale::chooseByHeight(0.f, s), 60);

   // unlocked = every note in range is fair game
   Scale::Settings unlocked;
   unlocked.lockToScale = false;
   EXPECT_EQ(Scale::snap(61, unlocked), 61);
}

TEST(ScaleTest, BackwardsRangeStillWorks)
{
   Scale::Settings s;
   s.lowestNote = 83;
   s.highestNote = 48;
   EXPECT_EQ(Scale::chooseByHeight(0.f, s), 48);
   EXPECT_EQ(Scale::getNoteName(48), "C3");
   EXPECT_EQ(Scale::getNoteName(61), "C#4");
}

//==============================================================================
TEST(SlicerTest, EqualSlicesCoverTheRegion)
{
   auto sample = makeRamp(10000);
   SampleSlicer::Settings s;
   s.start = 0.2f;
   s.end = 0.8f;
   s.sliceCount = 4;

   EXPECT_EQ(SampleSlicer::getNumSlices(*sample, s), 4);
   EXPECT_EQ(SampleSlicer::getSlice(*sample, s, 0).getStart(), 2000);
   EXPECT_EQ(SampleSlicer::getSlice(*sample, s, 3).getEnd(), 8000);

   for (int i = 0; i < 3; i++)
      EXPECT_EQ(SampleSlicer::getSlice(*sample, s, i).getEnd(), SampleSlicer::getSlice(*sample, s, i + 1).getStart());

   const auto boundaries = SampleSlicer::getBoundaries(*sample, s);
   EXPECT_EQ(boundaries.size(), 5u);

   // start after end gets swapped instead of breaking
   s.start = 0.8f;
   s.end = 0.2f;
   EXPECT_EQ(SampleSlicer::getRegion(*sample, s), juce::Range<int>(2000, 8000));
}

TEST(SlicerTest, TransientsFindTheHits)
{
   const std::vector<int> hits = { 12000, 30000, 52000, 70000 };
   auto sample = makeClicks(hits, 90000);

   SampleSlicer::Settings s;
   s.mode = SampleSlicer::Mode::Transients;
   s.sensitivity = 0.9f;

   // a slice before the first hit, then one starting at each hit
   ASSERT_EQ(SampleSlicer::getNumSlices(*sample, s), 5);
   for (size_t i = 0; i < hits.size(); i++)
      EXPECT_NEAR(SampleSlicer::getSlice(*sample, s, static_cast<int>(i) + 1).getStart(), hits[i], 600);

   // the region cuts off hits outside it
   s.start = 0.4f;
   EXPECT_EQ(SampleSlicer::getNumSlices(*sample, s), 3);
}

TEST(SlicerTest, SensitivityFiltersQuietHits)
{
   auto sample = makeClicks({ 10000, 40000 }, 70000);
   // make the second hit much quieter
   sample->buffer.applyGain(40000, 30000, 0.05f);
   sample->analyse();

   SampleSlicer::Settings s;
   s.mode = SampleSlicer::Mode::Transients;

   s.sensitivity = 1.f;
   const int lots = SampleSlicer::getNumSlices(*sample, s);
   s.sensitivity = 0.f;
   const int few = SampleSlicer::getNumSlices(*sample, s);

   EXPECT_GE(lots, few);
   EXPECT_GE(few, 2); // the loudest hit always counts
}

//==============================================================================
TEST(HitSynthSettingsTest, PulseSweepTableChangesTheDutyCycle)
{
   // the pulse sweep wavetable goes from a square (0.5) at the start to a thin 0.05 pulse at the end
   for (auto [position, width] : { std::pair{ 0.f, 0.5f }, { 0.5f, 0.27f }, { 1.f, 0.05f } })
   {
      HitSynth synth;
      synth.prepareToPlay(SAMPLE_RATE);
      synth.setWavetable(Wavetable::createPreset(Wavetable::Preset::PulseSweep));
      HitSynth::Settings settings;
      settings.position = position;
      synth.setSettings(settings);
      synth.trigger({ 45, 1.f, Waveform::Synth }); // 110hz, long cycles

      auto buffer = render(synth, 4800);
      // the dc gets taken out, so the high part is the bit above the average
      double mean = 0.0;
      for (int i = 480; i < 4800; i++)
         mean += buffer.getSample(0, i);
      mean /= 4320.0;
      int high = 0;
      for (int i = 480; i < 4800; i++)
         if (buffer.getSample(0, i) > mean)
            high++;

      EXPECT_NEAR(static_cast<float>(high) / 4320.f, width, 0.05f) << position;
   }
}

TEST(HitSynthSettingsTest, ReversePlaysBackwards)
{
   HitSynth synth;
   synth.prepareToPlay(SAMPLE_RATE);
   synth.setSample(makeRamp(4000));

   HitSynth::Settings settings;
   settings.reverse = true;
   settings.fadeInMs = 0.f;
   settings.fadeOutMs = 0.f;
   synth.setSettings(settings);
   synth.trigger({ settings.rootNote, 1.f, Waveform::Sample });

   auto buffer = render(synth, 4000);
   // ramp goes up, so reversed it should come down
   EXPECT_GT(buffer.getSample(0, 100), buffer.getSample(0, 2000));
   EXPECT_GT(buffer.getSample(0, 2000), buffer.getSample(0, 3800));
}

TEST(HitSynthSettingsTest, SlicesPickedByNoteAndInOrder)
{
   auto firstSampleOf = [](HitSynth &synth, int note) {
      synth.trigger({ note, 1.f, Waveform::Sample });
      auto buffer = render(synth, 8);
      render(synth, 2000); // let the slice finish so it doesn't add into the next one
      return buffer.getSample(0, 4) / HitSynth::VOICE_GAIN;
   };

   HitSynth::Settings settings;
   settings.slicing.sliceCount = 4;
   settings.fadeInMs = 0.f;
   settings.trackPitch = false;

   // by note: root = slice 0, root + 1 = slice 1 ...
   {
      HitSynth synth;
      synth.prepareToPlay(SAMPLE_RATE);
      synth.setSample(makeRamp(4000));
      synth.setSettings(settings);
      EXPECT_NEAR(firstSampleOf(synth, 60), 0.0f, 0.01f);
      EXPECT_NEAR(firstSampleOf(synth, 61), 0.25f, 0.01f);
      EXPECT_NEAR(firstSampleOf(synth, 63), 0.75f, 0.01f);
      EXPECT_NEAR(firstSampleOf(synth, 64), 0.0f, 0.01f); // wraps around
   }

   // in order ignores the note
   {
      settings.slicing.pick = SampleSlicer::Pick::InOrder;
      HitSynth synth;
      synth.prepareToPlay(SAMPLE_RATE);
      synth.setSample(makeRamp(4000));
      synth.setSettings(settings);
      EXPECT_NEAR(firstSampleOf(synth, 60), 0.0f, 0.01f);
      EXPECT_NEAR(firstSampleOf(synth, 60), 0.25f, 0.01f);
      EXPECT_NEAR(firstSampleOf(synth, 60), 0.5f, 0.01f);
   }
}

TEST(HitSynthSettingsTest, PitchTrackingTransposeAndGain)
{
   auto measureLength = [](const HitSynth::Settings &settings, int note) {
      HitSynth synth;
      synth.prepareToPlay(SAMPLE_RATE);
      synth.setSample(makeRamp(4800));
      synth.setSettings(settings);
      synth.trigger({ note, 1.f, Waveform::Sample });
      auto buffer = render(synth, 20000);
      int last = 0;
      for (int i = 0; i < 20000; i++)
         if (buffer.getSample(0, i) != 0.f)
            last = i;
      return last;
   };

   HitSynth::Settings settings;
   EXPECT_NEAR(measureLength(settings, 60), 4800, 10);
   EXPECT_NEAR(measureLength(settings, 72), 2400, 10); // octave up plays twice as fast

   settings.trackPitch = false;
   EXPECT_NEAR(measureLength(settings, 72), 4800, 10);

   settings.transpose = -12;
   EXPECT_NEAR(measureLength(settings, 72), 9600, 20);

   // gain
   HitSynth quiet;
   quiet.prepareToPlay(SAMPLE_RATE);
   quiet.setSample(makeRamp(4800));
   HitSynth::Settings quietSettings;
   quietSettings.sampleGain = 0.5f;
   quietSettings.fadeInMs = 0.f;
   quiet.setSettings(quietSettings);
   quiet.trigger({ 60, 1.f, Waveform::Sample });
   auto buffer = render(quiet, 2400);
   EXPECT_NEAR(buffer.getSample(0, 2000) / HitSynth::VOICE_GAIN, 0.5f * 2000.f / 4800.f, 0.01f);
}

//==============================================================================
TEST(BallPhysicsTest, PassThroughAndDrag)
{
   const juce::Rectangle<float> bounds(0.f, 0.f, 400.f, 300.f);
   std::vector<GeoUtils::Edge> edges = { { { 0.0, 150.0 }, { 400.0, 150.0 } } };
   juce::Random random(1);

   // always passes: falls straight through the line
   Ball::Physics always;
   always.passThroughChance = 1.f;
   always.random = &random;

   Ball ghost;
   ghost.setXY(200.f, 100.f);
   float loudest = 0.f;
   float lowest = 0.f;
   // stop before it reaches the floor and bounces back up
   for (int i = 0; i < 30; i++)
   {
      loudest = juce::jmax(loudest, ghost.update(1.f / 60.f, bounds, edges, always));
      lowest = juce::jmax(lowest, ghost.getY());
   }
   EXPECT_GT(lowest, 150.f + ghost.radius); // made it all the way under the line
   EXPECT_GT(loudest, 0.f); // still played on the way through
   EXPECT_EQ(ghost.numPassing, 0); // and stopped tracking it once clear

   // silent pass through
   always.passThroughPlays = false;
   Ball quietGhost;
   quietGhost.setXY(200.f, 100.f);
   float crossing = 0.f;
   for (int i = 0; i < 20; i++)
      crossing = juce::jmax(crossing, quietGhost.update(1.f / 60.f, bounds, edges, always));
   EXPECT_EQ(crossing, 0.f);

   // never passes: lands on it
   Ball::Physics never;
   never.random = &random;
   Ball solid;
   solid.setXY(200.f, 100.f);
   for (int i = 0; i < 60; i++)
      solid.update(1.f / 60.f, bounds, edges, never);
   EXPECT_LT(solid.getY(), 150.f);

   // drag slows it down, no gravity keeps it level
   Ball::Physics draggy;
   draggy.gravity = 0.f;
   draggy.drag = 2.f;
   Ball slow;
   slow.setXY(50.f, 50.f);
   slow.velocity = { 300.f, 0.f };
   slow.update(0.5f, bounds, {}, draggy);
   EXPECT_LT(slow.velocity.x, 300.f * 0.5f);
   EXPECT_FLOAT_EQ(slow.getY(), 50.f);
}

//==============================================================================
TEST(EditorSettingsTest, LinesToggleChangesWhatBallsHit)
{
   EditorFixture f;
   f.addSites();
   f.tick();
   const auto bothCount = f.e->getEdges().size();

   setParam(f.processor, Params::LINES, static_cast<float>(Params::Lines::VoronoiOnly));
   f.tick();
   const auto voronoiCount = f.e->getEdges().size();

   setParam(f.processor, Params::LINES, static_cast<float>(Params::Lines::DelaunayOnly));
   f.tick();
   const auto delaunayCount = f.e->getEdges().size();

   EXPECT_GT(voronoiCount, 0u);
   EXPECT_GT(delaunayCount, 0u);
   EXPECT_LT(voronoiCount, bothCount);
   EXPECT_LT(delaunayCount, bothCount);

   // 5 sites in this layout make 4 triangles = 8 sides
   EXPECT_EQ(delaunayCount, 8u);
}

TEST(EditorSettingsTest, BallSettingsApplyToNewBalls)
{
   EditorFixture f;
   setParam(f.processor, Params::BALL_SIZE, 15.f);
   setParam(f.processor, Params::LIFETIME, 2.f);
   setParam(f.processor, Params::LAUNCH_SPEED, 0.f);
   setParam(f.processor, Params::MAX_BALLS, 3.f);

   for (int i = 0; i < 5; i++)
      f.w->dropBall({ 100.f + 50.f * static_cast<float>(i), 50.f });

   ASSERT_EQ(f.w->getBalls().size(), 3u);
   for (const auto &ball : f.w->getBalls())
   {
      EXPECT_FLOAT_EQ(ball.radius, 15.f);
      EXPECT_FLOAT_EQ(ball.timeLeftAlive, 2.f);
      EXPECT_FLOAT_EQ(ball.velocity.x, 0.f);
   }

   // zero gravity keeps them where they are
   setParam(f.processor, Params::GRAVITY, 0.f);
   f.run(0.5);
   EXPECT_FLOAT_EQ(f.w->getBalls().front().getY(), 50.f);

   // gone after their 2 seconds
   f.run(2.0);
   EXPECT_TRUE(f.w->getBalls().empty());
}

TEST(EditorSettingsTest, NoteSettingsApplyToBalls)
{
   EditorFixture f;
   f.addSites();

   // random pitch with only e allowed
   setParam(f.processor, Params::SCALE_LOCK, 0.f);
   setParam(f.processor, Params::PITCH_SOURCE, 1.f);
   for (int pc = 0; pc < 12; pc++)
      setParam(f.processor, Params::noteWeight(pc), pc == 4 ? 1.f : 0.f);

   for (int i = 0; i < 10; i++)
      f.w->dropBall({ 300.f, 50.f });
   for (const auto &ball : f.w->getBalls())
      EXPECT_EQ(ball.noteNumber % 12, 4);

   // midi balls snap when asked (once the sites have reached the audio side)
   f.run(0.01);
   setParam(f.processor, Params::MIDI_DROP_CHANCE, 1.f);
   setParam(f.processor, Params::SNAP_MIDI, 1.f);
   f.w->spawnMidiBall(61, 100);
   EXPECT_EQ(f.w->getBalls().back().noteNumber % 12, 4);
}

TEST(EditorSettingsTest, WholePanelPaints)
{
   EditorFixture f;
   f.addSites();

   // every page and lfo, with and without a modulation on screen
   auto &panel = f.editorPtr->getMainPanel();
   f.processor.addModulation(1, Params::GRAVITY, 0.4f);
   for (auto page : { UI::MainPanel::Page::Synth, UI::MainPanel::Page::Balls, UI::MainPanel::Page::Notes, UI::MainPanel::Page::Sample })
   {
      panel.showPage(page);
      for (int lfo = 0; lfo < Params::NUM_LFOS; lfo++)
      {
         panel.getLfoPanel().selectLfo(lfo);
         juce::Image image(juce::Image::ARGB, f.editorPtr->getWidth(), f.editorPtr->getHeight(), true);
         juce::Graphics g(image);
         f.editorPtr->paintEntireComponent(g, false);
      }
   }
   EXPECT_EQ(panel.getPage(), UI::MainPanel::Page::Sample);
}

//==============================================================================
TEST(StateTest, SettingsSitesAndSampleSurviveSaveAndLoad)
{
   juce::ScopedJuceInitialiser_GUI juceInit;

   auto file = juce::File::createTempFile(".wav");
   {
      juce::WavAudioFormat wav;
      std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(new juce::FileOutputStream(file), 44100.0, 1, 16, {}, 0));
      ASSERT_NE(writer, nullptr);
      auto ramp = makeRamp(4410);
      writer->writeFromAudioSampleBuffer(ramp->buffer, 0, ramp->buffer.getNumSamples());
   }

   juce::MemoryBlock saved;
   {
      VoronoiseAudioProcessor original;
      setParam(original, Params::GRAVITY, 1234.f);
      setParam(original, Params::SCALE_TYPE, static_cast<float>(Scale::Type::Dorian));
      setParam(original, Params::LINES, static_cast<float>(Params::Lines::VoronoiOnly));
      original.getValueTree().getChildWithName("Sites").appendChild(
          juce::ValueTree("Site").setProperty("x", 10.0, nullptr).setProperty("y", 20.0, nullptr), nullptr);
      ASSERT_TRUE(original.loadSample(file));
      original.getStateInformation(saved);
   }

   VoronoiseAudioProcessor restored;
   restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));

   EXPECT_NEAR(restored.getParamValue(Params::GRAVITY), 1234.f, 1.f);
   EXPECT_EQ(static_cast<int>(restored.getParamValue(Params::SCALE_TYPE)), static_cast<int>(Scale::Type::Dorian));
   EXPECT_EQ(static_cast<int>(restored.getParamValue(Params::LINES)), static_cast<int>(Params::Lines::VoronoiOnly));
   EXPECT_EQ(restored.getValueTree().getChildWithName("Sites").getNumChildren(), 1);
   EXPECT_EQ(restored.getSampleName(), file.getFileNameWithoutExtension());

   // an open editor follows the state swap
   {
      std::unique_ptr<juce::AudioProcessorEditor> editor(restored.createEditor());
      restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
      dynamic_cast<VoronoiseAudioProcessorEditor &>(*editor).getCanvas().syncWithProcessor();
      restored.getValueTree().getChildWithName("Sites").appendChild(
          juce::ValueTree("Site").setProperty("x", 300.0, nullptr).setProperty("y", 200.0, nullptr), nullptr);
   }

   // junk data is ignored instead of crashing
   const char junk[] = "not a plugin state";
   restored.setStateInformation(junk, sizeof(junk));
   EXPECT_NEAR(restored.getParamValue(Params::GRAVITY), 1234.f, 1.f);

   file.deleteFile();
}

TEST(EditorSettingsTest, MidiBallsDropFromCellCentresByChance)
{
   EditorFixture f;
   f.addSites();
   f.tick();

   const auto &centres = f.e->getCellCentres();
   ASSERT_EQ(centres.size(), 5u); // one per site

   // every centre is on the screen
   const juce::Rectangle<float> screen(0.f, 0.f, BallWorld::WIDTH, BallWorld::HEIGHT);
   for (const auto &c : centres)
      EXPECT_TRUE(screen.contains(c));

   // the audio side needs the new sites before midi can drop from them
   f.run(0.01);

   // chance 0 = never, 1 = always
   setParam(f.processor, Params::MIDI_DROP_CHANCE, 0.f);
   for (int i = 0; i < 50; i++)
      f.w->spawnMidiBall(60, 100);
   EXPECT_TRUE(f.w->getBalls().empty());

   setParam(f.processor, Params::MIDI_DROP_CHANCE, 1.f);
   setParam(f.processor, Params::MAX_BALLS, 500.f);
   for (int i = 0; i < 50; i++)
      f.w->spawnMidiBall(60, 100);
   ASSERT_EQ(f.w->getBalls().size(), 50u);

   // each one starts on a cell centre
   for (const auto &ball : f.w->getBalls())
   {
      bool onCentre = false;
      for (const auto &c : centres)
         onCentre = onCentre || c.getDistanceFrom({ ball.getX(), ball.getY() }) < 0.01f;
      EXPECT_TRUE(onCentre);
   }

   // somewhere in between drops roughly that often
   setParam(f.processor, Params::MIDI_DROP_CHANCE, 0.3f);
   const auto before = f.w->getBalls().size();
   for (int i = 0; i < 400; i++)
      f.w->spawnMidiBall(60, 100);
   const auto dropped = static_cast<float>(f.w->getBalls().size() - before);
   EXPECT_NEAR(dropped / 400.f, 0.3f, 0.08f);
}
