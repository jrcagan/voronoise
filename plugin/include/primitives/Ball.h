#pragma once

#include <JuceHeader.h>
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
      bool passThroughPlays = true;   // whether going through a line still plays a note
      juce::Random* random = nullptr; // needed for pass through, without one it never passes
   };

   juce::Colour color = juce::Colours::orange;

   float radius = 6.f;
   juce::Point<float> velocity; // pixels per second

   int noteNumber = 60; // note it plays when it hits an edge
   float frequency = 0.f; // frequency of its associated note
   float noteVelocity = 100.f; // midi velocity of the note that spawned it
   Waveform sound = Waveform::Sine;

   float timeLeftAlive = 8.f; // seconds
   float hitCooldown = 0.f; // seconds until it's allowed to play again

   // edges (by index) it's currently passing through, so it doesn't bounce halfway through them
   std::vector<size_t> passingEdges;

   // moves the ball dt seconds forward, bouncing it off the walls and any edges it hits.
   // returns how hard it hit an edge or wall (speed into it in pixels per second), 0 if it didn't
   float update(float dt, const juce::Rectangle<float>& bounds, const std::vector<GeoUtils::Edge>& edges,
                const Physics& physics);
   // same thing with the default physics
   float update(float dt, const juce::Rectangle<float>& bounds, const std::vector<GeoUtils::Edge>& edges);

   // true if a hit this hard should make a sound, also starts the cooldown
   bool shouldPlayHit(float impactSpeed);
   float getHitGain(float impactSpeed) const; // 0 - 1

   bool isAlive() const;
   float getAlpha() const; // fades out over the last second alive

   static constexpr float GRAVITY = 900.f;     // defaults, the settings pane can change them
   static constexpr float RESTITUTION = 0.8f;
   static constexpr float FADE_TIME = 1.f;     // seconds

   static constexpr float MIN_HIT_SPEED = 60.f;  // slower than this is rolling/resting, stays quiet
   static constexpr float LOUD_HIT_SPEED = 900.f; // this fast or more is full volume
   static constexpr float HIT_COOLDOWN = 0.04f;  // stops corners from double triggering

private:
   float bounceOffWalls(const juce::Rectangle<float>& bounds, const Physics& physics);
   float bounceOffEdge(const GeoUtils::Edge& edge, size_t index, const Physics& physics);
   float getDistanceTo(const GeoUtils::Edge& edge, juce::Point<float>& closest) const;
   bool isPassing(size_t index) const;
};
