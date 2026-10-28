#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "synth/HitSynth.h"
#include "primitives/Ball.h"
#include "Voronoise/Params.h"
#include "Voronoise/PluginProcessor.h"
#include "Voronoise/PluginEditor.h"

// end to end checks of ball hits: drop a ball, run the processor the way a host does, and compare
// what comes out against a buffer of what should come out (perfectly timed notes at the moments
// a reference simulation says the ball touched the floor)
namespace
{
   constexpr double SAMPLE_RATE = 48000.0;
   constexpr float GRAVITY = 900.f;
   constexpr float RADIUS = 6.f;
   constexpr float DROP_X = 373.f;

   void setParam(VoronoiseAudioProcessor &processor, const juce::String &id, float realValue)
   {
      auto *param = processor.apvts.getParameter(id);
      param->setValueNotifyingHost(param->convertTo0to1(realValue));
   }

   struct ExpectedHit {
      double seconds;
      float impactSpeed;
   };

   // ground truth for a ball dropped from rest onto the floor: tiny steps of plain gravity,
   // the same restitution, until the bounces are too soft to play
   std::vector<ExpectedHit> referenceBounces(float dropFrom, double maxSeconds, float radius = RADIUS)
   {
      std::vector<ExpectedHit> hits;
      double y = dropFrom, v = 0.0, t = 0.0;
      constexpr double h = 1e-5;
      while (t < maxSeconds)
      {
         v += GRAVITY * h;
         y += v * h;
         t += h;
         if (y + radius > BallWorld::HEIGHT && v > 0.0)
         {
            y = BallWorld::HEIGHT - radius;
            if (v >= Ball::MIN_HIT_SPEED)
               hits.push_back({ t, static_cast<float>(v) });
            v = -v * Ball::RESTITUTION;
         }
      }
      return hits;
   }

   // first sample of each note: level jumps above the threshold after being quiet for a bit
   std::vector<double> findOnsets(const std::vector<float> &out, float threshold = 1e-3f)
   {
      std::vector<double> onsets;
      int quietFor = 1000000;
      for (size_t i = 0; i < out.size(); i++)
      {
         if (std::abs(out[i]) > threshold)
         {
            if (quietFor > static_cast<int>(0.004 * SAMPLE_RATE))
               onsets.push_back(static_cast<double>(i) / SAMPLE_RATE);
            quietFor = 0;
         }
         else
         {
            quietFor++;
         }
      }
      return onsets;
   }

   float peakBetween(const std::vector<float> &out, double from, double to)
   {
      float peak = 0.f;
      for (auto i = static_cast<size_t>(juce::jmax(0.0, from) * SAMPLE_RATE); i < static_cast<size_t>(to * SAMPLE_RATE) && i < out.size(); i++)
         peak = juce::jmax(peak, std::abs(out[i]));
      return peak;
   }

   // short clicky envelope so the bounces don't overlap and each one has a clean onset
   void useShortEnvelope(VoronoiseAudioProcessor &processor, float attackMs = 0.f)
   {
      setParam(processor, Params::AMP_ATTACK, attackMs);
      setParam(processor, Params::AMP_DECAY, 15.f);
      setParam(processor, Params::AMP_SUSTAIN, 0.f);
      setParam(processor, Params::AMP_RELEASE, 5.f);
      setParam(processor, Params::HIT_LENGTH, 20.f);
   }

   struct Fixture {
      juce::ScopedJuceInitialiser_GUI juceInit; // the processor runs a timer
      VoronoiseAudioProcessor processor;

      explicit Fixture(int blockSize = 512)
      {
         processor.setRateAndBufferSizeDetails(SAMPLE_RATE, blockSize);
         processor.prepareToPlay(SAMPLE_RATE, blockSize);
         setParam(processor, Params::LAUNCH_SPEED, 0.f);
         setParam(processor, Params::LIFETIME, 30.f);
         setParam(processor, Params::BALL_SIZE, RADIUS);
         // plain physics so bounces can be checked against the reference (and settle, or stop with bounce 0)
         setParam(processor, Params::MIN_BOUNCE, 0.f);
      }
   };

   struct Run {
      std::vector<float> out; // left channel
      std::vector<ExpectedHit> expected;
      std::vector<double> onsets;
      int note = 60;
   };

