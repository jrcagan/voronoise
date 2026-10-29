#include "primitives/Ball.h"
#include <algorithm>
#include <cmath>

float Ball::update(float dt, const juce::Rectangle<float>& bounds, const std::vector<GeoUtils::Edge>& edges,
                   const Physics& physics) {
   // split the frame into smaller steps so fast balls don't skip through edges
   constexpr int SUBSTEPS = 4;
   const float h = dt / static_cast<float>(SUBSTEPS);

   float hardestHit = 0.f;
   for (int i = 0; i < SUBSTEPS; i++) {
      hardestHit = juce::jmax(hardestHit, step(h, bounds, edges, physics).speed);
   }
   return hardestHit;
}

Ball::Contact Ball::step(float h, const juce::Rectangle<float>& bounds, const std::vector<GeoUtils::Edge>& edges,
                         const Physics& physics) {
   // hanging where it spawned until its key is let go, its life hasn't started yet
   if (isWaiting()) {
      return {};
   }

   timeLeftAlive -= h;
   hitCooldown -= h;
   sinceLanding += h;

   // move by the average of the old and new velocity, which is exact under gravity. moving by the new
   // one alone puts the ball half a step ahead, so every hit would land a couple of ms early
   const auto before = velocity;
   velocity.y += physics.gravity * h;
   velocity *= std::exp(-physics.drag * h);
   x += 0.5f * (before.x + velocity.x) * h;
   y += 0.5f * (before.y + velocity.y) * h;

   Contact hardest;
   auto keepHardest = [&hardest](Contact contact) {
      if (contact.speed > hardest.speed) {
         hardest = contact;
      }
   };

   for (size_t i = 0; i < edges.size(); i++) {
      keepHardest(bounceOffEdge(edges[i], static_cast<int>(i), physics, h));
   }
   keepHardest(bounceOffWalls(bounds, physics, h));

   // stop tracking edges it's made it all the way through
   int kept = 0;
   for (int i = 0; i < numPassing; i++) {
      const int index = passingEdges[static_cast<size_t>(i)];
      juce::Point<float> closest;
      if (index < static_cast<int>(edges.size()) && getDistanceTo(edges[static_cast<size_t>(index)], closest) < radius) {
         passingEdges[static_cast<size_t>(kept++)] = index;
      }
   }
   numPassing = kept;

   return hardest;
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

float Ball::getHitGain(float impactSpeed, float dynamics) const {
   const float hitAmount = juce::jlimit(0.f, 1.f, impactSpeed / LOUD_HIT_SPEED);
   const float impactScale = 1.f - juce::jlimit(0.f, 1.f, dynamics) * (1.f - hitAmount);
   return impactScale * juce::jlimit(0.f, 1.f, noteVelocity / 127.f);
}

float Ball::getHopTime(const Physics& physics) {
   if (physics.gravity <= 0.f || physics.minBounce <= 0.f) {
      return 0.f;
   }
   // twice the time to the top. with drag the way down is slower than the way up, so it's a bit more
   const float toTop = physics.drag > 1e-4f ? std::log(1.f + physics.drag * physics.minBounce / physics.gravity) / physics.drag
                                            : physics.minBounce / physics.gravity;
   return 2.f * toTop;
}

bool Ball::land(const Physics& physics) {
   // a little slack for the physics step
   shortHops = sinceLanding < 0.9f * getHopTime(physics) ? shortHops + 1 : 0;
   sinceLanding = 0.f;
   return shortHops < 2;
}

bool Ball::isAlive() const {
   return timeLeftAlive > 0.f;
}

float Ball::getAlpha() const {
   return juce::jlimit(0.f, 1.f, timeLeftAlive / FADE_TIME);
}

bool Ball::isPassing(int index) const {
   for (int i = 0; i < numPassing; i++) {
      if (passingEdges[static_cast<size_t>(i)] == index) {
         return true;
      }
   }
   return false;
}

namespace
{
   // how long ago it touched, from how far it went past and how fast it was going
   float timeSinceContact(float overlap, float speed, float h) {
      return speed > 1e-6f ? juce::jlimit(0.f, h, overlap / speed) : 0.f;
   }
}

// how hard it hit a wall (and when), nothing if it didn't
Ball::Contact Ball::bounceOffWalls(const juce::Rectangle<float>& bounds, const Physics& physics, float h) {
   Contact hardest;
   auto hit = [&](float overlap, float speed) {
      if (speed > hardest.speed) {
         hardest = { speed, timeSinceContact(overlap, speed, h) };
      }
   };

   // sideways there's no gravity, so the time after the touch is just spent moving back out
   if (x - radius < bounds.getX()) {
      hit(bounds.getX() + radius - x, -velocity.x);
      velocity.x = std::abs(velocity.x) * physics.restitution;
      x = bounds.getX() + radius + velocity.x * hardest.secondsAgo;
   } else if (x + radius > bounds.getRight()) {
      hit(x + radius - bounds.getRight(), velocity.x);
      velocity.x = -std::abs(velocity.x) * physics.restitution;
      x = bounds.getRight() - radius + velocity.x * hardest.secondsAgo;
   }

   // up and down: undo the gravity since the touch, bounce, then let gravity carry on for that time
   auto bounceVertically = [&](float surface, float overlap, float speedIn, float sign) {
      const auto before = hardest;
      hit(overlap, speedIn);
      const float ago = hardest.speed > before.speed ? hardest.secondsAgo : timeSinceContact(overlap, speedIn, h);
      float atTouch = std::abs(velocity.y - physics.gravity * ago) * physics.restitution * sign;
      // the floor never lets it settle: at least the min bounce back up, if there's room to hop
      if (sign < 0.f) {
         if (land(physics)) {
            atTouch = -juce::jmax(std::abs(atTouch), physics.minBounce);
         }
      }
      y = surface + atTouch * ago + 0.5f * physics.gravity * ago * ago;
      velocity.y = atTouch + physics.gravity * ago;
   };

   if (y - radius < bounds.getY()) {
      bounceVertically(bounds.getY() + radius, bounds.getY() + radius - y, -velocity.y, 1.f);
   } else if (y + radius > bounds.getBottom()) {
      bounceVertically(bounds.getBottom() - radius, y + radius - bounds.getBottom(), velocity.y, -1.f);
   }

   return hardest;
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

// how hard it hit the edge (and when), nothing if it didn't
Ball::Contact Ball::bounceOffEdge(const GeoUtils::Edge& edge, int index, const Physics& physics, float h) {
   if (edge.u.getDistanceSquaredFrom(edge.v) < 1e-6 || isPassing(index)) {
      return {};
   }

   juce::Point<float> closest;
   const float distance = getDistanceTo(edge, closest);

   if (distance >= radius || distance < 1e-6f) {
      return {};
   }

   const auto normal = (juce::Point<float>(x, y) - closest) / distance;

   const float speedIntoEdge = velocity.getDotProduct(normal);
   const bool movingIn = speedIntoEdge < 0.f;

   const Contact contact { -speedIntoEdge, timeSinceContact(radius - distance, -speedIntoEdge, h) };

   // roll once when it first touches, then let it through until it's clear of the edge
   if (movingIn && numPassing < MAX_PASSING && physics.random != nullptr && physics.random->nextFloat() < physics.passThroughChance) {
      passingEdges[static_cast<size_t>(numPassing++)] = index;
      return physics.passThroughPlays ? contact : Contact {};
   }

   // push the ball back out so it's touching the edge instead of overlapping it
   x += normal.x * (radius - distance);
   y += normal.y * (radius - distance);

   // only reflect if it's moving into the edge, otherwise it's already leaving
   if (movingIn) {
      // rewind to the touch (undo gravity since then), bounce, then replay the rest of the step
      const float ago = contact.secondsAgo;
      const juce::Point<float> gravity { 0.f, physics.gravity };
      const auto atTouch = velocity - gravity * ago;
      auto bounced = atTouch - normal * ((1.f + physics.restitution) * atTouch.getDotProduct(normal));

      // a line it can land on (facing upwards) sends it off at least the min bounce, so it hops
      // instead of sliding along silently. walls and overhangs bounce as normal
      constexpr float FACING_UP = -0.3f; // screen y points down
      const float awaySpeed = bounced.getDotProduct(normal);
      if (normal.y < FACING_UP && land(physics) && awaySpeed < physics.minBounce) {
         bounced += normal * (physics.minBounce - awaySpeed);
      }
      x += bounced.x * ago;
      y += bounced.y * ago + 0.5f * physics.gravity * ago * ago;
      velocity = bounced + gravity * ago;
      return contact;
   }

   return {};
}
