#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

// one lfo's shape: points joined by bendable curves, like serum
class LfoShape
{
public:
   struct Point {
      float x = 0.f;     // 0 - 1 across one cycle
      float y = 0.f;     // 0 - 1, 1 is the top
      float curve = 0.f; // -1 - 1, bends the segment from this point to the next one
   };

   enum class Preset {
      Sine,
      Triangle,
      SawUp,
      SawDown,
      Square,
      Steps,
      Flat,
      END
   };
   static juce::StringArray getPresetNames();

   LfoShape(); // starts as a sine

   void setPreset(Preset preset, juce::Random* random = nullptr);

   const std::vector<Point>& getPoints() const { return points; }
   void setPoints(std::vector<Point> newPoints); // sorts and clamps, ends stay at x = 0 and x = 1

   int addPoint(float x, float y); // returns the new point's index
   void removePoint(int index);    // the two ends can't be removed
   void movePoint(int index, float x, float y); // x stays between its neighbours, the ends can only move up and down
   void setCurve(int index, float curve);

   // 0 - 1 for a phase of 0 - 1
   float evaluate(float phase) const;

   // shapes t (0 - 1) along a bent segment, curve > 0 starts slow, < 0 starts fast
   static float applyCurve(float t, float curve);

   juce::ValueTree toValueTree() const;
   void fromValueTree(const juce::ValueTree& tree);

   bool operator==(const LfoShape& other) const;

private:
   std::vector<Point> points;
};

// the shape baked into a table, written on the message thread and read on the audio thread.
// it's all atomics so a half written table just plays a mix of old and new for one block
struct LfoTable
{
   static constexpr int SIZE = 1024;

   void fill(const LfoShape& shape);
   float lookup(float phase) const;

private:
   std::array<std::atomic<float>, SIZE + 1> values {};
};