   // drops one ball at time zero and runs the processor in host sized blocks
   Run dropAndListen(VoronoiseAudioProcessor &processor, float dropFrom, double seconds, int blockSize = 512)
   {
      Run run;
      run.expected = referenceBounces(dropFrom, seconds);
      const double latency = processor.getWorld().getLatencySamples() / SAMPLE_RATE;
      for (auto &hit : run.expected)
         hit.seconds += latency;

      processor.getWorld().dropBall({ DROP_X, dropFrom });
      run.note = processor.getWorld().getBalls().back().noteNumber;

      juce::AudioBuffer<float> buffer(2, blockSize);
      const auto totalSamples = static_cast<int>(seconds * SAMPLE_RATE);
      for (int start = 0; start < totalSamples; start += blockSize)
      {
         juce::MidiBuffer midi;
         processor.processBlock(buffer, midi);
         run.out.insert(run.out.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + blockSize);
      }

      run.onsets = findOnsets(run.out);
      return run;
   }

   // what it should sound like: the same hit synth, with each note on the exact sample of its bounce
   std::vector<float> expectedBuffer(VoronoiseAudioProcessor &processor, const Run &run)
   {
      HitSynth synth;
      synth.prepareToPlay(SAMPLE_RATE);
      synth.setSettings(processor.getSoundSettings());

      juce::AudioBuffer<float> buffer(2, static_cast<int>(run.out.size()));
      buffer.clear();
      int rendered = 0;
      for (const auto &hit : run.expected)
      {
         const int at = juce::jmin(buffer.getNumSamples(), static_cast<int>(std::round(hit.seconds * SAMPLE_RATE)));
         synth.render(buffer, rendered, at - rendered);
         Ball ball;
         synth.trigger({ run.note, ball.getHitGain(hit.impactSpeed, processor.getParamValue(Params::HIT_DYNAMICS)), Waveform::Synth });
         rendered = at;
      }
      synth.render(buffer, rendered, buffer.getNumSamples() - rendered);
      return { buffer.getReadPointer(0), buffer.getReadPointer(0) + buffer.getNumSamples() };
   }
}

//==============================================================================
TEST(HitTimingTest, EveryBounceLandsOnTimeAndAtTheRightLevel)
{
   Fixture f;
   useShortEnvelope(f.processor);
   const auto run = dropAndListen(f.processor, 40.f, 4.0);
   const auto expected = expectedBuffer(f.processor, run);
   const auto expectedOnsets = findOnsets(expected);

   ASSERT_GE(run.expected.size(), 3u);
   ASSERT_EQ(run.onsets.size(), run.expected.size()) << "a bounce was missed or doubled";
   ASSERT_EQ(expectedOnsets.size(), run.expected.size());

   for (size_t i = 0; i < run.expected.size(); i++)
   {
      // within a millisecond of when the ball really touched
      EXPECT_NEAR(run.onsets[i], expectedOnsets[i], 0.001) << "bounce " << i;

      // and as loud as a perfectly timed note for that impact
      const double at = expectedOnsets[i];
      const float actualPeak = peakBetween(run.out, at - 0.002, at + 0.03);
      const float expectedPeak = peakBetween(expected, at - 0.002, at + 0.03);
      EXPECT_NEAR(actualPeak / expectedPeak, 1.f, 0.05f) << "bounce " << i;
   }

   // the whole output against the expected one, as loudness over time (comparing raw samples would
   // count a 0.1ms shift of a 440hz tone as a big difference)
   constexpr size_t WINDOW = 96; // 2ms
   double difference = 0.0, energy = 0.0;
   for (size_t start = 0; start + WINDOW <= expected.size(); start += WINDOW)
   {
      float actualLevel = 0.f, expectedLevel = 0.f;
      for (size_t i = start; i < start + WINDOW; i++)
      {
         actualLevel = juce::jmax(actualLevel, std::abs(run.out[i]));
         expectedLevel = juce::jmax(expectedLevel, std::abs(expected[i]));
      }
      difference += (actualLevel - expectedLevel) * (actualLevel - expectedLevel);
      energy += expectedLevel * expectedLevel;
   }
   EXPECT_LT(std::sqrt(difference / energy), 0.15);
}

