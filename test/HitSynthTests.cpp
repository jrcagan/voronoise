#include <gtest/gtest.h>
#include <memory>
#include <cmath>

#include "synth/HitSynth.h"
#include "primitives/Ball.h"
#include "Voronoise/PluginProcessor.h"
#include "Voronoise/PluginEditor.h"

namespace
{
   constexpr double SAMPLE_RATE = 48000.0;

   juce::AudioBuffer<float> renderHit(HitSynth &synth, const HitSynth::Hit &hit, int numSamples)
   {
      juce::AudioBuffer<float> buffer(2, numSamples);
      buffer.clear();
      synth.trigger(hit);
      synth.render(buffer, 0, numSamples);
      return buffer;
   }

   // counts upward zero crossings to estimate the pitch
   double estimateFrequency(const juce::AudioBuffer<float> &buffer, int start, int length)
   {
      const auto *data = buffer.getReadPointer(0);
      int crossings = 0;
      for (int i = start + 1; i < start + length; i++)
         if (data[i - 1] < 0.f && data[i] >= 0.f)
            crossings++;
      return crossings * SAMPLE_RATE / length;
   }

   HitSynth::SampleData::Ptr makeTestSample(double frequency, double sampleRate, int length)
   {
      HitSynth::SampleData::Ptr data = new HitSynth::SampleData();
      data->sampleRate = sampleRate;
      data->buffer.setSize(1, length);
      for (int i = 0; i < length; i++)
         data->buffer.setSample(0, i, static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * frequency * i / sampleRate)));
      return data;
   }
}

TEST(HitSynthTest, EveryWavetableFrameMakesSoundAtTheRightPitch)
{
   // the default table is sine, triangle, saw, square
   for (float position : { 0.f, 1.f / 3.f, 2.f / 3.f, 1.f })
   {
      HitSynth synth;
      synth.prepareToPlay(SAMPLE_RATE);
      HitSynth::Settings settings;
      settings.position = position;
      synth.setSettings(settings);

      // a4 = 440hz
      auto buffer = renderHit(synth, { 69, 1.f, Waveform::Synth }, 4800);

      EXPECT_GT(buffer.getMagnitude(0, 0, 4800), 0.1f) << "position " << position;
      EXPECT_NEAR(estimateFrequency(buffer, 0, 4800), 440.0, 15.0) << "position " << position;

      for (int i = 0; i < 4800; i++)
         ASSERT_TRUE(std::isfinite(buffer.getSample(0, i)));
   }
}

TEST(HitSynthTest, OscillatorsDecayAndFreeTheirVoice)
{
   HitSynth synth;
   synth.prepareToPlay(SAMPLE_RATE);

   auto buffer = renderHit(synth, { 60, 1.f, Waveform::Synth }, static_cast<int>(SAMPLE_RATE * 5));

   EXPECT_EQ(synth.getNumActiveVoices(), 0);
   const int end = buffer.getNumSamples();
   EXPECT_LT(buffer.getMagnitude(0, end - 4800, 4800), 0.001f);
}

TEST(HitSynthTest, GainScalesVolume)
{
   HitSynth loudSynth, quietSynth;
   loudSynth.prepareToPlay(SAMPLE_RATE);
   quietSynth.prepareToPlay(SAMPLE_RATE);

   auto loud = renderHit(loudSynth, { 60, 1.f, Waveform::Synth }, 2400);
   auto quiet = renderHit(quietSynth, { 60, 0.25f, Waveform::Synth }, 2400);

   EXPECT_NEAR(quiet.getMagnitude(0, 0, 2400) / loud.getMagnitude(0, 0, 2400), 0.25f, 0.01f);
}

TEST(HitSynthTest, ManyHitsStealVoicesInsteadOfOverflowing)
{
   HitSynth synth;
   synth.prepareToPlay(SAMPLE_RATE);

   juce::AudioBuffer<float> buffer(2, 64);
   for (int i = 0; i < 200; i++)
   {
      synth.trigger({ 40 + i % 40, 1.f, Waveform::Synth });
      buffer.clear();
      synth.render(buffer, 0, 64);
   }

   EXPECT_EQ(synth.getNumActiveVoices(), HitSynth::NUM_VOICES);
}

TEST(HitSynthTest, SampleIsSilentUntilOneIsLoaded)
{
   HitSynth synth;
   synth.prepareToPlay(SAMPLE_RATE);

   auto buffer = renderHit(synth, { 60, 1.f, Waveform::Sample }, 2400);
   EXPECT_EQ(buffer.getMagnitude(0, 2400), 0.f);
   EXPECT_EQ(synth.getNumActiveVoices(), 0);
}

