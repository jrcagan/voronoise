#include "synth/HitSynth.h"
#include <cmath>

namespace
{
   double noteToFrequency(int noteNumber) {
      return 440.0 * std::pow(2.0, (noteNumber - 69) / 12.0);
   }
}

HitSynth::HitSynth() : wavetable(Wavetable::createPreset(Wavetable::Preset::BasicShapes)) {}

void HitSynth::prepareToPlay(double newSampleRate) {
   sampleRate = newSampleRate;

   for (auto& voice : voices) {
      voice.active = false;
      voice.envelope.setSampleRate(sampleRate);
      voice.envelope.reset();
   }
}

void HitSynth::setSettings(const Settings& newSettings) {
   settings = newSettings;
}

void HitSynth::setSample(SampleData::Ptr newSample) {
   // voices reading the old sample would read past its end, so cut them
   for (auto& voice : voices) {
      if (voice.waveform == Waveform::Sample) {
         voice.active = false;
      }
   }

   sample = newSample;
   nextSlice = 0;
}

void HitSynth::setWavetable(Wavetable::Ptr newWavetable) {
   // tables never shrink under a playing voice, getSample clamps the position to whatever's there
   if (newWavetable != nullptr) {
      wavetable = newWavetable;
   }
}

HitSynth::Voice& HitSynth::findFreeVoice() {
   Voice* oldest = &voices[0];

   for (auto& voice : voices) {
      if (! voice.active) {
         return voice;
      }
      if (voice.age > oldest->age) {
         oldest = &voice;
      }
   }

   // everything's busy, steal the one that's been playing longest
   return *oldest;
}

int HitSynth::pickSlice(int noteNumber) {
   const int numSlices = SampleSlicer::getNumSlices(*sample, settings.slicing);

   switch (settings.slicing.pick) {
      case SampleSlicer::Pick::Random:
         return random.nextInt(numSlices);
      case SampleSlicer::Pick::InOrder: {
         const int slice = nextSlice % numSlices;
         nextSlice = (slice + 1) % numSlices;
         return slice;
      }
      case SampleSlicer::Pick::ByNote:
      default:
         // root note is the first slice, each semitone up is the next one, wrapping around
         return (((noteNumber - settings.rootNote) % numSlices) + numSlices) % numSlices;
   }
}

void HitSynth::trigger(const Hit& hit) {
   if (hit.waveform == Waveform::Sample && (sample == nullptr || sample->buffer.getNumSamples() < 2)) {
      return;
   }

   auto& voice = findFreeVoice();

   voice.active = true;
   voice.waveform = hit.waveform;
   voice.gain = juce::jlimit(0.f, 1.f, hit.gain);
   voice.age = 0;

   // this hit's random jumps
   auto bipolar = [this] { return random.nextFloat() * 2.f - 1.f; };
   voice.positionOffset = settings.randomPosition * bipolar();
   const int pitchJump = settings.randomPitch > 0 ? random.nextInt(settings.randomPitch * 2 + 1) - settings.randomPitch : 0;
   voice.gain *= 1.f - settings.randomLevel * random.nextFloat();
   const float pan = juce::jlimit(-1.f, 1.f, settings.randomPan * bipolar());
   voice.panLeft = juce::jmin(1.f, 1.f - pan); // the middle stays at full level on both sides
   voice.panRight = juce::jmin(1.f, 1.f + pan);

   // same shape as a key press: attack, decay to sustain, then let go. the hit length counts from
   // the top of the attack, so a long attack still gets all the way up instead of fading out half way
   voice.envelope.setParameters(settings.envelope);
   voice.envelope.reset();
   voice.envelope.noteOn();
   voice.released = false;
   if (hit.gateSeconds >= 0.f) {
      // exactly as long as the key was held, the same as the key's own note
      voice.holdSamples = juce::jmax(1, static_cast<int>(hit.gateSeconds * sampleRate));
   } else {
      voice.holdSamples = juce::jmax(1, static_cast<int>((settings.envelope.attack + settings.hitLengthMs * 0.001f) * sampleRate));
   }

   voice.phase = 0.0;
   voice.phaseIncrement = noteToFrequency(hit.noteNumber + pitchJump) / sampleRate;

   if (hit.waveform == Waveform::Sample) {
      const auto slice = SampleSlicer::getSlice(*sample, settings.slicing, pickSlice(hit.noteNumber));
      voice.sliceStart = slice.getStart();
      voice.sliceEnd = juce::jmin(slice.getEnd(), sample->buffer.getNumSamples());

      const double semitones = (settings.trackPitch ? hit.noteNumber - settings.rootNote : 0)
                               + settings.transpose + pitchJump + settings.fineTune / 100.0;
      const double increment = std::pow(2.0, semitones / 12.0) * sample->sampleRate / sampleRate;

      voice.sampleIncrement = settings.reverse ? -increment : increment;
      // reading interpolates between index and index + 1, so backwards has to start one before the last sample
      voice.samplePosition = settings.reverse ? juce::jmax(static_cast<double>(voice.sliceStart), voice.sliceEnd - 2.0)
                                              : static_cast<double>(voice.sliceStart);
      voice.gain *= settings.sampleGain;

      voice.fadeInSamples = juce::jmax(1, static_cast<int>(settings.fadeInMs * 0.001 * sampleRate));
      voice.fadeOutSamples = juce::jmax(1, static_cast<int>(settings.fadeOutMs * 0.001 * sampleRate));
   }
}