TEST(HitTimingTest, NoBounceIsMissedFromAnyHeight)
{
   for (float dropFrom : { 10.f, 60.f, 150.f, 250.f, 320.f })
   {
      Fixture f;
      useShortEnvelope(f.processor);
      const auto run = dropAndListen(f.processor, dropFrom, 3.0);
      EXPECT_EQ(run.onsets.size(), run.expected.size()) << "dropped from " << dropFrom;
      for (size_t i = 0; i < juce::jmin(run.onsets.size(), run.expected.size()); i++)
         EXPECT_NEAR(run.onsets[i], run.expected[i].seconds, 0.0015) << "dropped from " << dropFrom << ", bounce " << i;
   }
}

TEST(HitTimingTest, TimingDoesNotDependOnTheBlockSize)
{
   std::vector<std::vector<double>> onsetsPerBlockSize;
   for (int blockSize : { 64, 512, 2048 })
   {
      Fixture f(blockSize);
      useShortEnvelope(f.processor);
      onsetsPerBlockSize.push_back(dropAndListen(f.processor, 40.f, 4.0, blockSize).onsets);
   }

   for (size_t b = 1; b < onsetsPerBlockSize.size(); b++)
   {
      ASSERT_EQ(onsetsPerBlockSize[b].size(), onsetsPerBlockSize[0].size());
      for (size_t i = 0; i < onsetsPerBlockSize[0].size(); i++)
         EXPECT_NEAR(onsetsPerBlockSize[b][i], onsetsPerBlockSize[0][i], 1.0 / SAMPLE_RATE * 2.0) << "bounce " << i;
   }
}

TEST(HitTimingTest, LongAttackStillReachesFullLevel)
{
   // a hit used to let go after the hit length even mid attack, so long attacks barely made a sound
   float shortPeak = 0.f;
   for (float attack : { 5.f, 500.f, 2000.f })
   {
      Fixture f;
      setParam(f.processor, Params::AMP_ATTACK, attack);
      setParam(f.processor, Params::AMP_SUSTAIN, 1.f);
      setParam(f.processor, Params::BOUNCINESS, 0.f); // one hit only, so nothing else adds to it
      const auto run = dropAndListen(f.processor, 300.f, 3.5);
      const double firstBounce = run.expected.front().seconds;

      const float peak = peakBetween(run.out, firstBounce, 3.5);
      if (attack < 10.f)
         shortPeak = peak;
      else
         EXPECT_NEAR(peak / shortPeak, 1.f, 0.1f) << "attack " << attack;
   }
}

TEST(HitTimingTest, BallsKeepPlayingWithTheEditorClosed)
{
   Fixture f;
   useShortEnvelope(f.processor);
   setParam(f.processor, Params::MIDI_DROP_CHANCE, 1.f);
   f.processor.getValueTree().getChildWithName("Sites").appendChild(
       juce::ValueTree("Site").setProperty("x", 373.0, nullptr).setProperty("y", 40.0, nullptr), nullptr);

   // no editor anywhere: a midi note should still drop a ball that bounces and plays
   juce::AudioBuffer<float> buffer(2, 512);
   std::vector<float> out;
   for (int block = 0; block < 300; block++)
   {
      juce::MidiBuffer midi;
      if (block == 1)
         midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
      if (block == 2)
         midi.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
      f.processor.processBlock(buffer, midi);
      out.insert(out.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + 512);
   }

   // the held note itself plays at the start (and releases), then the ball's first floor hit about 0.8s later
   EXPECT_GT(peakBetween(out, 0.6, 1.2), 0.05f);
}

