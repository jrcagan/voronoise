#pragma once
#include <JuceHeader.h>
#include "synth/WavetableOscillator.h"
#include <vector>

class WavetableSynth
{
public:
   void prepareToPlay(double sampleRate);
   void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages);

   // amp envelope and overall level for held notes, safe to call every block
   void setEnvelope(const juce::ADSR::Parameters& parameters);
   void setLevel(float gain);

   int getNumActiveNotes() const;

private:
   std::vector<float> generateSineWavetable(int length);
   void initializeOscillators();
   void handleMidiEvent(const juce::MidiMessage& midiEvent);
   float midiNoteNumberToFrequency(int midiNoteNumber);
   void render(juce::AudioBuffer<float>& buffer, int startSample, int endSample);

   double sampleRate;
   std::vector<WavetableOscillator> oscillators;
   std::vector<juce::ADSR> envelopes;  // one per note, same index as the oscillators
   std::vector<float> velocities;      // 0 - 1
   juce::ADSR::Parameters envelopeParameters { 0.005f, 0.2f, 0.8f, 0.3f };
   float level = 0.5f;
};