TEST(HitSynthTest, SampleIsRepitchedAndResampled)
{
   HitSynth synth;
   synth.prepareToPlay(SAMPLE_RATE);

   // 300hz tone recorded at 44.1k, played on the device at 48k
   synth.setSample(makeTestSample(300.0, 44100.0, 44100));

   // root note plays at the original pitch
   auto root = renderHit(synth, { HitSynth::SAMPLE_ROOT_NOTE, 1.f, Waveform::Sample }, 9600);
   EXPECT_NEAR(estimateFrequency(root, 0, 9600), 300.0, 10.0);

   // an octave up is double
   HitSynth synth2;
   synth2.prepareToPlay(SAMPLE_RATE);
   synth2.setSample(makeTestSample(300.0, 44100.0, 44100));
   auto octave = renderHit(synth2, { HitSynth::SAMPLE_ROOT_NOTE + 12, 1.f, Waveform::Sample }, 9600);
   EXPECT_NEAR(estimateFrequency(octave, 0, 9600), 600.0, 10.0);
}

TEST(HitSynthTest, SampleVoiceStopsAtTheEnd)
{
   HitSynth synth;
   synth.prepareToPlay(SAMPLE_RATE);
   synth.setSample(makeTestSample(300.0, SAMPLE_RATE, 1000));

   auto buffer = renderHit(synth, { HitSynth::SAMPLE_ROOT_NOTE, 1.f, Waveform::Sample }, 4800);
   EXPECT_EQ(synth.getNumActiveVoices(), 0);
   EXPECT_EQ(buffer.getMagnitude(0, 1100, 3700), 0.f);
}

TEST(BallHitTest, ReportsImpactOnlyWhenHittingAnEdge)
{
   const juce::Rectangle<float> bounds(0.f, 0.f, 400.f, 300.f);
   std::vector<GeoUtils::Edge> edges = { { { 0.0, 150.0 }, { 400.0, 150.0 } } };

   Ball ball;
   ball.radius = 5.f;
   ball.setXY(200.f, 100.f);

   EXPECT_EQ(ball.update(1.f / 60.f, bounds, edges), 0.f);

   ball.setXY(200.f, 140.f);
   ball.velocity = { 0.f, 600.f };
   const float impact = ball.update(1.f / 60.f, bounds, edges);
   EXPECT_GT(impact, 550.f);

   // the window border counts as a hit too
   Ball wallBall;
   wallBall.radius = 5.f;
   wallBall.setXY(200.f, 290.f);
   wallBall.velocity = { 0.f, 600.f };
   EXPECT_GT(wallBall.update(1.f / 60.f, bounds, {}), 550.f);

   // and so do the sides
   Ball sideBall;
   sideBall.radius = 5.f;
   sideBall.setXY(8.f, 100.f);
   sideBall.velocity = { -600.f, 0.f };
   EXPECT_GT(sideBall.update(1.f / 60.f, bounds, {}), 550.f);
}

TEST(EditorHitTest, BallsHittingTheBorderMakeSoundWithNoSites)
{
   juce::ScopedJuceInitialiser_GUI juceInit;
   VoronoiseAudioProcessor processor;
   processor.prepareToPlay(48000.0, 512);
   auto editor = std::unique_ptr<juce::AudioProcessorEditor>(processor.createEditor());
   auto &e = dynamic_cast<VoronoiseAudioProcessorEditor &>(*editor).getCanvas();

   ASSERT_TRUE(e.getEdges().empty());
   e.dropBall({ 200.f, 30.f });

   // the audio blocks move the ball now, 120 of them is plenty to reach the floor
   float loudest = 0.f;
   juce::AudioBuffer<float> buffer(2, 512);
   juce::MidiBuffer midi;
   for (int frame = 0; frame < 120; frame++)
   {
      buffer.clear();
      processor.processBlock(buffer, midi);
      loudest = juce::jmax(loudest, buffer.getMagnitude(0, 512));
   }

   EXPECT_GT(loudest, 0.01f);
}

