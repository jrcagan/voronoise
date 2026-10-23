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
   inline constexpr auto DRAG = "drag";
   inline constexpr auto BALL_SIZE = "ballSize";
   inline constexpr auto SIZE_VARIATION = "sizeVariation";
   inline constexpr auto LIFETIME = "lifetime";
   inline constexpr auto LAUNCH_SPEED = "launchSpeed";
   inline constexpr auto MAX_BALLS = "maxBalls";
   inline constexpr auto PASS_THROUGH = "passThrough";
   inline constexpr auto PASS_THROUGH_PLAYS = "passThroughPlays";

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

   // sound
   inline constexpr auto PULSE_WIDTH = "pulseWidth";
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
