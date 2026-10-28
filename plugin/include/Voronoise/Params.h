#pragma once

#include <JuceHeader.h>

// every setting in the settings pane, ids are shared by the processor, editor and tests
namespace Params
{
   // view
   inline constexpr auto LINES = "lines";

   // balls
   inline constexpr auto GRAVITY = "gravity";
   inline constexpr auto BOUNCINESS = "bounciness";
   inline constexpr auto MIN_BOUNCE = "minBounce";
   inline constexpr auto DRAG = "drag";
   inline constexpr auto BALL_SIZE = "ballSize";
   inline constexpr auto SIZE_VARIATION = "sizeVariation";
   inline constexpr auto LIFETIME = "lifetime";
   inline constexpr auto LAUNCH_SPEED = "launchSpeed";
   inline constexpr auto MAX_BALLS = "maxBalls";
   inline constexpr auto PASS_THROUGH = "passThrough";
   inline constexpr auto PASS_THROUGH_PLAYS = "passThroughPlays";
   inline constexpr auto MIDI_DROP_CHANCE = "midiDropChance";

   // notes
   inline constexpr auto SCALE_LOCK = "scaleLock";
   inline constexpr auto SCALE_ROOT = "scaleRoot";
   inline constexpr auto SCALE_TYPE = "scaleType";
   inline constexpr auto LOWEST_NOTE = "lowestNote";
   inline constexpr auto HIGHEST_NOTE = "highestNote";
   inline constexpr auto OCTAVE_SHIFT = "octaveShift";
   inline constexpr auto PITCH_SOURCE = "pitchSource";
   inline constexpr auto SNAP_MIDI = "snapMidi";
   inline juce::String noteWeight(int pitchClass) { return "noteWeight" + juce::String(pitchClass); }

   // amp envelope for held midi / keyboard notes
   inline constexpr auto AMP_ATTACK = "ampAttack";
   inline constexpr auto AMP_DECAY = "ampDecay";
   inline constexpr auto AMP_SUSTAIN = "ampSustain";
   inline constexpr auto AMP_RELEASE = "ampRelease";
   inline constexpr auto KEYS_LEVEL = "keysLevel";
   inline constexpr auto HIT_LENGTH = "hitLength"; // how long a ball hit is held before it releases

   // sound
   inline constexpr auto BALL_SOUND = "ballSound"; // synth or sample for new balls
   inline constexpr auto WT_POSITION = "wtPosition"; // where in the wavetable, 0 - 1

   // random jumps every time a ball hits a line or the border
   inline constexpr auto RND_POSITION = "rndPosition";
   inline constexpr auto RND_PITCH = "rndPitch";
   inline constexpr auto RND_LEVEL = "rndLevel";
   inline constexpr auto RND_PAN = "rndPan";
   inline constexpr auto HIT_DYNAMICS = "hitDynamics"; // how much softer impacts are quieter, 0 = every hit as loud as the key
   inline constexpr auto SAMPLE_START = "sampleStart";
   inline constexpr auto SAMPLE_END = "sampleEnd";
   inline constexpr auto SLICE_MODE = "sliceMode";
   inline constexpr auto SLICE_COUNT = "sliceCount";
   inline constexpr auto SENSITIVITY = "transientSensitivity";
   inline constexpr auto SLICE_PICK = "slicePick";
   inline constexpr auto REVERSE = "sampleReverse";
   inline constexpr auto TRANSPOSE = "sampleTranspose";
   inline constexpr auto FINE_TUNE = "sampleFineTune";
   inline constexpr auto ROOT_NOTE = "sampleRootNote";
   inline constexpr auto TRACK_PITCH = "sampleTrackPitch";
   inline constexpr auto SAMPLE_GAIN = "sampleGain";
   inline constexpr auto FADE_IN = "sampleFadeIn";
   inline constexpr auto FADE_OUT = "sampleFadeOut";

   // lfos, numbered from 0 in code and from 1 on screen
   inline constexpr int NUM_LFOS = 3;
   inline juce::String lfoRate(int lfo)     { return "lfo" + juce::String(lfo + 1) + "Rate"; }
   inline juce::String lfoSync(int lfo)     { return "lfo" + juce::String(lfo + 1) + "Sync"; }
   inline juce::String lfoDivision(int lfo) { return "lfo" + juce::String(lfo + 1) + "Division"; }
   inline juce::String lfoMode(int lfo)     { return "lfo" + juce::String(lfo + 1) + "Mode"; }
   inline juce::String lfoPhase(int lfo)    { return "lfo" + juce::String(lfo + 1) + "Phase"; }
   inline juce::String lfoSmooth(int lfo)   { return "lfo" + juce::String(lfo + 1) + "Smooth"; }

   // free runs forever, retrigger restarts on every midi note, envelope runs once per note
   enum class LfoMode {
      Free,
      Retrigger,
      Envelope
   };

   inline juce::StringArray getLfoModeNames() {
      return { "Free", "Retrig", "Env" };
   }

   // tempo synced lengths, and how many beats each one is
   inline juce::StringArray getLfoDivisionNames() {
      return { "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T", "1/16T" };
   }

   inline double getLfoDivisionBeats(int index) {
      constexpr double BEATS[] = { 16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0 };
      return BEATS[juce::jlimit(0, 10, index)];
   }

   // which lines are drawn and bounced off
   enum class Lines {
      Both,
      VoronoiOnly,
      DelaunayOnly
   };

   inline juce::StringArray getLinesNames() {
      return { "Both", "Voronoi only", "Delaunay only" };
   }
}
