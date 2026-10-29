#include <gtest/gtest.h>
#include <memory>
#include <set>

#include "synth/Wavetable.h"
#include "synth/HitSynth.h"
#include "Voronoise/Params.h"
#include "Voronoise/PluginProcessor.h"
#include "Voronoise/PluginEditor.h"

namespace
{
   constexpr double SAMPLE_RATE = 48000.0;

   // one cycle read straight out of the table at a low pitch (all harmonics allowed)
   std::vector<float> readCycle(const Wavetable &table, float position, double increment = 1.0 / Wavetable::FRAME_SIZE)
   {
      std::vector<float> cycle;
      for (int i = 0; i < Wavetable::FRAME_SIZE; i++)
         cycle.push_back(table.getSample(static_cast<double>(i) / Wavetable::FRAME_SIZE, increment, position));
      return cycle;
   }

   float maxDifference(const std::vector<float> &a, const std::vector<float> &b)
   {
      float worst = 0.f;
      for (size_t i = 0; i < a.size() && i < b.size(); i++)
         worst = juce::jmax(worst, std::abs(a[i] - b[i]));
      return worst;
   }

   std::vector<float> withoutDc(std::vector<float> frame)
   {
      double mean = 0.0;
      for (float v : frame)
         mean += v;
      mean /= static_cast<double>(frame.size());
      for (auto &v : frame)
         v -= static_cast<float>(mean);
      return frame;
   }

   void setParam(VoronoiseAudioProcessor &processor, const juce::String &id, float realValue)
   {
      auto *param = processor.apvts.getParameter(id);
      ASSERT_NE(param, nullptr) << id;
      param->setValueNotifyingHost(param->convertTo0to1(realValue));
   }
}

//==============================================================================
TEST(WavetableTest, FullDetailLevelMatchesWhatWasDrawn)
{
   auto table = Wavetable::createPreset(Wavetable::Preset::BasicShapes);
   ASSERT_EQ(table->getNumFrames(), 4);

   // sine and triangle have no jumps, so the band limited copy should be nearly identical
   EXPECT_LT(maxDifference(readCycle(*table, 0.f), withoutDc(table->getFrame(0))), 0.01f);
   EXPECT_LT(maxDifference(readCycle(*table, 1.f / 3.f), withoutDc(table->getFrame(1))), 0.02f);

   // a drawn frame with an offset loses the offset (dc would thump)
   std::vector<float> lifted(Wavetable::FRAME_SIZE);
   for (int i = 0; i < Wavetable::FRAME_SIZE; i++)
      lifted[static_cast<size_t>(i)] = 0.5f + 0.4f * std::sin(juce::MathConstants<float>::twoPi * static_cast<float>(i) / Wavetable::FRAME_SIZE);
   Wavetable liftedTable({ lifted }, "lifted");
   double mean = 0.0;
   for (float v : readCycle(liftedTable, 0.f))
      mean += v;
   EXPECT_NEAR(mean / Wavetable::FRAME_SIZE, 0.0, 1e-3);
}

TEST(WavetableTest, HighNotesLoseHarmonicsInsteadOfAliasing)
{
   auto saw = Wavetable::createPreset(Wavetable::Preset::Saw);

   // 15khz at 48k: only the fundamental fits under nyquist, so the saw comes out as a pure sine
   const double increment = 15000.0 / SAMPLE_RATE;
   double dot = 0.0, sawEnergy = 0.0, sineEnergy = 0.0;
   for (int i = 0; i < 1000; i++)
   {
      const double phase = static_cast<double>(i) / 1000.0;
      const double s = saw->getSample(phase, increment, 0.f);
      const double sine = std::sin(juce::MathConstants<double>::twoPi * phase);
      dot += s * sine;
      sawEnergy += s * s;
      sineEnergy += sine * sine;
   }
   EXPECT_GT(std::abs(dot) / std::sqrt(sawEnergy * sineEnergy), 0.999);

   // a low note keeps the sharp edge
   const auto low = readCycle(*saw, 0.f);
   float biggestJump = 0.f;
   for (size_t i = 1; i < low.size(); i++)
      biggestJump = juce::jmax(biggestJump, std::abs(low[i] - low[i - 1]));
   EXPECT_GT(biggestJump, 0.5f);
}

