#pragma once
#include <JuceHeader.h>
#include "synth/WavetableOscillator.h"
#include <vector>

class WavetableSynth
{
public:
   void prepareToPlay(double sampleRate);
   void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages);

private:
   std::vector<float> generateSineWavetable(int length);
   void initializeOscillators();
   void handleMidiEvent(const juce::MidiMessage& midiEvent);
   float midiNoteNumberToFrequency(int midiNoteNumber);
   void render(juce::AudioBuffer<float>& buffer, int startSample, int endSample);

   double sampleRate;
   std::vector<WavetableOscillator> oscillators;
};