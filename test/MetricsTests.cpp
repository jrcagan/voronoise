#include <gtest/gtest.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <set>

#include "geometry/Metrics.h"
#include "Voronoise/Params.h"
#include "Voronoise/PluginProcessor.h"
#include "Voronoise/PluginEditor.h"
#include "TestHelpers.h"

namespace
{
   constexpr double W = BallWorld::WIDTH;
   constexpr double H = BallWorld::HEIGHT;
   using Pt = Metrics::Point;

   std::vector<Pt> randomSites(int count, int seed, bool wholePixels = true)
   {
      juce::Random random(seed);
      std::vector<Pt> sites;
      for (int i = 0; i < count; i++)
      {
         Pt p { 20.0 + random.nextDouble() * (W - 40.0), 20.0 + random.nextDouble() * (H - 40.0) };
         if (wholePixels) // like mouse clicks, which is when exact squares happen
            p = { std::round(p.x), std::round(p.y) };
         sites.push_back(p);
      }
      return sites;
   }

   double distanceIn(Metrics::Type type, Pt a, Pt b)
   {
      return type == Metrics::Type::Manhattan ? Metrics::manhattan(a, b) : Metrics::hilbert(a, b, W, H);
   }

   // the nearest and second nearest distances from p to the sites
   std::pair<double, double> twoNearest(Metrics::Type type, const std::vector<Pt> &sites, Pt p)
   {
      std::vector<double> d;
      for (const auto &s : sites)
         d.push_back(distanceIn(type, s, p));
      std::sort(d.begin(), d.end());
      return { d[0], d[1] };
   }

   void setParam(VoronoiseAudioProcessor &processor, const juce::String &id, float realValue)
   {
      auto *param = processor.apvts.getParameter(id);
      param->setValueNotifyingHost(param->convertTo0to1(realValue));
   }
}

//==============================================================================
TEST(HilbertTest, DistanceMatchesTheCrossRatio)
{
   juce::Random random(3);
   for (int i = 0; i < 2000; i++)
   {
      const Pt a { 1.0 + random.nextDouble() * (W - 2.0), 1.0 + random.nextDouble() * (H - 2.0) };
      const Pt b { 1.0 + random.nextDouble() * (W - 2.0), 1.0 + random.nextDouble() * (H - 2.0) };
      const double fast = Metrics::hilbert(a, b, W, H);
      const double slow = Metrics::hilbertCrossRatio(a, b, W, H);
      EXPECT_NEAR(fast, slow, 1e-9 * juce::jmax(1.0, slow)) << a.x << "," << a.y << " " << b.x << "," << b.y;
      EXPECT_NEAR(fast, Metrics::hilbert(b, a, W, H), 1e-9 * juce::jmax(1.0, fast)); // symmetric
      EXPECT_GE(fast, 0.0);
   }

   // grows without limit towards the boundary, and is 0 for the same point
   const Pt centre { W / 2.0, H / 2.0 };
   EXPECT_GT(Metrics::hilbert(centre, { 0.001, H / 2.0 }, W, H), Metrics::hilbert(centre, { 10.0, H / 2.0 }, W, H) + 3.0);
   EXPECT_EQ(Metrics::hilbert(centre, centre, W, H), 0.0);
}

TEST(HilbertTest, BisectorIsEquidistantAndFollowsTheSectorConics)
{
   const auto sites = randomSites(16, 11, false);
   for (size_t i = 0; i + 1 < sites.size(); i += 2)
   {
      const auto a = sites[i], b = sites[i + 1];
      const auto line = Metrics::hilbertBisector(a, b, W, H);
      ASSERT_GT(line.size(), 4u);

      for (size_t k = 0; k < line.size(); k++)
      {
         const auto p = line[k];
         EXPECT_NEAR(Metrics::hilbert(a, p, W, H), Metrics::hilbert(b, p, W, H), 1e-5) << "pair " << i << " point " << k;

         // on the conic of the sector it's in (measured as a distance, since the conic's scale is arbitrary)
         if (k > 0 && k + 1 < line.size())
         {
            Pt gradient;
            const double value = Metrics::sectorConic(Metrics::sectorAt(p, a, b, W, H), a, b, p, W, H, &gradient);
            EXPECT_LT(std::abs(value) / std::sqrt(gradient.x * gradient.x + gradient.y * gradient.y), 1e-6);
         }
         if (k > 0)
            EXPECT_LT(line[k].getDistanceFrom(line[k - 1]), 4.5); // no jumps
      }

      // it runs from one side of the screen to another
      auto onBoundary = [](Pt p) { return p.x < 1e-6 || p.y < 1e-6 || p.x > W - 1e-6 || p.y > H - 1e-6; };
      EXPECT_TRUE(onBoundary(line.front()));
      EXPECT_TRUE(onBoundary(line.back()));
   }
}

