#pragma once

#include <JuceHeader.h>
#include <array>
#include <vector>
#include "geometry/Utils.h"
#include "synth/Waveform.h"

class Ball : public juce::Point<float> {

public:
   // physics settings from the settings pane
   struct Physics {
      float gravity = 900.f;          // pixels per second^2
      float restitution = 0.8f;       // how much speed is kept after a bounce
      float drag = 0.f;               // air resistance, 1 = loses ~63% of its speed per second
      float passThroughChance = 0.f;  // 0 - 1, chance it goes through a line instead of bouncing
      float minBounce = 0.f;          // anything it can land on sends it back up at least this fast (px/s),
                                      // so it keeps hopping instead of settling or sliding along a line
      bool passThroughPlays = true;   // whether going through a line still plays a note
      juce::Random* random = nullptr; // needed for pass through, without one it never passes
   };

   juce::Colour color = juce::Colours::orange;

   float radius = 6.f;
   juce::Point<float> velocity; // pixels per second

   int noteNumber = 60; // note it plays when it hits an edge
   float frequency = 0.f; // frequency of its associated note
   float noteVelocity = 100.f; // midi velocity of the note that spawned it
   Waveform sound = Waveform::Synth;

   // the key press that made it: it waits where it spawned while the key is down, drops when it's let
   // go, and every hit is then held for exactly as long as the key was. clicked balls have neither
   juce::uint32 keyPress = 0;  // the press it's waiting on, 0 once it's let go (or for clicked balls)
   float gateSeconds = -1.f;   // how long the key was held, -1 for clicked balls (they use the hit length knob)
   bool isWaiting() const { return keyPress != 0; }
   juce::int64 dropAt = 0;     // the sample its key was let go on, it doesn't move before then

   float timeLeftAlive = 8.f; // seconds
   float hitCooldown = 0.f; // seconds until it's allowed to play again

   // edges (by index) it's currently passing through, so it doesn't bounce halfway through them.
   // a fixed size array because this runs on the audio thread, which mustn't allocate
   static constexpr int MAX_PASSING = 8;
   std::array<int, MAX_PASSING> passingEdges {};
   int numPassing = 0;
   void clearPassing() { numPassing = 0; }

   // what one physics step bumped into: how hard, and how long before the end of the step it touched,
   // so the note can start on the exact sample the ball reached the line
   struct Contact {
      float speed = 0.f;      // pixels per second into whatever it hit, 0 if nothing
      float secondsAgo = 0.f;
   };

   // one small physics step (the world uses 1/240s)
   Contact step(float h, const juce::Rectangle<float>& bounds, const std::vector<GeoUtils::Edge>& edges, const Physics& physics);

   // moves the ball dt seconds forward, bouncing it off the walls and any edges it hits.
   // returns how hard it hit an edge or wall (speed into it in pixels per second), 0 if it didn't
   float update(float dt, const juce::Rectangle<float>& bounds, const std::vector<GeoUtils::Edge>& edges,
                const Physics& physics);
   // same thing with the default physics
   float update(float dt, const juce::Rectangle<float>& bounds, const std::vector<GeoUtils::Edge>& edges);

   // true if a hit this hard should make a sound, also starts the cooldown
   bool shouldPlayHit(float impactSpeed);
   // 0 - 1: the note's velocity, made quieter for soft impacts by the dynamics amount
   // (1 = all the way down to silent for the slowest hits, 0 = every hit at the note's velocity)
   float getHitGain(float impactSpeed, float dynamics = 1.f) const;

   bool isAlive() const;
   float getAlpha() const; // fades out over the last second alive

   static constexpr float GRAVITY = 900.f;     // defaults, the settings pane can change them
   static constexpr float RESTITUTION = 0.8f;
   static constexpr float FADE_TIME = 1.f;     // seconds

   static constexpr float MIN_HIT_SPEED = 60.f;  // slower than this is rolling/resting, stays quiet
   static constexpr float LOUD_HIT_SPEED = 900.f; // this fast or more is full volume
   static constexpr float HIT_COOLDOWN = 0.04f;  // stops corners from double triggering

private:
   Contact bounceOffWalls(const juce::Rectangle<float>& bounds, const Physics& physics, float h);
   Contact bounceOffEdge(const GeoUtils::Edge& edge, int index, const Physics& physics, float h);
   float getDistanceTo(const GeoUtils::Edge& edge, juce::Point<float>& closest) const;
   bool isPassing(int index) const;
};
