#pragma once

#include <JuceHeader.h>
#include <array>

// picks which notes balls are allowed to play
namespace Scale
{
   enum class Type {
      Chromatic,
      Major,
      Minor,
      Dorian,
      Phrygian,
      Lydian,
      Mixolydian,
      HarmonicMinor,
      MajorPentatonic,
      MinorPentatonic,
      Blues,
      WholeTone,
      END
   };

   // how a hand dropped ball gets its note
   enum class PitchSource {
      Height, // higher on screen = higher note
      Random  // weighted by the note probabilities
   };

   struct Settings {
      bool lockToScale = true;
      int root = 0; // 0 = c, 11 = b
      Type type = Type::MinorPentatonic;
      int lowestNote = 48;  // c3
      int highestNote = 83; // b5
      int octaveShift = 0;  // applied to every ball after the note is picked
      std::array<float, 12> weights { 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f }; // per pitch class, c first
   };

   juce::StringArray getTypeNames();
   juce::StringArray getNoteNames(); // "C", "C#", ...
   juce::String getNoteName(int noteNumber); // "C3"

   // which pitch classes (c = 0) are in the scale, ignoring the probabilities
   std::array<bool, 12> getPitchClassMask(Type type, int root);

   // in the scale (if locked), in range, and probability above 0
   bool isAllowed(int noteNumber, const Settings& settings);

   // 0 = bottom of the screen, 1 = top
   int chooseByHeight(float amountUp, const Settings& settings);
   int chooseRandom(juce::Random& random, const Settings& settings);

   // moves a note to the closest allowed one (used for midi balls)
   int snap(int noteNumber, const Settings& settings);

   int applyOctaveShift(int noteNumber, const Settings& settings);
}