TEST(ManhattanTest, BisectorIsEquidistant)
{
   const auto sites = Metrics::nudgeSquares(randomSites(40, 5));
   for (size_t i = 0; i + 1 < sites.size(); i += 2)
   {
      const auto line = Metrics::manhattanBisector(sites[i], sites[i + 1], W, H);
      ASSERT_GE(line.size(), 2u);
      for (size_t k = 0; k + 1 < line.size(); k++)
      {
         for (double t : { 0.0, 0.3, 0.5, 0.9 })
         {
            const auto p = line[k] + (line[k + 1] - line[k]) * t;
            EXPECT_NEAR(Metrics::manhattan(sites[i], p), Metrics::manhattan(sites[i + 1], p), 1e-6);
         }

         // every piece is horizontal, vertical or exactly 45 degrees
         const auto d = line[k + 1] - line[k];
         const bool straight = std::abs(d.x) < 1e-9 || std::abs(d.y) < 1e-9 || std::abs(std::abs(d.x) - std::abs(d.y)) < 1e-6;
         EXPECT_TRUE(straight) << d.x << ", " << d.y;
      }
   }
}

TEST(ManhattanTest, SitesOnASquareAreNudged)
{
   // a 3-4-5... no: an exact square, 100 across and 100 down, where the bisector would be a whole region
   const std::vector<Pt> square { { 200.0, 100.0 }, { 300.0, 200.0 } };
   const auto nudged = Metrics::nudgeSquares(square);
   EXPECT_NE(std::abs(nudged[0].x - nudged[1].x), std::abs(nudged[0].y - nudged[1].y));
   EXPECT_LT(nudged[1].getDistanceFrom(square[1]), 1e-6); // only a tiny bit

   const auto diagram = Metrics::build(Metrics::Type::Manhattan, square, W, H);
   ASSERT_EQ(diagram.voronoiLines.size(), 1u);
   EXPECT_EQ(diagram.neighbours.size(), 1u);
}

