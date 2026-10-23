#include "primitives/Ball.h"
#include <algorithm>
#include <cmath>

float Ball::update(float dt, const juce::Rectangle<float>& bounds, const std::vector<GeoUtils::Edge>& edges,
                   const Physics& physics) {
   timeLeftAlive -= dt;
   hitCooldown -= dt;

   // split the frame into smaller steps so fast balls don't skip through edges
   constexpr int SUBSTEPS = 4;
   const float h = dt / static_cast<float>(SUBSTEPS);
   const float dragMultiplier = std::exp(-physics.drag * h);

   float hardestHit = 0.f;

   for (int step = 0; step < SUBSTEPS; step++) {
      velocity.y += physics.gravity * h;
      velocity *= dragMultiplier;
      x += velocity.x * h;
      y += velocity.y * h;

      for (size_t i = 0; i < edges.size(); i++) {
         hardestHit = juce::jmax(hardestHit, bounceOffEdge(edges[i], i, physics));
      }

      hardestHit = juce::jmax(hardestHit, bounceOffWalls(bounds, physics));

      // stop tracking edges it's made it all the way through
      passingEdges.erase(std::remove_if(passingEdges.begin(), passingEdges.end(), [&](size_t index) {
         juce::Point<float> closest;
         return index >= edges.size() || getDistanceTo(edges[index], closest) >= radius;
      }), passingEdges.end());
   }

   return hardestHit;
}

float Ball::update(float dt, const juce::Rectangle<float>& bounds, const std::vector<GeoUtils::Edge>& edges) {
   return update(dt, bounds, edges, Physics {});
}

bool Ball::shouldPlayHit(float impactSpeed) {
   if (impactSpeed < MIN_HIT_SPEED || hitCooldown > 0.f) {
      return false;
   }

   hitCooldown = HIT_COOLDOWN;
   return true;
}

float Ball::getHitGain(float impactSpeed) const {
   const float hitAmount = juce::jlimit(0.f, 1.f, impactSpeed / LOUD_HIT_SPEED);
   return hitAmount * juce::jlimit(0.f, 1.f, noteVelocity / 127.f);
}

bool Ball::isAlive() const {
   return timeLeftAlive > 0.f;
}

float Ball::getAlpha() const {
   return juce::jlimit(0.f, 1.f, timeLeftAlive / FADE_TIME);
}

bool Ball::isPassing(size_t index) const {
   return std::find(passingEdges.begin(), passingEdges.end(), index) != passingEdges.end();
}

// returns the speed it hit a wall at, 0 if it didn't
float Ball::bounceOffWalls(const juce::Rectangle<float>& bounds, const Physics& physics) {
   float hardestHit = 0.f;

   if (x - radius < bounds.getX()) {
      x = bounds.getX() + radius;
      hardestHit = juce::jmax(hardestHit, -velocity.x);
      velocity.x = std::abs(velocity.x) * physics.restitution;
   } else if (x + radius > bounds.getRight()) {
      x = bounds.getRight() - radius;
      hardestHit = juce::jmax(hardestHit, velocity.x);
      velocity.x = -std::abs(velocity.x) * physics.restitution;
   }

   if (y - radius < bounds.getY()) {
      y = bounds.getY() + radius;
      hardestHit = juce::jmax(hardestHit, -velocity.y);
      velocity.y = std::abs(velocity.y) * physics.restitution;
   } else if (y + radius > bounds.getBottom()) {
      y = bounds.getBottom() - radius;
      hardestHit = juce::jmax(hardestHit, velocity.y);
      velocity.y = -std::abs(velocity.y) * physics.restitution;
   }

   return hardestHit;
}

// distance from the ball's centre to the closest point on the edge
float Ball::getDistanceTo(const GeoUtils::Edge& edge, juce::Point<float>& closest) const {
   const auto a = edge.u.toFloat();
   const auto b = edge.v.toFloat();
   const auto ab = b - a;
   const juce::Point<float> centre(x, y);

   const float lengthSq = ab.getDotProduct(ab);
   const float t = lengthSq < 1e-6f ? 0.f : juce::jlimit(0.f, 1.f, (centre - a).getDotProduct(ab) / lengthSq);
   closest = a + ab * t;

   return centre.getDistanceFrom(closest);
}

// returns the speed it hit the edge at, 0 if it didn't
float Ball::bounceOffEdge(const GeoUtils::Edge& edge, size_t index, const Physics& physics) {
   if (edge.u.getDistanceSquaredFrom(edge.v) < 1e-6 || isPassing(index)) {
      return 0.f;
   }

   juce::Point<float> closest;
   const float distance = getDistanceTo(edge, closest);

   if (distance >= radius || distance < 1e-6f) {
      return 0.f;
   }

   const auto normal = (juce::Point<float>(x, y) - closest) / distance;

   const float speedIntoEdge = velocity.getDotProduct(normal);
   const bool movingIn = speedIntoEdge < 0.f;

   // roll once when it first touches, then let it through until it's clear of the edge
   if (movingIn && physics.random != nullptr && physics.random->nextFloat() < physics.passThroughChance) {
      passingEdges.push_back(index);
      return physics.passThroughPlays ? -speedIntoEdge : 0.f;
   }

   // push the ball back out so it's touching the edge instead of overlapping it
   x += normal.x * (radius - distance);
   y += normal.y * (radius - distance);

   // only reflect if it's moving into the edge, otherwise it's already leaving
   if (movingIn) {
      velocity -= normal * ((1.f + physics.restitution) * speedIntoEdge);
      return -speedIntoEdge;
   }

   return 0.f;
}