TEST(WavetableTest, PositionMorphsBetweenFrames)
{
   auto table = Wavetable::createPreset(Wavetable::Preset::BasicShapes);
   const auto sine = readCycle(*table, 0.f);
   const auto triangle = readCycle(*table, 1.f / 3.f);
   const auto between = readCycle(*table, 1.f / 6.f);

   for (size_t i = 0; i < sine.size(); i += 64)
      EXPECT_NEAR(between[i], 0.5f * (sine[i] + triangle[i]), 1e-4f) << i;

   // out of range positions clamp to the ends
   EXPECT_LT(maxDifference(readCycle(*table, -1.f), sine), 1e-6f);
   EXPECT_LT(maxDifference(readCycle(*table, 2.f), readCycle(*table, 1.f)), 1e-6f);
}

TEST(WavetableTest, AudioIsCutIntoFrames)
{
   // 10 frames' worth of a rising tone
   juce::AudioBuffer<float> audio(2, Wavetable::FRAME_SIZE * 10);
   for (int i = 0; i < audio.getNumSamples(); i++)
   {
      const float v = 0.5f * std::sin(static_cast<float>(i) * 0.01f * (1.f + static_cast<float>(i) / 10000.f));
      audio.setSample(0, i, v);
      audio.setSample(1, i, v);
   }
   auto table = Wavetable::fromAudio(audio, "rising");
   ASSERT_NE(table, nullptr);
   EXPECT_EQ(table->getNumFrames(), 10);
   EXPECT_EQ(table->getName(), "rising");

   // normalised to full scale
   float peak = 0.f;
   for (const auto &frame : table->getFrames())
      for (float v : frame)
         peak = juce::jmax(peak, std::abs(v));
   EXPECT_NEAR(peak, 1.f, 1e-4f);

   // a single short cycle becomes one frame, a huge file gets capped, silence is refused
   juce::AudioBuffer<float> shortCycle(1, 300);
   for (int i = 0; i < 300; i++)
      shortCycle.setSample(0, i, std::sin(juce::MathConstants<float>::twoPi * static_cast<float>(i) / 300.f));
   EXPECT_EQ(Wavetable::fromAudio(shortCycle, "short")->getNumFrames(), 1);

   juce::AudioBuffer<float> huge(1, Wavetable::FRAME_SIZE * 200);
   for (int i = 0; i < huge.getNumSamples(); i++)
      huge.setSample(0, i, std::sin(static_cast<float>(i) * 0.05f));
   EXPECT_EQ(Wavetable::fromAudio(huge, "huge")->getNumFrames(), Wavetable::MAX_FRAMES);

   juce::AudioBuffer<float> silence(1, 5000);
   silence.clear();
   EXPECT_EQ(Wavetable::fromAudio(silence, "silent"), nullptr);
}

TEST(WavetableTest, SwappingAFrameLeavesTheOthers)
{
   auto table = Wavetable::createPreset(Wavetable::Preset::BasicShapes);
   std::vector<float> flat(Wavetable::FRAME_SIZE, 0.25f);
   Wavetable edited(*table, 2, flat);

   EXPECT_EQ(edited.getNumFrames(), 4);
   EXPECT_EQ(edited.getFrame(2), flat);
   EXPECT_EQ(edited.getFrame(0), table->getFrame(0));
   EXPECT_EQ(edited.getFrame(3), table->getFrame(3));

   // and the whole thing round trips through a project
   auto restored = Wavetable::fromMemoryBlock(edited.toMemoryBlock(), "restored");
   ASSERT_NE(restored, nullptr);
   EXPECT_EQ(restored->getFrames(), edited.getFrames());
}