//==============================================================================
namespace
{
   // plays one key for holdSeconds, with a single site above the floor so the note drops a ball there
   std::vector<float> pressKeyAndListen(VoronoiseAudioProcessor &processor, double holdSeconds, double seconds, bool dropBall = true)
   {
      setParam(processor, Params::MIDI_DROP_CHANCE, dropBall ? 1.f : 0.f);
      processor.getValueTree().getChildWithName("Sites").appendChild(
          juce::ValueTree("Site").setProperty("x", static_cast<double>(DROP_X), nullptr).setProperty("y", 40.0, nullptr), nullptr);

      constexpr int BLOCK = 512;
      const auto releaseAt = static_cast<juce::int64>(holdSeconds * SAMPLE_RATE);
      juce::AudioBuffer<float> buffer(2, BLOCK);
      std::vector<float> out;
      for (juce::int64 start = 0; start < static_cast<juce::int64>(seconds * SAMPLE_RATE); start += BLOCK)
      {
         juce::MidiBuffer midi;
         if (start == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
         if (releaseAt >= start && releaseAt < start + BLOCK)
            midi.addEvent(juce::MidiMessage::noteOff(1, 60), static_cast<int>(releaseAt - start));
         processor.processBlock(buffer, midi);
         out.insert(out.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + BLOCK);
      }
      return out;
   }

   // loudness every 2ms from a start point, scaled so the loudest bit is 1
   std::vector<float> envelopeFrom(const std::vector<float> &out, double from, double length)
   {
      std::vector<float> envelope;
      constexpr size_t WINDOW = 96;
      const auto start = static_cast<size_t>(from * SAMPLE_RATE);
      const auto end = juce::jmin(out.size(), start + static_cast<size_t>(length * SAMPLE_RATE));
      for (size_t i = start; i + WINDOW <= end; i += WINDOW)
      {
         float level = 0.f;
         for (size_t s = i; s < i + WINDOW; s++)
            level = juce::jmax(level, std::abs(out[s]));
         envelope.push_back(level);
      }
      const float peak = *std::max_element(envelope.begin(), envelope.end());
      for (auto &level : envelope)
         level /= peak;
      return envelope;
   }

   // where a note starts: the first sample above 1% of that note's own peak (a fixed threshold
   // would find a quiet note later than a loud one, since it takes longer to climb past it)
   double startOf(const std::vector<float> &out, double from, double length)
   {
      const float peak = peakBetween(out, from, from + length);
      for (auto i = static_cast<size_t>(from * SAMPLE_RATE); i < out.size(); i++)
         if (std::abs(out[i]) > 0.01f * peak)
            return static_cast<double>(i) / SAMPLE_RATE;
      return from;
   }

   // how long until it fades under 1% of its peak and stays there
   double soundingFor(const std::vector<float> &envelope)
   {
      size_t last = 0;
      for (size_t i = 0; i < envelope.size(); i++)
         if (envelope[i] > 0.01f)
            last = i;
      return static_cast<double>(last + 1) * 0.002;
   }

   void useAudibleEnvelope(VoronoiseAudioProcessor &processor)
   {
      setParam(processor, Params::AMP_ATTACK, 10.f);
      setParam(processor, Params::AMP_DECAY, 60.f);
      setParam(processor, Params::AMP_SUSTAIN, 0.6f);
      setParam(processor, Params::AMP_RELEASE, 120.f);
   }
}

TEST(KeyLengthTest, HitsCopyTheKeysEnvelopeIncludingHowLongItWasHeld)
{
   double previousLength = 0.0;
   for (double hold : { 0.08, 0.4 })
   {
      Fixture f;
      useAudibleEnvelope(f.processor);
      const auto out = pressKeyAndListen(f.processor, hold, 2.5);

      // the key's own note right at the start, and the ball's first floor hit: it waits until the key
      // is let go, then falls from 40px up (~0.82s). midi balls grow with velocity, so use its real size
      ASSERT_EQ(f.processor.getWorld().getBalls().size(), 1u);
      const float radius = f.processor.getWorld().getBalls().front().radius;
      const auto firstHit = hold + referenceBounces(40.f, 2.0, radius).front().seconds + f.processor.getWorld().getLatencySamples() / SAMPLE_RATE;
      const double length = hold + 0.12 + 0.05;
      const double hitStart = startOf(out, firstHit - 0.005, length);
      EXPECT_NEAR(hitStart, firstHit, 0.001) << "lands when it should, held " << hold;
      const auto key = envelopeFrom(out, startOf(out, 0.0, length), length);
      const auto hit = envelopeFrom(out, hitStart, length);

      // same shape, 2ms at a time: attack, decay to sustain, held as long as the key, same release
      float worst = 0.f;
      for (size_t i = 0; i < juce::jmin(key.size(), hit.size()); i++)
         worst = juce::jmax(worst, std::abs(key[i] - hit[i]));
      EXPECT_LT(worst, 0.1f) << "held " << hold;
      EXPECT_NEAR(soundingFor(hit), soundingFor(key), 0.006) << "held " << hold;
      EXPECT_NEAR(soundingFor(hit), hold + 0.12, 0.01) << "held " << hold;

      // a longer press makes a longer hit
      if (previousLength > 0.0)
         EXPECT_GT(soundingFor(hit), previousLength + 0.25);
      previousLength = soundingFor(hit);
   }
}

TEST(KeyLengthTest, BallWaitsAtItsCentreUntilTheKeyIsLetGo)
{
   Fixture f;
   useAudibleEnvelope(f.processor);
   setParam(f.processor, Params::MIDI_DROP_CHANCE, 1.f);
   f.processor.getValueTree().getChildWithName("Sites").appendChild(
       juce::ValueTree("Site").setProperty("x", static_cast<double>(DROP_X), nullptr).setProperty("y", 40.0, nullptr), nullptr);
   const auto &balls = f.processor.getWorld().getBalls();

   // key down: the ball shows up at the site and just hangs there, for as long as the key is held
   juce::AudioBuffer<float> buffer(2, 512);
   std::vector<float> out;
   auto block = [&](juce::MidiBuffer midi) {
      f.processor.processBlock(buffer, midi);
      out.insert(out.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + 512);
   };
   juce::MidiBuffer down;
   down.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 0);
   block(down);
   ASSERT_EQ(balls.size(), 1u);
   EXPECT_TRUE(balls.front().isWaiting());

