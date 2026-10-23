#pragma once

#include <JuceHeader.h>

// the sound a ball makes when it hits an edge
enum class Waveform {
   Sine,
   Saw,
   Triangle,
   Pulse,
   Sample,
   END
};

inline juce::StringArray getWaveformNames() {
   return { "Sine", "Saw", "Triangle", "Pulse", "Sample" };
}
