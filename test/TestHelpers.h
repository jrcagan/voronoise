#pragma once

#include "Voronoise/PluginProcessor.h"

namespace TestHelpers
{
   // runs the processor like a host would, which is also what moves the balls
   inline void runAudio(VoronoiseAudioProcessor &processor, double seconds, int blockSize = 512)
   {
      juce::AudioBuffer<float> buffer(2, blockSize);
      const int blocks = juce::jmax(1, static_cast<int>(seconds * processor.getSampleRate() / blockSize));
      for (int i = 0; i < blocks; i++)
      {
         juce::MidiBuffer midi;
         processor.processBlock(buffer, midi);
      }
   }

   // ready to run at 48k, the sample rate the tests use
   inline void prepare(VoronoiseAudioProcessor &processor, int blockSize = 512)
   {
      processor.setRateAndBufferSizeDetails(48000.0, blockSize);
      processor.prepareToPlay(48000.0, blockSize);
   }
}