TEST(BallHitTest, QuietWhenRestingAndCoolsDownBetweenHits)
{
   Ball ball;
   EXPECT_FALSE(ball.shouldPlayHit(Ball::MIN_HIT_SPEED * 0.5f));

   EXPECT_TRUE(ball.shouldPlayHit(500.f));
   EXPECT_FALSE(ball.shouldPlayHit(500.f)); // still cooling down

   ball.update(Ball::HIT_COOLDOWN * 2.f, { 0.f, 0.f, 400.f, 300.f }, {});
   EXPECT_TRUE(ball.shouldPlayHit(500.f));

   ball.noteVelocity = 127.f;
   EXPECT_FLOAT_EQ(ball.getHitGain(Ball::LOUD_HIT_SPEED * 2.f), 1.f);
   EXPECT_LT(ball.getHitGain(Ball::LOUD_HIT_SPEED * 0.5f), 0.6f);
}

TEST(EditorHitTest, BallsHittingEdgesMakeSoundThroughTheProcessor)
{
   juce::ScopedJuceInitialiser_GUI juceInit;
   VoronoiseAudioProcessor processor;
   processor.prepareToPlay(48000.0, 512);
   auto editor = std::unique_ptr<juce::AudioProcessorEditor>(processor.createEditor());
   auto &e = dynamic_cast<VoronoiseAudioProcessorEditor &>(*editor).getCanvas();

   auto sites = processor.getValueTree().getChildWithName("Sites");
   for (auto [x, y] : { std::pair{ 80.0, 80.0 }, { 300.0, 90.0 }, { 200.0, 220.0 }, { 60.0, 250.0 } })
      sites.appendChild(juce::ValueTree("Site").setProperty("x", x, nullptr).setProperty("y", y, nullptr), nullptr);

   // delaunay triangle sides are in there too: 4 sites make 2 triangles = 5 sides,
   // plus the inner voronoi edges
   EXPECT_GT(e.getEdges().size(), 5u);

   e.setSound(Waveform::Synth);
   auto *position = processor.apvts.getParameter(Params::WT_POSITION);
   for (float wtPosition : { 0.f, 1.f / 3.f, 2.f / 3.f, 1.f })
   {
      position->setValueNotifyingHost(wtPosition);
      e.dropBall({ 200.f, 30.f });

      float loudest = 0.f;
      juce::AudioBuffer<float> buffer(2, 512);
      juce::MidiBuffer midi;
      for (int frame = 0; frame < 120; frame++)
      {
            buffer.clear();
         processor.processBlock(buffer, midi);
         loudest = juce::jmax(loudest, buffer.getMagnitude(0, 512));
      }

      EXPECT_GT(loudest, 0.01f) << "position " << wtPosition;
   }
}

TEST(EditorHitTest, LoadedSamplePlaysOnHits)
{
   juce::ScopedJuceInitialiser_GUI juceInit;

   // write a short wav to load
   auto file = juce::File::createTempFile(".wav");
   {
      juce::WavAudioFormat wav;
      std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(new juce::FileOutputStream(file), 44100.0, 1, 16, {}, 0));
      ASSERT_NE(writer, nullptr);
      auto tone = makeTestSample(300.0, 44100.0, 22050);
      writer->writeFromAudioSampleBuffer(tone->buffer, 0, tone->buffer.getNumSamples());
   }

   VoronoiseAudioProcessor processor;
   processor.prepareToPlay(48000.0, 512);

   EXPECT_FALSE(processor.loadSample(juce::File::createTempFile(".notaudio")));
   ASSERT_TRUE(processor.loadSample(file));
   EXPECT_EQ(processor.getSampleName(), file.getFileNameWithoutExtension());

   // loading a second time swaps it without problems
   ASSERT_TRUE(processor.loadSample(file));

   processor.hitFifo.push({ HitSynth::SAMPLE_ROOT_NOTE, 1.f, Waveform::Sample });

   juce::AudioBuffer<float> buffer(2, 512);
   buffer.clear();
   juce::MidiBuffer midi;
   processor.processBlock(buffer, midi);
   EXPECT_GT(buffer.getMagnitude(0, 512), 0.1f);

   file.deleteFile();
}

TEST(EditorHitTest, DroppedBallPitchFollowsHeight)
{
   juce::ScopedJuceInitialiser_GUI juceInit;
   VoronoiseAudioProcessor processor;
   auto editor = std::unique_ptr<juce::AudioProcessorEditor>(processor.createEditor());
   auto &e = dynamic_cast<VoronoiseAudioProcessorEditor &>(*editor).getCanvas();

   const float bottom = static_cast<float>(e.getHeight()) - 1.f;
   EXPECT_GT(e.getNoteForHeight(10.f), e.getNoteForHeight(bottom - 10.f));
   EXPECT_EQ(e.getNoteForHeight(bottom), 48);
   EXPECT_LE(e.getNoteForHeight(0.f), 48 + 36);
}
