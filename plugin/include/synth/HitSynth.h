#pragma once

#include <JuceHeader.h>
#include <array>
#include "synth/Waveform.h"
#include "synth/Sample.h"

// plays short one-shot notes for ball hits, everything here runs on the audio thread
class HitSynth
{
public:
   struct Hit {
      int noteNumber = 60;
      float gain = 1.f; // 0 - 1
      Waveform waveform = Waveform::Sine;
   };

   using SampleData = ::SampleData;

   struct Settings {
      float pulseWidth = 0.5f; // 0.5 is a square

      SampleSlicer::Settings slicing;
      bool reverse = false;
      int transpose = 0;       // semitones
      float fineTune = 0.f;    // cents
      int rootNote = 60;       // note that plays the sample at its original pitch
      bool trackPitch = true;  // off = every note plays at the original pitch (drums)
      float sampleGain = 1.f;  // linear
      float fadeInMs = 2.f;
      float fadeOutMs = 5.f;
   };

   void prepareToPlay(double sampleRate);
   void setSettings(const Settings& newSettings);
   void trigger(const Hit& hit);
   void setSample(SampleData::Ptr newSample);

   // adds the active voices into the buffer
   void render(juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

   int getNumActiveVoices() const;

   static constexpr int NUM_VOICES = 32;
   static constexpr int SAMPLE_ROOT_NOTE = 60; // default root, samples play at their original pitch on middle c
   static constexpr float VOICE_GAIN = 0.3f;

private:
   struct Voice {
      bool active = false;
      Waveform waveform = Waveform::Sine;
      float gain = 0.f;

      double phase = 0.0;          // 0 - 1, for the oscillators
      double phaseIncrement = 0.0;
      float pulseWidth = 0.5f;

      double samplePosition = 0.0; // in samples of the loaded file
      double sampleIncrement = 0.0; // negative when reversed
      int sliceStart = 0;
      int sliceEnd = 0;
      int fadeInSamples = 1;
      int fadeOutSamples = 1;

      int age = 0;                 // samples since it started, for the attack and voice stealing
      float envelope = 0.f;
   };

   Voice& findFreeVoice();
   float renderOscillator(Voice& voice);
   float getEnvelope(Voice& voice);
   int pickSlice(int noteNumber);

   std::array<Voice, NUM_VOICES> voices;
   SampleData::Ptr sample;
   Settings settings;

   juce::Random random;
   int nextSlice = 0; // for in order slicing

   double sampleRate = 44100.0;
   int attackSamples = 1;
   float decayMultiplier = 0.9999f;
};
