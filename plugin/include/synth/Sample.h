#pragma once

#include <JuceHeader.h>
#include <vector>

// a loaded sample, ref counted so the message thread can hand it to the audio thread safely
struct SampleData : juce::ReferenceCountedObject {
   using Ptr = juce::ReferenceCountedObjectPtr<SampleData>;

   struct Onset {
      int position = 0;     // in samples
      float strength = 0.f; // 0 - 1, 1 = the strongest hit in the file
   };

   juce::AudioBuffer<float> buffer;
   double sampleRate = 44100.0;
   juce::String name;
   juce::File file;

   // worked out once when it's loaded so nothing heavy runs on the audio thread
   std::vector<Onset> onsets;        // sorted by position
   std::vector<float> peaks;         // max abs value per block, for drawing
   static constexpr int PEAK_BLOCK_SIZE = 256;

   // fills in onsets and peaks, call after the buffer is set
   void analyse();
};

namespace SampleSlicer
{
   enum class Mode {
      Equal,     // cut the region into slice count equal pieces
      Transients // cut wherever there's a hit louder than the sensitivity
   };

   // which slice a hit plays
   enum class Pick {
      ByNote,  // note number picks the slice, root note = first slice
      Random,
      InOrder  // round robin
   };

   struct Settings {
      float start = 0.f; // 0 - 1 of the file
      float end = 1.f;
      int sliceCount = 1;
      Mode mode = Mode::Equal;
      float sensitivity = 0.5f; // 0 - 1, higher finds more transients
      Pick pick = Pick::ByNote;
   };

   juce::StringArray getModeNames();
   juce::StringArray getPickNames();

   // start/end in samples, end > start (unless the sample is empty)
   juce::Range<int> getRegion(const SampleData& sample, const Settings& settings);

   // none of these allocate, they're used on the audio thread
   int getNumSlices(const SampleData& sample, const Settings& settings);
   juce::Range<int> getSlice(const SampleData& sample, const Settings& settings, int index);

   // the cut points including region start and end, for drawing (allocates)
   std::vector<int> getBoundaries(const SampleData& sample, const Settings& settings);

   std::vector<SampleData::Onset> detectOnsets(const juce::AudioBuffer<float>& buffer, double sampleRate);
}