//==============================================================================
namespace
{
   // checks a diagram against the definition of a voronoi diagram in its metric
   void checkDiagram(Metrics::Type type, const std::vector<Pt> &input, double vertexTolerance, double segmentTolerance)
   {
      const auto diagram = Metrics::build(type, input, W, H);
      const auto &sites = diagram.sites;
      ASSERT_FALSE(diagram.voronoiLines.empty());

      // 1. every bit of every wall is equally far from its two nearest sites. measured in pixels off the
      // true wall (distance difference over how fast it changes), since hilbert distances shoot up at the edge
      auto pixelsOffWall = [&](Pt p) {
         std::vector<std::pair<double, size_t>> order;
         for (size_t k = 0; k < sites.size(); k++)
            order.push_back({ distanceIn(type, sites[k], p), k });
         std::sort(order.begin(), order.end());
         const auto i = order[0].second, j = order[1].second;
         auto gap = [&](Pt q) { return distanceIn(type, sites[j], q) - distanceIn(type, sites[i], q); };
         constexpr double D = 1e-4;
         const Pt gradient { (gap(p + Pt { D, 0.0 }) - gap(p - Pt { D, 0.0 })) / (2 * D),
                             (gap(p + Pt { 0.0, D }) - gap(p - Pt { 0.0, D })) / (2 * D) };
         // two nearly lined up hilbert sites are almost tied across a whole region, and their gap barely
         // changes as you move, so pixels mean nothing there: a gap that small counts as on the wall
         if (gap(p) < (type == Metrics::Type::Hilbert ? 1e-4 : 1e-9))
            return 0.0;
         return gap(p) / juce::jmax(1e-12, std::sqrt(gradient.x * gradient.x + gradient.y * gradient.y));
      };
      auto nearEdge = [](Pt p) { return p.x < 1.0 || p.y < 1.0 || p.x > W - 1.0 || p.y > H - 1.0; };
      for (const auto &line : diagram.voronoiLines)
      {
         for (size_t k = 0; k < line.size(); k++)
         {
            if (! nearEdge(line[k]))
               EXPECT_LT(pixelsOffWall(line[k]), vertexTolerance);
            if (k + 1 < line.size())
            {
               const auto mid = (line[k] + line[k + 1]) * 0.5;
               if (! nearEdge(mid))
                  EXPECT_LT(pixelsOffWall(mid), segmentTolerance) << "between (" << line[k].x << "," << line[k].y << ") and ("
                                                                  << line[k + 1].x << "," << line[k + 1].y << ") gap "
                                                                  << [&] { auto [n, m2] = twoNearest(type, sites, mid); return m2 - n; }(); // straight pieces between points on a curve
            }
         }
      }

      // 2. and nothing's missing: wherever two neighbouring grid points belong to different sites,
      // there has to be a wall between them
      auto nearestSite = [&](Pt p) {
         size_t best = 0;
         for (size_t k = 1; k < sites.size(); k++)
            if (distanceIn(type, sites[k], p) < distanceIn(type, sites[best], p))
               best = k;
         return best;
      };
      auto wallNear = [&](Pt p, double radius) {
         for (const auto &line : diagram.voronoiLines)
            for (size_t k = 0; k + 1 < line.size(); k++)
            {
               const auto a = line[k], b = line[k + 1], ab = b - a;
               const double t = juce::jlimit(0.0, 1.0, ab.getDotProduct(p - a) / juce::jmax(1e-12, ab.getDotProduct(ab)));
               if ((a + ab * t).getDistanceFrom(p) < radius)
                  return true;
            }
         return false;
      };
      int boundaries = 0, missing = 0;
      for (double y = 6.0; y < H - 6.0; y += 6.0)
         for (double x = 6.0; x < W - 6.0; x += 6.0)
            for (auto step : { Pt { 6.0, 0.0 }, Pt { 0.0, 6.0 } })
            {
               const Pt p { x, y }, q = p + step;
               if (nearestSite(p) != nearestSite(q))
               {
                  boundaries++;
                  missing += wallNear((p + q) * 0.5, 6.0) ? 0 : 1;
               }
            }
      EXPECT_GT(boundaries, 50);
      EXPECT_EQ(missing, 0) << "of " << boundaries;

      // 3. the delaunay graph (sites that share a wall) connects everything
      std::vector<std::vector<int>> graph(sites.size());
      for (const auto &[i, j] : diagram.neighbours)
      {
         graph[static_cast<size_t>(i)].push_back(j);
         graph[static_cast<size_t>(j)].push_back(i);
      }
      std::vector<bool> seen(sites.size(), false);
      std::vector<int> stack { 0 };
      seen[0] = true;
      while (! stack.empty())
      {
         const int at = stack.back();
         stack.pop_back();
         for (int next : graph[static_cast<size_t>(at)])
            if (! seen[static_cast<size_t>(next)])
            {
               seen[static_cast<size_t>(next)] = true;
               stack.push_back(next);
            }
      }
      EXPECT_EQ(std::count(seen.begin(), seen.end(), true), static_cast<long>(sites.size()));

      // 4. a centre per cell, inside its own cell
      ASSERT_EQ(diagram.cellCentres.size(), sites.size());
      int ownCell = 0;
      for (size_t k = 0; k < sites.size(); k++)
         ownCell += nearestSite(diagram.cellCentres[k].toDouble()) == k ? 1 : 0;
      EXPECT_GE(ownCell, static_cast<int>(sites.size()) - 1); // a thin bent cell can have its centre just outside
   }
}

TEST(MetricDiagramTest, ManhattanVoronoiIsRight)
{
   for (int seed : { 1, 2, 3 })
      checkDiagram(Metrics::Type::Manhattan, randomSites(14, seed), 1e-5, 1e-5);
}

TEST(MetricDiagramTest, HilbertVoronoiIsRight)
{
   for (int seed : { 1, 2, 3 })
      checkDiagram(Metrics::Type::Hilbert, randomSites(14, seed), 0.05, 0.3);
}