//==============================================================================
TEST(WavetableProcessorTest, NewTableReachesTheAudioAndIsSaved)
{
   juce::ScopedJuceInitialiser_GUI juceInit;
   juce::MemoryBlock saved;
   {
      VoronoiseAudioProcessor processor;
      processor.prepareToPlay(SAMPLE_RATE, 480);

      // a square only table, so a held note isn't a sine anymore
      processor.setWavetable(Wavetable::createPreset(Wavetable::Preset::Square));
      setParam(processor, Params::AMP_SUSTAIN, 1.f);
      processor.getKeyboardState().noteOn(1, 45, 1.f);

      juce::AudioBuffer<float> buffer(2, 480);
      for (int i = 0; i < 10; i++)
      {
         juce::MidiBuffer midi;
         processor.processBlock(buffer, midi);
      }
      // a square sits near its peak most of the time, a sine doesn't
      const float peak = buffer.getMagnitude(0, 0, 480);
      int nearPeak = 0;
      for (int i = 0; i < 480; i++)
         if (std::abs(buffer.getSample(0, i)) > 0.7f * peak)
            nearPeak++;
      EXPECT_GT(nearPeak, 480 * 3 / 4);

      processor.getStateInformation(saved);
   }

   VoronoiseAudioProcessor restored;
   restored.setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
   EXPECT_EQ(restored.getWavetable()->getName(), "Square");
   EXPECT_EQ(restored.getWavetable()->getFrames(), Wavetable::createPreset(Wavetable::Preset::Square)->getFrames());
}

//==============================================================================
namespace
{
   // fires a bunch of hits one at a time and measures each one
   struct HitResult {
      double frequency;
      float left, right;
      float spread; // how different this hit's waveform is from a plain sine
   };

   std::vector<HitResult> fireHits(const HitSynth::Settings &settings, int count)
   {
      std::vector<HitResult> results;
      HitSynth synth;
      synth.prepareToPlay(SAMPLE_RATE);
      synth.setSettings(settings);
      for (int i = 0; i < count; i++)
      {
         synth.trigger({ 69, 1.f, Waveform::Synth });
         juce::AudioBuffer<float> buffer(2, 4800);
         buffer.clear();
         synth.render(buffer, 0, 4800);
         juce::AudioBuffer<float> rest(2, 48000);
         rest.clear();
         synth.render(rest, 0, 48000); // let it finish before the next hit

         // pitch from the time between the first and last upward zero crossing (just counting
         // crossings in a short window is off by up to a cycle, which is half a semitone at 220hz)
         const auto *data = buffer.getReadPointer(0);
         double firstCrossing = -1.0, lastCrossing = -1.0;
         int crossings = 0;
         for (int s = 1201; s < 4800; s++)
         {
            if (data[s - 1] < 0.f && data[s] >= 0.f)
            {
               const double exact = (s - 1) + data[s - 1] / (data[s - 1] - data[s]);
               if (firstCrossing < 0.0)
                  firstCrossing = exact;
               lastCrossing = exact;
               crossings++;
            }
         }
         const double frequency = crossings > 1 ? (crossings - 1) * SAMPLE_RATE / (lastCrossing - firstCrossing) : 0.0;

         // ratio of peak to rms: 1.41 for a sine, 1 for a square
         const float peak = buffer.getMagnitude(0, 1200, 3600);
         const float rms = buffer.getRMSLevel(0, 1200, 3600);
         results.push_back({ frequency, buffer.getMagnitude(0, 1200, 3600),
                             buffer.getMagnitude(1, 1200, 3600), rms > 0.f ? peak / rms : 0.f });
      }
      return results;
   }
}

TEST(HitRandomTest, NothingChangesWhenTheAmountsAreZero)
{
   const auto hits = fireHits({}, 6);
   for (const auto &hit : hits)
   {
      EXPECT_NEAR(hit.frequency, 440.0, 20.0);
      EXPECT_NEAR(hit.left, hits.front().left, 1e-4f);
      EXPECT_NEAR(hit.left, hit.right, 1e-4f);
   }
}

