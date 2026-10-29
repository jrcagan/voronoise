#pragma once

#include <JuceHeader.h>
#include <array>
#include "synth/Waveform.h"
#include "synth/Sample.h"
#include "synth/Wavetable.h"

// plays short one-shot notes for ball hits, everything here runs on the audio thread
class HitSynth
{
public:
   struct Hit {
      int noteNumber = 60;
      float gain = 1.f; // 0 - 1
      Waveform waveform = Waveform::Synth;

      // how long to hold the note: the key's length if it came from a key press, otherwise the
      // hit length knob (-1)
      float gateSeconds = -1.f;
   };

   using SampleData = ::SampleData;

   struct Settings {
      float position = 0.f; // where in the wavetable, 0 - 1
      float synthLevel = 1.f; // the keys' level knob, so a full strength hit matches the key

      // every hit can jump around a bit: up to this far from the position (0 - 1),
      // this many semitones up or down, this much quieter (0 - 1) and this far left/right (0 - 1)
      float randomPosition = 0.f;
      int randomPitch = 0;
      float randomLevel = 0.f;
      float randomPan = 0.f;

      SampleSlicer::Settings slicing;
      bool reverse = false;
      int transpose = 0;       // semitones
      float fineTune = 0.f;    // cents
      int rootNote = 60;       // note that plays the sample at its original pitch
      bool trackPitch = true;  // off = every note plays at the original pitch (drums)
      float sampleGain = 1.f;  // linear
      float fadeInMs = 2.f;
      float fadeOutMs = 5.f;

      // oscillator hits use the same amp envelope as the keys. a hit has no note off,
      // so it's held for the attack plus hitLengthMs and then released
      juce::ADSR::Parameters envelope { 0.005f, 0.2f, 0.8f, 0.3f };
      float hitLengthMs = 200.f;
   };

   void prepareToPlay(double sampleRate);
   void setSettings(const Settings& newSettings);
   void trigger(const Hit& hit);
   void setSample(SampleData::Ptr newSample);
   void setWavetable(Wavetable::Ptr newWavetable);


   // adds the active voices into the buffer
   void render(juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

   int getNumActiveVoices() const;

   HitSynth();

   static constexpr int NUM_VOICES = 32;
   static constexpr int SAMPLE_ROOT_NOTE = 60; // default root, samples play at their original pitch on middle c
   static constexpr float VOICE_GAIN = 0.3f; // sample hits, which have their own gain knob on top

private:
   struct Voice {
      bool active = false;
      Waveform waveform = Waveform::Synth;
      float gain = 0.f;
      float panLeft = 1.f, panRight = 1.f;

      double phase = 0.0;          // 0 - 1, for the wavetable
      double phaseIncrement = 0.0;
      float positionOffset = 0.f;  // this hit's random jump in the wavetable

      double samplePosition = 0.0; // in samples of the loaded file
      double sampleIncrement = 0.0; // negative when reversed
      int sliceStart = 0;
      int sliceEnd = 0;
      int fadeInSamples = 1;
      int fadeOutSamples = 1;

      int age = 0;                 // samples since it started, for the attack and voice stealing
      juce::ADSR envelope;         // oscillators only
      int holdSamples = 0;         // when to let go of the envelope
      bool released = false;
   };

   Voice& findFreeVoice();
   float renderWavetable(Voice& voice);
   static float panFor(const Voice& voice, int channel, int numChannels);
   float getEnvelope(Voice& voice);
   int pickSlice(int noteNumber);

   std::array<Voice, NUM_VOICES> voices;
   SampleData::Ptr sample;
   Wavetable::Ptr wavetable;
   Settings settings;

   juce::Random random;
   int nextSlice = 0; // for in order slicing

   double sampleRate = 44100.0;

};
