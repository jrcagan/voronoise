#include "synth/WavetableSynth.h"

std::vector<float> WavetableSynth::generateSineWavetable(int length) {
   std::vector<float> sineWaveTable(length);

   const auto PI = std::atanf(1.f) * 4;

   for(int i = 0; i < length; i++) {
      sineWaveTable[i] = std::sinf(2 * PI * static_cast<float>(i) / static_cast<float>(length));
   }

   return sineWaveTable;
}

void WavetableSynth::initializeOscillators() {
   constexpr auto OSCILLATORS_COUNT = 128;

   const auto waveTable = generateSineWavetable(64);

   oscillators.clear();
   envelopes.assign(OSCILLATORS_COUNT, juce::ADSR());
   velocities.assign(OSCILLATORS_COUNT, 0.f);
   for(int i = 0; i < OSCILLATORS_COUNT; i++) {
      oscillators.emplace_back(waveTable, sampleRate);
      envelopes[static_cast<size_t>(i)].setSampleRate(sampleRate);
      envelopes[static_cast<size_t>(i)].setParameters(envelopeParameters);
   }
}

void WavetableSynth::setEnvelope(const juce::ADSR::Parameters& parameters) {
   // only touch the envelopes when something changed, it recalculates their rates
   const auto& p = envelopeParameters;
   if (juce::approximatelyEqual(p.attack, parameters.attack) && juce::approximatelyEqual(p.decay, parameters.decay)
       && juce::approximatelyEqual(p.sustain, parameters.sustain) && juce::approximatelyEqual(p.release, parameters.release)) {
      return;
   }

   envelopeParameters = parameters;
   for (auto& envelope : envelopes) {
      envelope.setParameters(envelopeParameters);
   }
}

void WavetableSynth::setLevel(float gain) {
   level = gain;
}

int WavetableSynth::getNumActiveNotes() const {
   int count = 0;
   for (const auto& envelope : envelopes) {
      if (envelope.isActive()) {
         count++;
      }
   }
   return count;
}

void WavetableSynth::prepareToPlay(double newSampleRate) {
   sampleRate = newSampleRate;

   initializeOscillators();
}

void WavetableSynth::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
   auto currentSample = 0;

   for (const auto midiMessage : midiMessages) {
      const auto midiEvent = midiMessage.getMessage();
      const auto midiEventSample = static_cast<int>(midiEvent.getTimeStamp());

      render(buffer, currentSample, midiEventSample);
      handleMidiEvent(midiEvent);

      currentSample = midiEventSample;
   }

   render(buffer, currentSample, buffer.getNumSamples());
}

void WavetableSynth::render(juce::AudioBuffer<float>& buffer, int startSample, int endSample) {
   auto* firstChannel = buffer.getWritePointer(0);

   for (size_t i = 0; i < oscillators.size(); i++) {
      auto& oscillator = oscillators[i];
      auto& envelope = envelopes[i];
      if (! oscillator.isPlaying()) {
         continue;
      }

      const float gain = velocities[i] * level;
      for (int s = startSample; s < endSample; s++) {
         firstChannel[s] += oscillator.getSample() * envelope.getNextSample() * gain;
      }

      // release finished, the note's done
      if (! envelope.isActive()) {
         oscillator.stop();
      }
   }

   for (int c = 1; c < buffer.getNumChannels(); c++) {
      std::copy(firstChannel + startSample, firstChannel + endSample, 
         buffer.getWritePointer(c) + startSample);
   }
}

void WavetableSynth::handleMidiEvent(const juce::MidiMessage& midiEvent) {
   // oscillators only exist after prepareToPlay
   if (oscillators.empty()) {
      return;
   }

   if (midiEvent.isNoteOn()) {
      const auto oscillatorId = midiEvent.getNoteNumber();
      const auto frequency = midiNoteNumberToFrequency(oscillatorId);
      oscillators[oscillatorId].setFrequency(frequency);
      velocities[static_cast<size_t>(oscillatorId)] = midiEvent.getFloatVelocity();
      envelopes[static_cast<size_t>(oscillatorId)].noteOn();

   } else if (midiEvent.isNoteOff()) {
      // keeps playing through the release, render stops it once that's done
      const auto oscillatorId = midiEvent.getNoteNumber();
      envelopes[static_cast<size_t>(oscillatorId)].noteOff();

   } else if (midiEvent.isAllNotesOff()) {
      for (size_t i = 0; i < oscillators.size(); i++) {
         oscillators[i].stop();
         envelopes[i].reset();
      }
   }

}

float WavetableSynth::midiNoteNumberToFrequency(int midiNoteNumber) {
   constexpr auto A4_FREQEUNCY = 440.f;
   constexpr auto A4_NOTE_NUMBER = 69.f;
   constexpr auto SEMITONES_IN_AN_OCTAVE = 12.f;

   return A4_FREQEUNCY * std::powf(2.f, (midiNoteNumber - A4_NOTE_NUMBER) / SEMITONES_IN_AN_OCTAVE);
}