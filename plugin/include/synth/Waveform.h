#pragma once

#include <JuceHeader.h>

// the sound a ball makes when it hits an edge: the wavetable synth or the loaded sample
enum class Waveform {
   Synth,
   Sample,
   END
};

inline juce::StringArray getWaveformNames() {
   return { "Synth", "Sample" };
}
