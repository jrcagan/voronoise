#include "synth/HitSynth.h"
#include <cmath>

namespace
{
   // smooths the jump in saw/pulse waves so they don't alias as badly
   float polyBlep(double t, double dt) {
      if (t < dt) {
         t /= dt;
         return static_cast<float>(t + t - t * t - 1.0);
      }
      if (t > 1.0 - dt) {
         t = (t - 1.0) / dt;
         return static_cast<float>(t * t + t + t + 1.0);
      }
      return 0.f;
   }

   double noteToFrequency(int noteNumber) {
      return 440.0 * std::pow(2.0, (noteNumber - 69) / 12.0);
   }
}

void HitSynth::prepareToPlay(double newSampleRate) {
   sampleRate = newSampleRate;

   constexpr double ATTACK_SECONDS = 0.003;
   constexpr double DECAY_SECONDS = 0.35; // time to fall to ~37%

   attackSamples = juce::jmax(1, static_cast<int>(ATTACK_SECONDS * sampleRate));
   decayMultiplier = static_cast<float>(std::exp(-1.0 / (DECAY_SECONDS * sampleRate)));

   for (auto& voice : voices) {
      voice.active = false;
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
   voice.envelope = 1.f;

   voice.phase = 0.0;
   voice.phaseIncrement = noteToFrequency(hit.noteNumber) / sampleRate;
   voice.pulseWidth = juce::jlimit(0.01f, 0.99f, settings.pulseWidth);

   if (hit.waveform == Waveform::Sample) {
      const auto slice = SampleSlicer::getSlice(*sample, settings.slicing, pickSlice(hit.noteNumber));
      voice.sliceStart = slice.getStart();
      voice.sliceEnd = juce::jmin(slice.getEnd(), sample->buffer.getNumSamples());

      const double semitones = (settings.trackPitch ? hit.noteNumber - settings.rootNote : 0)
                               + settings.transpose + settings.fineTune / 100.0;
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

float HitSynth::renderOscillator(Voice& voice) {
   const auto t = voice.phase;
   const auto dt = voice.phaseIncrement;
   float value = 0.f;

   switch (voice.waveform) {
      case Waveform::Sine:
         value = static_cast<float>(std::sin(juce::MathConstants<double>::twoPi * t));
         break;
      case Waveform::Saw:
         value = static_cast<float>(2.0 * t - 1.0) - polyBlep(t, dt);
         break;
      case Waveform::Triangle:
         value = static_cast<float>(4.0 * std::abs(t - 0.5) - 1.0);
         break;
      case Waveform::Pulse:
         value = t < voice.pulseWidth ? 1.f : -1.f;
         value += polyBlep(t, dt);
         value -= polyBlep(std::fmod(t + 1.0 - voice.pulseWidth, 1.0), dt);
         // keep it centred on zero when the width isn't 50%
         value -= 2.f * voice.pulseWidth - 1.f;
         break;
      case Waveform::Sample: // samples don't go through here
      case Waveform::END:
         break;
   }

   voice.phase += dt;
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

   // short linear attack so the note doesn't click, then decay
   const float attack = juce::jmin(1.f, static_cast<float>(voice.age) / static_cast<float>(attackSamples));
   voice.envelope *= decayMultiplier;
   return attack * voice.envelope;
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
               buffer.addSample(c, s, value * level);
            }

            voice.samplePosition += voice.sampleIncrement;
         } else {
            const float level = getEnvelope(voice) * voice.gain * VOICE_GAIN;
            const float value = renderOscillator(voice) * level;

            for (int c = 0; c < numChannels; c++) {
               buffer.addSample(c, s, value);
            }

            if (voice.envelope < 1e-4f) {
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
