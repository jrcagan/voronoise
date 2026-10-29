#pragma once
#include <JuceHeader.h>
#include "synth/Wavetable.h"
#include <vector>

// plays held midi / keyboard notes through the wavetable with an amp envelope
class WavetableSynth
{
public:
   WavetableSynth();

   void prepareToPlay(double sampleRate);
   void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages);

   // amp envelope and overall level for held notes, safe to call every block
   void setEnvelope(const juce::ADSR::Parameters& parameters);
   void setLevel(float gain);

   // the table to play and where in it (0 - 1), audio thread
   void setWavetable(Wavetable::Ptr newWavetable);
   void setPosition(float newPosition);

   int getNumActiveNotes() const;

private:
   struct Note {
      double phase = 0.0;
      double increment = 0.0; // cycles per sample
      float velocity = 0.f;
      juce::ADSR envelope;
   };

   void handleMidiEvent(const juce::MidiMessage& midiEvent);
   void render(juce::AudioBuffer<float>& buffer, int startSample, int endSample);

   double sampleRate = 44100.0;
   std::vector<Note> notes; // one per midi note number
   juce::ADSR::Parameters envelopeParameters { 0.005f, 0.2f, 0.8f, 0.3f };
   float level = 0.5f;

   Wavetable::Ptr wavetable;
   float position = 0.f;
};
