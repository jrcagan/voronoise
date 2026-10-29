#include <gtest/gtest.h>
#include <algorithm>
#include <memory>
#include <vector>

#include "primitives/Ball.h"
#include "Voronoise/PluginProcessor.h"
#include "Voronoise/PluginEditor.h"
#include "TestHelpers.h"

namespace
{
   const juce::Rectangle<float> BOUNDS(0.f, 0.f, 400.f, 300.f);

   Ball makeBall(float x, float y)
   {
      Ball ball;
      ball.setXY(x, y);
      ball.radius = 5.f;
      return ball;
   }

   void simulate(Ball &ball, float seconds, const std::vector<GeoUtils::Edge> &edges = {})
   {
      constexpr float dt = 1.f / 60.f;
      for (float t = 0.f; t < seconds; t += dt)
         ball.update(dt, BOUNDS, edges);
   }
}

TEST(BallTest, GravityPullsBallDown)
{
   auto ball = makeBall(200.f, 50.f);
   ball.update(1.f / 60.f, BOUNDS, {});

   EXPECT_GT(ball.velocity.y, 0.f);
   EXPECT_GT(ball.getY(), 50.f);
}

TEST(BallTest, StaysInsideWindow)
{
   auto ball = makeBall(200.f, 50.f);
   ball.velocity = { 2000.f, -2000.f };

   for (int i = 0; i < 600; i++)
   {
      ball.update(1.f / 60.f, BOUNDS, {});
      EXPECT_GE(ball.getX() - ball.radius, BOUNDS.getX() - 0.01f);
      EXPECT_LE(ball.getX() + ball.radius, BOUNDS.getRight() + 0.01f);
      EXPECT_GE(ball.getY() - ball.radius, BOUNDS.getY() - 0.01f);
      EXPECT_LE(ball.getY() + ball.radius, BOUNDS.getBottom() + 0.01f);
   }
}

TEST(BallTest, BouncesOffFloorWithLessSpeed)
{
   auto ball = makeBall(200.f, BOUNDS.getBottom() - 5.5f);
   ball.velocity = { 0.f, 500.f };
   ball.update(1.f / 60.f, BOUNDS, {});

   EXPECT_LT(ball.velocity.y, 0.f);              // now going up
   EXPECT_LT(std::abs(ball.velocity.y), 520.f);  // and lost some speed
}

TEST(BallTest, LandsOnEdgeInsteadOfFallingThrough)
{
   // horizontal edge across the middle of the window
   std::vector<GeoUtils::Edge> edges = { { { 50.0, 150.0 }, { 350.0, 150.0 } } };

   // with 0.8 restitution the bounces add up to ~4s from this height, so give it time to settle
   auto ball = makeBall(200.f, 50.f);
   simulate(ball, 7.f, edges);

   EXPECT_LT(ball.getY(), 150.f);
   EXPECT_NEAR(ball.getY(), 150.f - ball.radius, 1.f);
}

TEST(BallTest, SlantedEdgeDeflectsBallSideways)
{
   // slopes down to the right, so a ball dropped on it should roll/bounce right
   std::vector<GeoUtils::Edge> edges = { { { 100.0, 100.0 }, { 300.0, 200.0 } } };

   auto ball = makeBall(200.f, 100.f);
   simulate(ball, 0.5f, edges);

   EXPECT_GT(ball.getX(), 200.f);
}

TEST(BallTest, FastBallDoesNotTunnelThroughEdge)
{
   std::vector<GeoUtils::Edge> edges = { { { 0.0, 150.0 }, { 400.0, 150.0 } } };

   auto ball = makeBall(200.f, 130.f);
   ball.velocity = { 0.f, 1500.f }; // 25px per frame, more than the ball's diameter
   ball.update(1.f / 60.f, BOUNDS, edges);

   EXPECT_LT(ball.getY(), 150.f);
}

TEST(BallTest, FadesOutAndDies)
{
   auto ball = makeBall(200.f, 50.f);
   ball.timeLeftAlive = 2.f;

   EXPECT_FLOAT_EQ(ball.getAlpha(), 1.f);
   simulate(ball, 1.5f);
   EXPECT_TRUE(ball.isAlive());
   EXPECT_LT(ball.getAlpha(), 1.f);
   simulate(ball, 1.f);
   EXPECT_FALSE(ball.isAlive());
}