float HitSynth::renderWavetable(Voice& voice) {
   // the position follows the knob (and lfos) live, plus this hit's own random jump
   const float position = juce::jlimit(0.f, 1.f, settings.position + voice.positionOffset);
   const float value = wavetable->getSample(voice.phase, voice.phaseIncrement, position);

   voice.phase += voice.phaseIncrement;
   if (voice.phase >= 1.0) {
      voice.phase -= 1.0;
   }

   return value;
}

float HitSynth::getEnvelope(Voice& voice) {
   if (voice.waveform == Waveform::Sample) {
      // fade in from the start of the slice, fade out before its end
      const float fadeIn = juce::jmin(1.f, static_cast<float>(voice.age) / static_cast<float>(voice.fadeInSamples));

      const double sourceLeft = voice.sampleIncrement > 0.0 ? voice.sliceEnd - 1 - voice.samplePosition
                                                             : voice.samplePosition - voice.sliceStart;
      const double outputLeft = sourceLeft / std::abs(voice.sampleIncrement);
      const float fadeOut = juce::jlimit(0.f, 1.f, static_cast<float>(outputLeft / voice.fadeOutSamples));

      return fadeIn * fadeOut;
   }

   if (! voice.released && voice.age >= voice.holdSamples) {
      voice.envelope.noteOff();
      voice.released = true;
   }
   return voice.envelope.getNextSample();
}

// left/right gain for stereo outputs, anything else plays everything in the middle
float HitSynth::panFor(const Voice& voice, int channel, int numChannels) {
   if (numChannels < 2) {
      return 1.f;
   }
   return channel == 0 ? voice.panLeft : channel == 1 ? voice.panRight : 1.f;
}

void HitSynth::render(juce::AudioBuffer<float>& buffer, int startSample, int numSamples) {
   const int numChannels = buffer.getNumChannels();
   if (numChannels == 0) {
      return;
   }

   for (auto& voice : voices) {
      if (! voice.active) {
         continue;
      }

      for (int s = startSample; s < startSample + numSamples; s++) {
         if (voice.waveform == Waveform::Sample) {
            const auto& data = sample->buffer;
            const int index = static_cast<int>(voice.samplePosition);

            if (index < voice.sliceStart || index + 1 >= voice.sliceEnd) {
               voice.active = false;
               break;
            }

            const float level = getEnvelope(voice) * voice.gain * VOICE_GAIN;
            const float frac = static_cast<float>(voice.samplePosition - index);

            for (int c = 0; c < numChannels; c++) {
               // mono samples go to every channel
               const auto* in = data.getReadPointer(c % data.getNumChannels());
               const float value = in[index] + frac * (in[index + 1] - in[index]);
               buffer.addSample(c, s, value * level * panFor(voice, c, numChannels));
            }

            voice.samplePosition += voice.sampleIncrement;
         } else {
            const float level = getEnvelope(voice) * voice.gain * settings.synthLevel;
            const float value = renderWavetable(voice) * level;

            for (int c = 0; c < numChannels; c++) {
               buffer.addSample(c, s, value * panFor(voice, c, numChannels));
            }

            // release finished
            if (! voice.envelope.isActive()) {
               voice.active = false;
               voice.age++;
               break;
            }
         }

         voice.age++;
      }
   }
}

int HitSynth::getNumActiveVoices() const {
   int count = 0;
   for (const auto& voice : voices) {
      if (voice.active) {
         count++;
      }
   }
   return count;
}