   const float lifetime = balls.front().timeLeftAlive;
   while (out.size() < static_cast<size_t>(1.5 * SAMPLE_RATE))
      block({});
   EXPECT_FLOAT_EQ(balls.front().getY(), 40.f);
   EXPECT_FLOAT_EQ(balls.front().timeLeftAlive, lifetime); // its life hasn't started

   // the canvas sees it as waiting too
   BallWorld::Snapshot snapshot;
   ASSERT_TRUE(f.processor.getWorld().pullSnapshot(snapshot));
   EXPECT_TRUE(snapshot.balls[0].waiting);

   // key up: it falls, and its first floor hit comes ~0.82s later (nothing in between but the key's release)
   juce::MidiBuffer up;
   up.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
   const double letGo = static_cast<double>(out.size()) / SAMPLE_RATE;
   block(up);
   EXPECT_FALSE(balls.front().isWaiting());
   EXPECT_NEAR(balls.front().gateSeconds, letGo, 0.001);
   while (out.size() < static_cast<size_t>((letGo + 1.2) * SAMPLE_RATE))
      block({});

   const double firstHit = letGo + referenceBounces(40.f, 2.0, balls.front().radius).front().seconds
                           + f.processor.getWorld().getLatencySamples() / SAMPLE_RATE;
   EXPECT_LT(peakBetween(out, letGo + 0.2, firstHit - 0.01), 0.001f);
   EXPECT_GT(peakBetween(out, firstHit, firstHit + 0.05), 0.02f);
   EXPECT_GT(std::abs(balls.front().getY() - 40.f), 50.f); // it's been bouncing
}

TEST(KeyLengthTest, ClickedBallsStillUseTheHitLengthKnob)
{
   Fixture f;
   useAudibleEnvelope(f.processor);
   setParam(f.processor, Params::HIT_LENGTH, 300.f);
   setParam(f.processor, Params::BOUNCINESS, 0.f); // one hit
   const auto run = dropAndListen(f.processor, 40.f, 2.0);
   ASSERT_FALSE(run.onsets.empty());
   const auto hit = envelopeFrom(run.out, run.onsets.front(), 1.0);

   // held for the attack plus the hit length, then the release
   EXPECT_NEAR(soundingFor(hit), 0.01 + 0.3 + 0.12, 0.012);
}