TEST(EditorBallTest, ModeControlsWhatClicksDo)
{
   juce::ScopedJuceInitialiser_GUI juceInit;
   VoronoiseAudioProcessor processor;
   TestHelpers::prepare(processor);
   auto editor = std::unique_ptr<juce::AudioProcessorEditor>(processor.createEditor());
   auto &e = dynamic_cast<VoronoiseAudioProcessorEditor &>(*editor).getCanvas();
   const auto &balls = processor.getWorld().getBalls();

   EXPECT_EQ(e.getMode(), UI::Canvas::Mode::DrawSites);

   // drops are requests, they show up once the audio runs a block
   e.setMode(UI::Canvas::Mode::DropBalls);
   e.dropBall({ 100.f, 100.f });
   e.dropBall({ 200.f, 100.f });
   EXPECT_TRUE(balls.empty());
   TestHelpers::runAudio(processor, 0.01);
   EXPECT_EQ(balls.size(), 2u);

   // the canvas gets a snapshot to draw
   e.syncWithProcessor();
   TestHelpers::runAudio(processor, 0.05);
   e.syncWithProcessor();
   EXPECT_EQ(e.getNumBalls(), 2);

   // balls stay alive a while, then get cleaned up
   TestHelpers::runAudio(processor, 1.0);
   EXPECT_EQ(balls.size(), 2u);
   TestHelpers::runAudio(processor, 10.0);
   EXPECT_TRUE(balls.empty());
}

TEST(EditorBallTest, EdgesFollowSites)
{
   juce::ScopedJuceInitialiser_GUI juceInit;
   VoronoiseAudioProcessor processor;
   TestHelpers::prepare(processor);
   auto editor = std::unique_ptr<juce::AudioProcessorEditor>(processor.createEditor());
   auto &e = dynamic_cast<VoronoiseAudioProcessorEditor &>(*editor).getCanvas();
   auto &world = processor.getWorld();

   EXPECT_TRUE(e.getEdges().empty());

   auto sites = processor.getValueTree().getChildWithName("Sites");
   for (auto [x, y] : { std::pair{ 80.0, 80.0 }, { 300.0, 90.0 }, { 200.0, 220.0 }, { 60.0, 250.0 } })
   {
      sites.appendChild(juce::ValueTree("Site").setProperty("x", x, nullptr).setProperty("y", y, nullptr), nullptr);
   }

   EXPECT_FALSE(e.getEdges().empty());

   // the audio side picks up the new lines on its next block
   TestHelpers::runAudio(processor, 0.01);
   EXPECT_EQ(world.getAudioGeometry().edges.size(), e.getEdges().size());

   // balls dropped around the screen should stay inside it while bouncing off the cells
   for (float x = 20.f; x < 400.f; x += 40.f)
      world.dropBall({ x, 40.f });

   const juce::Rectangle<float> screen(0.f, 0.f, BallWorld::WIDTH, BallWorld::HEIGHT);
   for (int i = 0; i < 60 * 3; i++)
   {
      TestHelpers::runAudio(processor, 1.0 / 60.0);
      for (const auto &ball : world.getBalls())
      {
         ASSERT_TRUE(std::isfinite(ball.getX()) && std::isfinite(ball.getY()));
         EXPECT_TRUE(screen.expanded(0.01f).contains(ball.getX(), ball.getY()));
      }
   }

   sites.removeAllChildren(nullptr);
   EXPECT_TRUE(e.getEdges().empty());
}

//==============================================================================
// min bounce keeps a ball hopping, but a ball boxed in somewhere it can't hop used to get kicked on
// every landing and rattle there until it died, which sounded like one note that wouldn't stop
namespace
{
   // the times (seconds) of the hits a ball would play, stepped like the world steps it
   std::vector<float> playedHits(Ball &ball, float seconds, const std::vector<GeoUtils::Edge> &edges, float minBounce = 250.f)
   {
      Ball::Physics physics;
      physics.minBounce = minBounce;
      ball.timeLeftAlive = seconds + 1.f;

      constexpr float h = 1.f / 240.f;
      std::vector<float> times;
      for (int i = 0; i < static_cast<int>(seconds / h); i++)
         if (ball.shouldPlayHit(ball.step(h, BOUNDS, edges, physics).speed))
            times.push_back(static_cast<float>(i) * h);
      return times;
   }