TEST(MetricDiagramTest, HilbertIsQuickEnoughWithLotsOfSites)
{
   const auto start = juce::Time::getMillisecondCounterHiRes();
   const auto diagram = Metrics::build(Metrics::Type::Hilbert, randomSites(30, 9), W, H);
   const auto ms = juce::Time::getMillisecondCounterHiRes() - start;
   EXPECT_FALSE(diagram.voronoiLines.empty());
   EXPECT_LT(ms, 3000.0); // debug build, it's much quicker in release
   printf("hilbert, 30 sites: %.0f ms, %zu walls\n", ms, diagram.voronoiLines.size());
}

//==============================================================================
TEST(MetricWorldTest, BallsBounceAndPlayInEveryMetric)
{
   juce::ScopedJuceInitialiser_GUI juceInit;
   for (auto metric : { Metrics::Type::Euclidean, Metrics::Type::Manhattan, Metrics::Type::Hilbert })
   {
      VoronoiseAudioProcessor processor;
      TestHelpers::prepare(processor);
      setParam(processor, Params::METRIC, static_cast<float>(metric));
      auto sitesTree = processor.getValueTree().getChildWithName("Sites");
      for (const auto &p : randomSites(8, 21))
         sitesTree.appendChild(juce::ValueTree("Site").setProperty("x", p.x, nullptr).setProperty("y", p.y, nullptr), nullptr);

      auto &world = processor.getWorld();
      ASSERT_EQ(world.getGeometry()->metric, metric);
      const auto both = world.getGeometry()->edges.size();
      EXPECT_GT(both, 0u);
      EXPECT_FALSE(world.getGeometry()->delaunayLines.empty());

      // changing the lines setting reuses the same diagram, just picks other lines
      setParam(processor, Params::LINES, static_cast<float>(Params::Lines::VoronoiOnly));
      world.syncGeometry();
      EXPECT_LT(world.getGeometry()->edges.size(), both);
      setParam(processor, Params::LINES, static_cast<float>(Params::Lines::Both));
      world.syncGeometry();

      // balls dropped across the screen stay inside it and make sound
      TestHelpers::runAudio(processor, 0.01);
      for (float x = 30.f; x < 720.f; x += 70.f)
         world.dropBall({ x, 20.f });
      float loudest = 0.f;
      juce::AudioBuffer<float> buffer(2, 512);
      for (int block = 0; block < 300; block++)
      {
         juce::MidiBuffer midi;
         processor.processBlock(buffer, midi);
         loudest = juce::jmax(loudest, buffer.getMagnitude(0, 512));
         for (const auto &ball : world.getBalls())
            ASSERT_TRUE(ball.getX() >= -0.01f && ball.getX() <= BallWorld::WIDTH + 0.01f && ball.getY() >= -0.01f
                        && ball.getY() <= BallWorld::HEIGHT + 0.01f);
      }
      EXPECT_GT(loudest, 0.01f) << static_cast<int>(metric);

      // midi balls drop from this metric's cell centres
      EXPECT_EQ(world.getGeometry()->cellCentres.size(), 8u);
   }
}

TEST(MetricWorldTest, AnLfoCanSwitchTheMetric)
{
   juce::ScopedJuceInitialiser_GUI juceInit;
   VoronoiseAudioProcessor processor;
   TestHelpers::prepare(processor);
   auto sitesTree = processor.getValueTree().getChildWithName("Sites");
   for (const auto &p : randomSites(6, 4))
      sitesTree.appendChild(juce::ValueTree("Site").setProperty("x", p.x, nullptr).setProperty("y", p.y, nullptr), nullptr);

   // an lfo stuck at the top with full depth pushes the metric to hilbert
   LfoShape flat;
   flat.setPoints({ { 0.f, 1.f, 0.f }, { 1.f, 1.f, 0.f } });
   processor.setLfoShape(0, flat);
   processor.addModulation(0, Params::METRIC, 1.f);
   TestHelpers::runAudio(processor, 0.05);
   processor.getWorld().syncGeometry();
   EXPECT_EQ(processor.getWorld().getGeometry()->metric, Metrics::Type::Hilbert);
}
