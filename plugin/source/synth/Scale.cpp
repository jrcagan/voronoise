#include "synth/Scale.h"
#include <vector>

namespace Scale
{
   namespace
   {
      // semitones above the root
      std::vector<int> getIntervals(Type type) {
         switch (type) {
            case Type::Chromatic:       return { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
            case Type::Major:           return { 0, 2, 4, 5, 7, 9, 11 };
            case Type::Minor:           return { 0, 2, 3, 5, 7, 8, 10 };
            case Type::Dorian:          return { 0, 2, 3, 5, 7, 9, 10 };
            case Type::Phrygian:        return { 0, 1, 3, 5, 7, 8, 10 };
            case Type::Lydian:          return { 0, 2, 4, 6, 7, 9, 11 };
            case Type::Mixolydian:      return { 0, 2, 4, 5, 7, 9, 10 };
            case Type::HarmonicMinor:   return { 0, 2, 3, 5, 7, 8, 11 };
            case Type::MajorPentatonic: return { 0, 2, 4, 7, 9 };
            case Type::MinorPentatonic: return { 0, 3, 5, 7, 10 };
            case Type::Blues:           return { 0, 3, 5, 6, 7, 10 };
            case Type::WholeTone:       return { 0, 2, 4, 6, 8, 10 };
            case Type::END:
            default:                    return { 0 };
         }
      }

      std::vector<int> getAllowedNotes(const Settings& settings) {
         std::vector<int> notes;
         const int low = juce::jlimit(0, 127, juce::jmin(settings.lowestNote, settings.highestNote));
         const int high = juce::jlimit(0, 127, juce::jmax(settings.lowestNote, settings.highestNote));

         for (int note = low; note <= high; note++) {
            if (isAllowed(note, settings)) {
               notes.push_back(note);
            }
         }
         return notes;
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
      std::array<bool, 12> mask {};
      for (int interval : getIntervals(type)) {
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

   int chooseByHeight(float amountUp, const Settings& settings) {
      const auto notes = getAllowedNotes(settings);
      if (notes.empty()) {
         return applyOctaveShift(juce::jlimit(0, 127, settings.lowestNote), settings);
      }

      const auto index = juce::jmin(notes.size() - 1,
                                    static_cast<size_t>(juce::jlimit(0.f, 1.f, amountUp) * static_cast<float>(notes.size())));
      return applyOctaveShift(notes[index], settings);
   }

   int chooseRandom(juce::Random& random, const Settings& settings) {
      const auto notes = getAllowedNotes(settings);
      if (notes.empty()) {
         return applyOctaveShift(juce::jlimit(0, 127, settings.lowestNote), settings);
      }

      float total = 0.f;
      for (int note : notes) {
         total += settings.weights[static_cast<size_t>(pitchClass(note))];
      }

      // walk along the weights until we pass the random point
      float point = random.nextFloat() * total;
      for (int note : notes) {
         point -= settings.weights[static_cast<size_t>(pitchClass(note))];
         if (point <= 0.f) {
            return applyOctaveShift(note, settings);
         }
      }

      return applyOctaveShift(notes.back(), settings);
   }

   int snap(int noteNumber, const Settings& settings) {
      const auto notes = getAllowedNotes(settings);
      if (notes.empty()) {
         return applyOctaveShift(noteNumber, settings);
      }

      int closest = notes.front();
      for (int note : notes) {
         // ties go to the lower note since notes are ascending and this uses <
         if (std::abs(note - noteNumber) < std::abs(closest - noteNumber)) {
            closest = note;
         }
      }

      return applyOctaveShift(closest, settings);
   }

   int applyOctaveShift(int noteNumber, const Settings& settings) {
      return juce::jlimit(0, 127, noteNumber + 12 * settings.octaveShift);
   }
}
