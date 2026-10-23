#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "primitives/Ball.h"
#include "Voronoise/PluginProcessor.h"
#include "Voronoise/PluginEditor.h"

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
   auto editor = std::unique_ptr<juce::AudioProcessorEditor>(processor.createEditor());
   auto &e = dynamic_cast<VoronoiseAudioProcessorEditor &>(*editor);

   EXPECT_EQ(e.getMode(), VoronoiseAudioProcessorEditor::Mode::DrawSites);

   e.setMode(VoronoiseAudioProcessorEditor::Mode::DropBalls);
   e.dropBall({ 100.f, 100.f });
   e.dropBall({ 200.f, 100.f });
   EXPECT_EQ(e.getBalls().size(), 2u);

   // balls stay alive a while, then get cleaned up
   for (int i = 0; i < 60; i++)
      e.updateBalls(1.f / 60.f);
   EXPECT_EQ(e.getBalls().size(), 2u);

   for (int i = 0; i < 60 * 10; i++)
      e.updateBalls(1.f / 60.f);
   EXPECT_TRUE(e.getBalls().empty());
}

TEST(EditorBallTest, EdgesFollowSites)
{
   juce::ScopedJuceInitialiser_GUI juceInit;
   VoronoiseAudioProcessor processor;
   auto editor = std::unique_ptr<juce::AudioProcessorEditor>(processor.createEditor());
   auto &e = dynamic_cast<VoronoiseAudioProcessorEditor &>(*editor);

   EXPECT_TRUE(e.getEdges().empty());

   auto sites = processor.getValueTree().getChildWithName("Sites");
   for (auto [x, y] : { std::pair{ 80.0, 80.0 }, { 300.0, 90.0 }, { 200.0, 220.0 }, { 60.0, 250.0 } })
   {
      sites.appendChild(juce::ValueTree("Site").setProperty("x", x, nullptr).setProperty("y", y, nullptr), nullptr);
   }

   EXPECT_FALSE(e.getEdges().empty());

   // balls dropped around the window should stay inside it while bouncing off the cells
   for (float x = 20.f; x < 400.f; x += 40.f)
      e.dropBall({ x, 40.f });

   for (int i = 0; i < 60 * 3; i++)
   {
      e.updateBalls(1.f / 60.f);
      for (const auto &ball : e.getBalls())
      {
         ASSERT_TRUE(std::isfinite(ball.getX()) && std::isfinite(ball.getY()));
         EXPECT_TRUE(juce::Rectangle<float>(0.f, 0.f, 400.f, 300.f).expanded(0.01f).contains(ball.getX(), ball.getY()));
      }
   }

   sites.removeAllChildren(nullptr);
   EXPECT_TRUE(e.getEdges().empty());
}
