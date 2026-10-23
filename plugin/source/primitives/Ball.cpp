#include "primitives/Ball.h"
#include <cmath>

void Ball::update(float dt, const juce::Rectangle<float>& bounds, const std::vector<GeoUtils::Edge>& edges) {
   timeLeftAlive -= dt;

   // split the frame into smaller steps so fast balls don't skip through edges
   constexpr int SUBSTEPS = 4;
   const float h = dt / static_cast<float>(SUBSTEPS);

   for (int step = 0; step < SUBSTEPS; step++) {
      velocity.y += GRAVITY * h;
      x += velocity.x * h;
      y += velocity.y * h;

      for (const auto& edge : edges) {
         bounceOffEdge(edge);
      }

      bounceOffWalls(bounds);
   }
}

bool Ball::isAlive() const {
   return timeLeftAlive > 0.f;
}

float Ball::getAlpha() const {
   return juce::jlimit(0.f, 1.f, timeLeftAlive / FADE_TIME);
}

void Ball::bounceOffWalls(const juce::Rectangle<float>& bounds) {
   if (x - radius < bounds.getX()) {
      x = bounds.getX() + radius;
      velocity.x = std::abs(velocity.x) * RESTITUTION;
   } else if (x + radius > bounds.getRight()) {
      x = bounds.getRight() - radius;
      velocity.x = -std::abs(velocity.x) * RESTITUTION;
   }

   if (y - radius < bounds.getY()) {
      y = bounds.getY() + radius;
      velocity.y = std::abs(velocity.y) * RESTITUTION;
   } else if (y + radius > bounds.getBottom()) {
      y = bounds.getBottom() - radius;
      velocity.y = -std::abs(velocity.y) * RESTITUTION;
   }
}

void Ball::bounceOffEdge(const GeoUtils::Edge& edge) {
   const auto a = edge.u.toFloat();
   const auto b = edge.v.toFloat();
   const auto ab = b - a;

   const float lengthSq = ab.getDotProduct(ab);
   if (lengthSq < 1e-6f) {
      return;
   }

   // closest point on the edge to the ball's centre
   const juce::Point<float> centre(x, y);
   const float t = juce::jlimit(0.f, 1.f, (centre - a).getDotProduct(ab) / lengthSq);
   const auto closest = a + ab * t;

   const auto offset = centre - closest;
   const float distance = offset.getDistanceFromOrigin();

   if (distance >= radius || distance < 1e-6f) {
      return;
   }

   const auto normal = offset / distance;

   // push the ball back out so it's touching the edge instead of overlapping it
   x += normal.x * (radius - distance);
   y += normal.y * (radius - distance);

   // only reflect if it's moving into the edge, otherwise it's already leaving
   const float speedIntoEdge = velocity.getDotProduct(normal);
   if (speedIntoEdge < 0.f) {
      velocity -= normal * ((1.f + RESTITUTION) * speedIntoEdge);
   }
}
