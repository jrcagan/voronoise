#pragma once

#include <JuceHeader.h>
#include <array>
#include <memory>
#include <vector>

// a stack of single cycle frames you can morph through. every frame is kept at several levels of
// detail with the high harmonics removed, so high notes don't alias. read only once it's built,
// which is what lets the audio thread use it while the ui makes a new one
class Wavetable : public juce::ReferenceCountedObject
{
public:
   using Ptr = juce::ReferenceCountedObjectPtr<Wavetable>;

   static constexpr int FRAME_SIZE = 2048;
   static constexpr int MAX_FRAMES = 64;
   static constexpr int NUM_LEVELS = 11; // 1024 harmonics, 512, ... down to 1

   enum class Preset {
      BasicShapes, // sine, triangle, saw, square
      Sine,
      Saw,
      Square,
      PulseSweep,  // square narrowing to a thin pulse
      HarmonicSweep,
      Random,
      END
   };
   static juce::StringArray getPresetNames();
   static Ptr createPreset(Preset preset, juce::Random* random = nullptr);

   // each frame is FRAME_SIZE samples, anything else gets resampled to fit
   Wavetable(const std::vector<std::vector<float>>& rawFrames, const juce::String& name);

   // copy of another table with one frame swapped out, the other frames are shared not rebuilt
   Wavetable(const Wavetable& base, int frameIndex, const std::vector<float>& newFrame);

   // cuts audio into frames, spread evenly through the file (up to MAX_FRAMES)
   static Ptr fromAudio(const juce::AudioBuffer<float>& audio, const juce::String& name);

   int getNumFrames() const { return static_cast<int>(frames.size()); }
   const std::vector<float>& getFrame(int index) const;
   std::vector<std::vector<float>> getFrames() const;
   const juce::String& getName() const { return name; }

   // phase 0 - 1, increment = cycles per sample (picks how many harmonics are safe), position 0 - 1
   float getSample(double phase, double increment, float position) const;

   // the waveform you'd hear at a position, for drawing
   std::vector<float> getFrameAt(float position, int numPoints) const;

   // saving with the project
   juce::MemoryBlock toMemoryBlock() const;
   static Ptr fromMemoryBlock(const juce::MemoryBlock& block, const juce::String& name);

   static std::vector<float> resample(const std::vector<float>& input, int size);

private:
   struct Frame {
      std::vector<float> raw;                            // what was drawn/loaded, FRAME_SIZE long
      std::array<std::vector<float>, NUM_LEVELS> levels; // band limited copies, FRAME_SIZE + 1 (wraps)
   };
   static std::shared_ptr<const Frame> buildFrame(const std::vector<float>& raw);
   static int pickLevel(double increment);

   std::vector<std::shared_ptr<const Frame>> frames;
   juce::String name;
};
