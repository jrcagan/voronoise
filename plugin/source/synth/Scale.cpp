#include "synth/Scale.h"
#include <array>

namespace Scale
{
   namespace
   {
      // which of the 12 semitones above the root are in each scale (a fixed table, no allocating)
      std::array<bool, 12> getSemitones(Type type) {
         auto from = [](std::initializer_list<int> intervals) {
            std::array<bool, 12> semitones {};
            for (int interval : intervals)
               semitones[static_cast<size_t>(interval)] = true;
            return semitones;
         };

         switch (type) {
            case Type::Chromatic:       return from({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 });
            case Type::Major:           return from({ 0, 2, 4, 5, 7, 9, 11 });
            case Type::Minor:           return from({ 0, 2, 3, 5, 7, 8, 10 });
            case Type::Dorian:          return from({ 0, 2, 3, 5, 7, 9, 10 });
            case Type::Phrygian:        return from({ 0, 1, 3, 5, 7, 8, 10 });
            case Type::Lydian:          return from({ 0, 2, 4, 6, 7, 9, 11 });
            case Type::Mixolydian:      return from({ 0, 2, 4, 5, 7, 9, 10 });
            case Type::HarmonicMinor:   return from({ 0, 2, 3, 5, 7, 8, 11 });
            case Type::MajorPentatonic: return from({ 0, 2, 4, 7, 9 });
            case Type::MinorPentatonic: return from({ 0, 3, 5, 7, 10 });
            case Type::Blues:           return from({ 0, 3, 5, 6, 7, 10 });
            case Type::WholeTone:       return from({ 0, 2, 4, 6, 8, 10 });
            case Type::END:
            default:                    return from({ 0 });
         }
      }

      int pitchClass(int noteNumber) {
         return ((noteNumber % 12) + 12) % 12;
      }
   }

   juce::StringArray getTypeNames() {
      return { "Chromatic", "Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian",
               "Harmonic minor", "Major pentatonic", "Minor pentatonic", "Blues", "Whole tone" };
   }

   juce::StringArray getNoteNames() {
      return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
   }

   juce::String getNoteName(int noteNumber) {
      // c3 = 48, same as the rest of the plugin
      return getNoteNames()[pitchClass(noteNumber)] + juce::String(noteNumber / 12 - 1);
   }

   std::array<bool, 12> getPitchClassMask(Type type, int root) {
      const auto semitones = getSemitones(type);
      std::array<bool, 12> mask {};
      for (int interval = 0; interval < 12; interval++) {
         if (semitones[static_cast<size_t>(interval)])
            mask[static_cast<size_t>(pitchClass(root + interval))] = true;
      }
      return mask;
   }

   bool isAllowed(int noteNumber, const Settings& settings) {
      if (noteNumber < juce::jmin(settings.lowestNote, settings.highestNote)
          || noteNumber > juce::jmax(settings.lowestNote, settings.highestNote)) {
         return false;
      }

      const auto pc = static_cast<size_t>(pitchClass(noteNumber));

      if (settings.lockToScale && ! getPitchClassMask(settings.type, settings.root)[pc]) {
         return false;
      }

      return settings.weights[pc] > 0.f;
   }

   // none of these allocate, they're called on the audio thread when midi drops balls

   int chooseByHeight(float amountUp, const Settings& settings) {
      const int low = juce::jlimit(0, 127, juce::jmin(settings.lowestNote, settings.highestNote));
      const int high = juce::jlimit(0, 127, juce::jmax(settings.lowestNote, settings.highestNote));

      int count = 0;
      for (int note = low; note <= high; note++) {
         count += isAllowed(note, settings) ? 1 : 0;
      }
      if (count == 0) {
         return applyOctaveShift(low, settings);
      }

      const int wanted = juce::jmin(count - 1, static_cast<int>(juce::jlimit(0.f, 1.f, amountUp) * static_cast<float>(count)));
      int seen = 0;
      for (int note = low; note <= high; note++) {
         if (isAllowed(note, settings) && seen++ == wanted) {
            return applyOctaveShift(note, settings);
         }
      }
      return applyOctaveShift(high, settings);
   }

   int chooseRandom(juce::Random& random, const Settings& settings) {
      const int low = juce::jlimit(0, 127, juce::jmin(settings.lowestNote, settings.highestNote));
      const int high = juce::jlimit(0, 127, juce::jmax(settings.lowestNote, settings.highestNote));

      float total = 0.f;
      int last = -1;
      for (int note = low; note <= high; note++) {
         if (isAllowed(note, settings)) {
            total += settings.weights[static_cast<size_t>(pitchClass(note))];
            last = note;
         }
      }
      if (last < 0) {
         return applyOctaveShift(low, settings);
      }

      // walk along the weights until we pass the random point
      float point = random.nextFloat() * total;
      for (int note = low; note <= high; note++) {
         if (! isAllowed(note, settings)) {
            continue;
         }
         point -= settings.weights[static_cast<size_t>(pitchClass(note))];
         if (point <= 0.f) {
            return applyOctaveShift(note, settings);
         }
      }
      return applyOctaveShift(last, settings);
   }

   int snap(int noteNumber, const Settings& settings) {
      const int low = juce::jlimit(0, 127, juce::jmin(settings.lowestNote, settings.highestNote));
      const int high = juce::jlimit(0, 127, juce::jmax(settings.lowestNote, settings.highestNote));

      int closest = -1;
      for (int note = low; note <= high; note++) {
         // ties go to the lower note since this walks upwards and uses <
         if (isAllowed(note, settings) && (closest < 0 || std::abs(note - noteNumber) < std::abs(closest - noteNumber))) {
            closest = note;
         }
      }
      return applyOctaveShift(closest < 0 ? noteNumber : closest, settings);
   }

   int applyOctaveShift(int noteNumber, const Settings& settings) {
      return juce::jlimit(0, 127, noteNumber + 12 * settings.octaveShift);
   }
}
