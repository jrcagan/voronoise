#include "synth/WavetableSynth.h"

WavetableSynth::WavetableSynth()
    : wavetable(Wavetable::createPreset(Wavetable::Preset::BasicShapes)) {}

void WavetableSynth::prepareToPlay(double newSampleRate) {
   sampleRate = newSampleRate;

   constexpr auto NOTE_COUNT = 128;
   notes.assign(NOTE_COUNT, Note());
   for (auto& note : notes) {
      note.envelope.setSampleRate(sampleRate);
      note.envelope.setParameters(envelopeParameters);
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
   for (auto& note : notes) {
      note.envelope.setParameters(envelopeParameters);
   }
}

void WavetableSynth::setLevel(float gain) {
   level = gain;
}

void WavetableSynth::setWavetable(Wavetable::Ptr newWavetable) {
   if (newWavetable != nullptr) {
      wavetable = newWavetable;
   }
}

void WavetableSynth::setPosition(float newPosition) {
   position = juce::jlimit(0.f, 1.f, newPosition);
}

int WavetableSynth::getNumActiveNotes() const {
   int count = 0;
   for (const auto& note : notes) {
      if (note.envelope.isActive()) {
         count++;
      }
   }
   return count;
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
   if (buffer.getNumChannels() == 0) {
      return;
   }

   auto* firstChannel = buffer.getWritePointer(0);

   for (auto& note : notes) {
      if (! note.envelope.isActive()) {
         continue;
      }

      const float gain = note.velocity * level;
      for (int s = startSample; s < endSample; s++) {
         firstChannel[s] += wavetable->getSample(note.phase, note.increment, position) * note.envelope.getNextSample() * gain;
         note.phase += note.increment;
         if (note.phase >= 1.0) {
            note.phase -= 1.0;
         }
      }
   }

   for (int c = 1; c < buffer.getNumChannels(); c++) {
      std::copy(firstChannel + startSample, firstChannel + endSample,
         buffer.getWritePointer(c) + startSample);
   }
}

void WavetableSynth::handleMidiEvent(const juce::MidiMessage& midiEvent) {
   // notes only exist after prepareToPlay
   if (notes.empty()) {
      return;
   }

   if (midiEvent.isNoteOn()) {
      auto& note = notes[static_cast<size_t>(midiEvent.getNoteNumber())];
      // restart from zero if it wasn't already sounding, so every note starts on the same part of the wave
      if (! note.envelope.isActive()) {
         note.phase = 0.0;
      }
      note.increment = juce::MidiMessage::getMidiNoteInHertz(midiEvent.getNoteNumber()) / sampleRate;
      note.velocity = midiEvent.getFloatVelocity();
      note.envelope.noteOn();

   } else if (midiEvent.isNoteOff()) {
      // keeps playing through the release
      notes[static_cast<size_t>(midiEvent.getNoteNumber())].envelope.noteOff();

   } else if (midiEvent.isAllNotesOff()) {
      for (auto& note : notes) {
         note.envelope.reset();
      }
   }
}
