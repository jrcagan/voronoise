#include "synth/Sample.h"
#include <cmath>

void SampleData::analyse() {
   onsets = SampleSlicer::detectOnsets(buffer, sampleRate);

   peaks.clear();
   const int length = buffer.getNumSamples();
   for (int start = 0; start < length; start += PEAK_BLOCK_SIZE) {
      const int size = juce::jmin(PEAK_BLOCK_SIZE, length - start);
      float peak = 0.f;
      for (int c = 0; c < buffer.getNumChannels(); c++) {
         peak = juce::jmax(peak, buffer.getMagnitude(c, start, size));
      }
      peaks.push_back(peak);
   }
}

namespace SampleSlicer
{
   namespace
   {
      float getThreshold(const Settings& settings) {
         // sensitivity 1 keeps everything, 0 only keeps the very loudest hits
         return 1.f - juce::jlimit(0.f, 1.f, settings.sensitivity);
      }

      bool isUsableOnset(const SampleData::Onset& onset, juce::Range<int> region, float threshold) {
         return onset.position > region.getStart() && onset.position < region.getEnd() && onset.strength >= threshold;
      }
   }

   juce::StringArray getModeNames() {
      return { "Equal", "Transients" };
   }

   juce::StringArray getPickNames() {
      return { "By note", "Random", "In order" };
   }

   juce::Range<int> getRegion(const SampleData& sample, const Settings& settings) {
      const int length = sample.buffer.getNumSamples();
      const float a = juce::jlimit(0.f, 1.f, juce::jmin(settings.start, settings.end));
      const float b = juce::jlimit(0.f, 1.f, juce::jmax(settings.start, settings.end));

      const int start = juce::jlimit(0, juce::jmax(0, length - 1), static_cast<int>(a * static_cast<float>(length)));
      const int end = juce::jlimit(juce::jmin(length, start + 1), length, static_cast<int>(b * static_cast<float>(length)));
      return { start, end };
   }

   int getNumSlices(const SampleData& sample, const Settings& settings) {
      if (settings.mode == Mode::Equal) {
         return juce::jmax(1, settings.sliceCount);
      }

      const auto region = getRegion(sample, settings);
      const float threshold = getThreshold(settings);

      int count = 1;
      for (const auto& onset : sample.onsets) {
         if (isUsableOnset(onset, region, threshold)) {
            count++;
         }
      }
      return count;
   }

   juce::Range<int> getSlice(const SampleData& sample, const Settings& settings, int index) {
      const auto region = getRegion(sample, settings);
      const int numSlices = getNumSlices(sample, settings);
      index = juce::jlimit(0, numSlices - 1, index);

      if (settings.mode == Mode::Equal) {
         const double sliceLength = static_cast<double>(region.getLength()) / numSlices;
         const int start = region.getStart() + static_cast<int>(sliceLength * index);
         const int end = index == numSlices - 1 ? region.getEnd()
                                                : region.getStart() + static_cast<int>(sliceLength * (index + 1));
         return { start, juce::jmax(start + 1, end) };
      }

      // transients: slice n runs from the nth cut to the next one
      const float threshold = getThreshold(settings);
      int start = region.getStart();
      int sliceNumber = 0;

      for (const auto& onset : sample.onsets) {
         if (! isUsableOnset(onset, region, threshold)) {
            continue;
         }
         if (sliceNumber == index) {
            return { start, onset.position };
         }
         start = onset.position;
         sliceNumber++;
      }

      return { start, juce::jmax(start + 1, region.getEnd()) };
   }

   std::vector<int> getBoundaries(const SampleData& sample, const Settings& settings) {
      std::vector<int> boundaries;
      const int numSlices = getNumSlices(sample, settings);
      for (int i = 0; i < numSlices; i++) {
         boundaries.push_back(getSlice(sample, settings, i).getStart());
      }
      boundaries.push_back(getRegion(sample, settings).getEnd());
      return boundaries;
   }

   std::vector<SampleData::Onset> detectOnsets(const juce::AudioBuffer<float>& buffer, double sampleRate) {
      std::vector<SampleData::Onset> onsets;

      const int length = buffer.getNumSamples();
      const int numChannels = buffer.getNumChannels();
      constexpr int HOP = 256;
      if (length < HOP * 4 || numChannels == 0) {
         return onsets;
      }

      // log energy per hop, mixed down to mono
      std::vector<float> energy;
      for (int start = 0; start + HOP <= length; start += HOP) {
         double sum = 0.0;
         for (int c = 0; c < numChannels; c++) {
            const auto* data = buffer.getReadPointer(c);
            for (int i = start; i < start + HOP; i++) {
               sum += static_cast<double>(data[i]) * data[i];
            }
         }
         energy.push_back(static_cast<float>(std::log10(sum / (HOP * numChannels) + 1e-10)));
      }

      // how much louder each hop is than the few before it
      constexpr int LOOKBACK = 4;
      std::vector<float> rise(energy.size(), 0.f);
      for (size_t i = LOOKBACK; i < energy.size(); i++) {
         float before = 0.f;
         for (size_t k = 1; k <= LOOKBACK; k++) {
            before += energy[i - k];
         }
         before /= LOOKBACK;
         rise[i] = juce::jmax(0.f, energy[i] - before);
      }

      // keep local peaks of the rise, at least 50ms apart, ignoring tiny wobbles
      const int minGapHops = juce::jmax(1, static_cast<int>(0.05 * sampleRate / HOP));
      constexpr float MIN_RISE = 0.3f; // ~3db jump in energy
      int lastHop = -minGapHops;

      for (size_t i = 1; i + 1 < rise.size(); i++) {
         const bool isPeak = rise[i] >= rise[i - 1] && rise[i] > rise[i + 1];
         if (isPeak && rise[i] >= MIN_RISE && static_cast<int>(i) - lastHop >= minGapHops) {
            onsets.push_back({ static_cast<int>(i) * HOP, rise[i] });
            lastHop = static_cast<int>(i);
         }
      }

      // strength relative to the biggest hit
      float strongest = 0.f;
      for (const auto& onset : onsets) {
         strongest = juce::jmax(strongest, onset.strength);
      }
      for (auto& onset : onsets) {
         onset.strength = strongest > 0.f ? onset.strength / strongest : 0.f;
      }

      return onsets;
   }
}