TEST(HitRandomTest, EachAmountVariesTheHits)
{
   HitSynth::Settings pitch;
   pitch.randomPitch = 12;
   std::set<int> pitches;
   for (const auto &hit : fireHits(pitch, 12))
      pitches.insert(juce::roundToInt(12.0 * std::log2(hit.frequency / 440.0)));
   EXPECT_GE(pitches.size(), 3u);
   for (int semitones : pitches)
      EXPECT_LE(std::abs(semitones), 12);

   HitSynth::Settings pan;
   pan.randomPan = 1.f;
   bool wentLeft = false, wentRight = false;
   for (const auto &hit : fireHits(pan, 12))
   {
      wentLeft = wentLeft || hit.left > hit.right * 1.2f;
      wentRight = wentRight || hit.right > hit.left * 1.2f;
   }
   EXPECT_TRUE(wentLeft && wentRight);

   HitSynth::Settings level;
   level.randomLevel = 1.f;
   float quietest = 1.f, loudest = 0.f;
   for (const auto &hit : fireHits(level, 12))
   {
      quietest = juce::jmin(quietest, hit.left);
      loudest = juce::jmax(loudest, hit.left);
   }
   EXPECT_LT(quietest, loudest * 0.7f);

   // random wt position on the default table: some hits land on the square end, some on the sine end
   HitSynth::Settings position;
   position.position = 0.5f;
   position.randomPosition = 1.f;
   float sineLike = 0.f, squareLike = 10.f;
   for (const auto &hit : fireHits(position, 16))
   {
      sineLike = juce::jmax(sineLike, hit.spread);
      squareLike = juce::jmin(squareLike, hit.spread);
   }
   EXPECT_GT(sineLike - squareLike, 0.15f);
}

//==============================================================================
TEST(WavetableViewTest, DrawingAndEditingFrames)
{
   juce::ScopedJuceInitialiser_GUI juceInit;
   VoronoiseAudioProcessor processor;
   UI::WavetableView view(processor);
   view.setSize(360, 150);

   // starts on the default table, editing frame 1 (the sine)
   ASSERT_EQ(processor.getWavetable()->getNumFrames(), 4);
   EXPECT_EQ(view.getEditFrame(), 0);

   // draw a flat line across the top of the frame
   view.setDrawMode(true);
   const auto area = view.getWaveArea().reduced(6.f);
   view.drawStroke({ area.getX(), area.getY() + 1.f }, { area.getRight(), area.getY() + 1.f });
   view.commit();
   for (float v : processor.getWavetable()->getFrame(0))
      EXPECT_GT(v, 0.95f);
   EXPECT_EQ(processor.getWavetable()->getFrame(1), Wavetable::createPreset(Wavetable::Preset::BasicShapes)->getFrame(1));

   // moving between frames moves the wt position knob
   view.goToFrame(2);
   EXPECT_EQ(view.getEditFrame(), 2);
   EXPECT_NEAR(processor.getBaseValue(Params::WT_POSITION), 2.f / 3.f, 1e-3f);

   // add copies the current frame after it, remove takes it away again
   view.addFrame();
   EXPECT_EQ(processor.getWavetable()->getNumFrames(), 5);
   EXPECT_EQ(view.getEditFrame(), 3);
   EXPECT_EQ(processor.getWavetable()->getFrame(3), processor.getWavetable()->getFrame(2));
   view.removeFrame();
   EXPECT_EQ(processor.getWavetable()->getNumFrames(), 4);

   // presets replace the table and go back to the first frame
   view.applyPreset(Wavetable::Preset::HarmonicSweep);
   EXPECT_EQ(processor.getWavetable()->getNumFrames(), 16);
   EXPECT_EQ(view.getEditFrame(), 0);

   // it paints in both modes
   for (bool draw : { false, true })
   {
      view.setDrawMode(draw);
      juce::Image image(juce::Image::ARGB, 360, 150, true);
      juce::Graphics g(image);
      view.paintEntireComponent(g, false);
   }
}