//==============================================================================
TEST(HitLevelTest, BouncesAreAsLoudAsTheKeyThatMadeThem)
{
   for (float dynamics : { 0.f, 0.25f })
   {
      Fixture f;
      useAudibleEnvelope(f.processor);
      setParam(f.processor, Params::HIT_DYNAMICS, dynamics);
      const auto out = pressKeyAndListen(f.processor, 0.08, 3.5);

      // the key's own note, then each bounce on its own (the ball's let go at 0.08s)
      const float keyPeak = peakBetween(out, 0.0, 0.3);
      ASSERT_EQ(f.processor.getWorld().getBalls().size(), 1u);
      const float radius = f.processor.getWorld().getBalls().front().radius;
      const double latency = f.processor.getWorld().getLatencySamples() / SAMPLE_RATE;
      const auto bounces = referenceBounces(40.f, 3.4, radius);
      ASSERT_GE(bounces.size(), 3u);

      for (size_t i = 0; i < 3; i++)
      {
         const double at = 0.08 + bounces[i].seconds + latency;
         const float hitPeak = peakBetween(out, at - 0.002, at + 0.25);
         // as loud as the key with no dynamics, and never much quieter with the default amount
         if (dynamics == 0.f)
            EXPECT_NEAR(hitPeak / keyPeak, 1.f, 0.03f) << "bounce " << i;
         else
            EXPECT_GT(hitPeak / keyPeak, 0.88f) << "bounce " << i << " at dynamics " << dynamics;
         EXPECT_LT(hitPeak / keyPeak, 1.03f) << "bounce " << i;
      }
   }
}

//==============================================================================
namespace
{
   // when each note starts, for a ball dropped from the top and left to live out its lifetime
   std::vector<double> hitsOverLifetime(float minBounce, bool withLines)
   {
      Fixture f;
      useShortEnvelope(f.processor);
      setParam(f.processor, Params::MIN_BOUNCE, minBounce);
      setParam(f.processor, Params::LIFETIME, 8.f);
      setParam(f.processor, Params::LAUNCH_SPEED, 100.f);
      if (withLines)
         for (auto [x, y] : { std::pair{ 90.0, 110.0 }, { 330.0, 80.0 }, { 220.0, 240.0 }, { 80.0, 300.0 }, { 420.0, 290.0 }, { 560.0, 160.0 }, { 690.0, 290.0 } })
            f.processor.getValueTree().getChildWithName("Sites").appendChild(
                juce::ValueTree("Site").setProperty("x", x, nullptr).setProperty("y", y, nullptr), nullptr);

      juce::AudioBuffer<float> buffer(2, 512);
      std::vector<float> out;
      for (int block = 0; block < static_cast<int>(9.0 * SAMPLE_RATE / 512); block++)
      {
         if (block == 1)
            f.processor.getWorld().dropBall({ 300.f, 30.f });
         juce::MidiBuffer midi;
         f.processor.processBlock(buffer, midi);
         out.insert(out.end(), buffer.getReadPointer(0), buffer.getReadPointer(0) + 512);
      }
      return findOnsets(out);
   }

   // the longest stretch with no notes between from and until
   double longestGapAfter(const std::vector<double> &onsets, double from, double until)
   {
      double longest = 0.0, previous = from;
      for (double t : onsets)
      {
         if (t <= from)
            continue;
         if (t > until)
            break;
         longest = juce::jmax(longest, t - previous);
         previous = t;
      }
      return juce::jmax(longest, until - previous);
   }
}

TEST(KeepBouncingTest, BallsMakeNoiseUntilTheyDisappear)
{
   for (bool withLines : { false, true })
   {
      // with the default min bounce they keep hopping: notes keep coming right up until the ball fades
      // out at 8s, with no long silences once the first big bounces are done
      const auto hopping = hitsOverLifetime(250.f, withLines);
      ASSERT_FALSE(hopping.empty());
      EXPECT_GT(hopping.back(), 7.4) << (withLines ? "lines" : "floor");
      EXPECT_LT(longestGapAfter(hopping, 3.0, 7.5), 1.0) << (withLines ? "lines" : "floor"); // a normal bounce is in the air up to ~0.85s here
   }

   // with it off, a ball in the cells slides along the lines silently for a second or more at a time
   const auto sliding = hitsOverLifetime(0.f, true);
   EXPECT_GT(longestGapAfter(sliding, 3.0, 7.5), 1.2);
}
