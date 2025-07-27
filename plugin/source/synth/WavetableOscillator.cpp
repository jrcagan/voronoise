#include "synth/WavetableOscillator.h"
#include <cmath>

WavetableOscillator::WavetableOscillator(std::vector<float> waveTable, 
   double sampleRate)
      : waveTable{std::move(waveTable)},
         sampleRate{sampleRate} {}

void WavetableOscillator::setFrequency(float frequency) {
   indexIncrement = frequency * static_cast<float>(waveTable.size())
                     / static_cast<float>(sampleRate);
}

float WavetableOscillator::getSample() {
   const auto sample = interpolateLinearly();
   index += indexIncrement;
   index = std::fmod(index, static_cast<float>(waveTable.size()));
   return sample;
}

float WavetableOscillator::interpolateLinearly() {
   const auto truncatedIdx = static_cast<int>(index);
   const auto nextIdx = (truncatedIdx + 1) % static_cast<int>(waveTable.size());

   const auto nextIdxWeight = index - static_cast<float>(truncatedIdx);
   const auto truncatedIdxWeight = 1.f - nextIdxWeight;

   return truncatedIdxWeight * waveTable[truncatedIdx] + nextIdxWeight * waveTable[nextIdx];
}

void WavetableOscillator::stop() {
   index = 0.f;
   indexIncrement = 0.f;
}

bool WavetableOscillator::isPlaying() {
   return indexIncrement != 0.f;
}