   int hitsAfter(const std::vector<float> &times, float from)
   {
      return static_cast<int>(std::count_if(times.begin(), times.end(), [from](float t) { return t >= from; }));
   }
}

TEST(TrappedBallTest, BallUnderTheCeilingSettlesInsteadOfRattling)
{
   // a line 20px under the ceiling, the ball dropped in the gap. a min bounce hop would go 35px up
   const std::vector<GeoUtils::Edge> edges { { { 0.0, 30.0 }, { 400.0, 30.0 } } };
   auto ball = makeBall(200.f, 15.f);
   const auto times = playedHits(ball, 8.f, edges);

   EXPECT_GT(times.size(), 0u);
   EXPECT_EQ(hitsAfter(times, 3.f), 0) << times.size() << " hits, still going at the end";

   EXPECT_LT(ball.getY(), 30.f); // still sitting on its line
}

TEST(TrappedBallTest, NoPocketRattlesFasterThanAHop)
{
   // vs, lines running into the side walls, and gaps under the ceiling, all sorts of sizes. once it's
   // had time to settle, a ball can't play faster than a hop would let it: two hits a hop at most
   // (one off a wall on the way). a rattling one plays about 10 a second
   const float maxRate = 2.f / (2.f * 250.f / 900.f) + 0.5f;
   std::vector<std::pair<juce::String, std::vector<GeoUtils::Edge>>> pockets;
   for (double angle = 30.0; angle <= 70.0; angle += 10.0)
      for (double depth = 15.0; depth <= 60.0; depth += 15.0)
      {
         const double run = depth / std::tan(juce::degreesToRadians(angle));
         pockets.push_back({ "v " + juce::String(angle) + " " + juce::String(depth),
                             { { { 200.0 - run, 200.0 - depth }, { 200.0, 200.0 } }, { { 200.0, 200.0 }, { 200.0 + run, 200.0 - depth } } } });
         pockets.push_back({ "wall " + juce::String(angle) + " " + juce::String(depth),
                             { { { 400.0 - run, 200.0 - depth }, { 400.0, 200.0 } } } });
      }
   for (double gap = 8.0; gap <= 32.0; gap += 4.0)
      pockets.push_back({ "ceiling " + juce::String(gap), { { { 0.0, 10.0 + gap }, { 400.0, 10.0 + gap } } } });

   int rattling = 0;
   for (const auto &[name, edges] : pockets)
      for (float dx : { -3.f, 1.f, 4.f })
      {
         const auto lowest = std::max(edges.front().u.y, edges.back().v.y);
         const float x = name.startsWith("wall") ? 392.f + dx : 200.f + dx * 2.f;
         auto ball = makeBall(x, name.startsWith("ceiling") ? 6.f : static_cast<float>(lowest) - 20.f);
         const auto times = playedHits(ball, 8.f, edges);
         const float rate = static_cast<float>(hitsAfter(times, 3.f)) / 5.f;
         if (rate > maxRate)
         {
            rattling++;
            ADD_FAILURE() << name << " dx " << dx << ": " << rate << " hits a second";
         }
      }
   EXPECT_EQ(rattling, 0);
}

TEST(TrappedBallTest, OpenFloorAndLinesStillHopForever)
{
   // the floor: a hop about every 2 * 250 / 900 = 0.56s, the whole time
   auto onFloor = makeBall(200.f, 100.f);
   const auto floorTimes = playedHits(onFloor, 8.f, {});
   EXPECT_GE(hitsAfter(floorTimes, 6.f), 3);

   // and a gentle slope it can hop down (and then along the floor)
   const std::vector<GeoUtils::Edge> slope { { { 0.0, 150.0 }, { 400.0, 220.0 } } };
   auto onSlope = makeBall(50.f, 100.f);
   const auto slopeTimes = playedHits(onSlope, 8.f, slope);
   EXPECT_GE(hitsAfter(slopeTimes, 6.f), 3);

   // never faster than the hits a real hop would make (two per hop at most, against a wall)
   for (const auto *times : { &floorTimes, &slopeTimes })
      EXPECT_LT(hitsAfter(*times, 4.f), 4 * 4);
}
