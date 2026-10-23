#pragma once

#include <JuceHeader.h>
#include <vector>
#include "geometry/Utils.h"

class Ball : public juce::Point<float> {

public:
   juce::Colour color = juce::Colours::orange;

   float radius = 6.f;
   juce::Point<float> velocity; // pixels per second

   float frequency = 0.f; // frequency of its associated note, 0 if it was dropped by hand
   float noteVelocity = 0.f; // midi velocity of the note that spawned it

   float timeLeftAlive = 8.f; // seconds

   // moves the ball dt seconds forward, bouncing it off the walls and any edges it hits
   void update(float dt, const juce::Rectangle<float>& bounds, const std::vector<GeoUtils::Edge>& edges);

   bool isAlive() const;
   float getAlpha() const; // fades out over the last second alive

   static constexpr float GRAVITY = 900.f;     // pixels per second^2
   static constexpr float RESTITUTION = 0.8f;  // how much speed is kept after a bounce
   static constexpr float FADE_TIME = 1.f;     // seconds

private:
   void bounceOffWalls(const juce::Rectangle<float>& bounds);
   void bounceOffEdge(const GeoUtils::Edge& edge);
};
