#include <gtest/gtest.h>
#include <memory>

#include "Voronoise/PluginProcessor.h"
#include "Voronoise/PluginEditor.h"
#include "TestHelpers.h"

namespace
{
   void setParam(VoronoiseAudioProcessor &processor, const juce::String &id, float realValue)
   {
      auto *param = processor.apvts.getParameter(id);
      param->setValueNotifyingHost(param->convertTo0to1(realValue));
   }

   juce::ValueTree sitesOf(VoronoiseAudioProcessor &processor)
   {
      return processor.getValueTree().getChildWithName("Sites");
   }

   void addSites(VoronoiseAudioProcessor &processor, int count, int seed)
   {
      juce::Random rng(seed);
      for (int i = 0; i < count; i++)
         sitesOf(processor).appendChild(juce::ValueTree("Site")
                                            .setProperty("x", 20.0 + rng.nextDouble() * 700.0, nullptr)
                                            .setProperty("y", 20.0 + rng.nextDouble() * 300.0, nullptr),
                                        nullptr);
   }

   juce::Point<float> siteAt(VoronoiseAudioProcessor &processor, int index)
   {
      const auto site = sitesOf(processor).getChild(index);
      return { static_cast<float>(site["x"]), static_cast<float>(site["y"]) };
   }

   // what the message thread's timers do until the background build is in
   bool waitForLines(BallWorld &world)
   {
      for (int i = 0; i < 2000 && world.isRebuilding(); i++)
      {
         juce::Thread::sleep(2);
         world.syncGeometry();
      }
      return ! world.isRebuilding();
   }

   struct Editor
   {
      juce::ScopedJuceInitialiser_GUI juceInit;
      VoronoiseAudioProcessor processor;
      std::unique_ptr<juce::AudioProcessorEditor> editor;
      UI::Canvas *canvas = nullptr;

      Editor()
      {
         TestHelpers::prepare(processor);
         editor.reset(processor.createEditor());
         canvas = &dynamic_cast<VoronoiseAudioProcessorEditor &>(*editor).getCanvas();
      }
   };
}

TEST(DragSiteTest, GrabsTheClosestSiteWithinReach)
{
   Editor e;
   sitesOf(e.processor).appendChild(juce::ValueTree("Site").setProperty("x", 100.0, nullptr).setProperty("y", 100.0, nullptr), nullptr);
   sitesOf(e.processor).appendChild(juce::ValueTree("Site").setProperty("x", 110.0, nullptr).setProperty("y", 100.0, nullptr), nullptr);

   EXPECT_EQ(e.canvas->findSiteAt({ 103.f, 100.f }), 0);
   EXPECT_EQ(e.canvas->findSiteAt({ 107.f, 101.f }), 1);
   EXPECT_EQ(e.canvas->findSiteAt({ 100.f, 100.f + UI::Canvas::GRAB_RADIUS + 1.f }), -1);

   // nothing there: nothing to drag
   EXPECT_FALSE(e.canvas->beginDrag({ 300.f, 200.f }));
   EXPECT_FALSE(e.canvas->isDragging());
}

TEST(DragSiteTest, SiteFollowsTheMouseFromWhereItWasGrabbedAndStaysOnScreen)
{
   Editor e;
   sitesOf(e.processor).appendChild(juce::ValueTree("Site").setProperty("x", 100.0, nullptr).setProperty("y", 100.0, nullptr), nullptr);

   // grabbed 3px to the right of its middle, so it doesn't jump over to the pointer
   ASSERT_TRUE(e.canvas->beginDrag({ 103.f, 100.f }));
   e.canvas->dragTo({ 203.f, 150.f });
   EXPECT_EQ(siteAt(e.processor, 0), juce::Point<float>(200.f, 150.f));

   // dragged off the edge, it stops at the edge
   e.canvas->dragTo({ -50.f, 1000.f });
   EXPECT_EQ(siteAt(e.processor, 0), juce::Point<float>(0.f, BallWorld::HEIGHT));

   e.canvas->endDrag();
   EXPECT_FALSE(e.canvas->isDragging());
   e.canvas->dragTo({ 300.f, 100.f }); // let go, so this does nothing
   EXPECT_EQ(siteAt(e.processor, 0), juce::Point<float>(0.f, BallWorld::HEIGHT));
   EXPECT_EQ(sitesOf(e.processor).getNumChildren(), 1);
}

TEST(DragSiteTest, LinesFollowTheDraggedSiteInEveryMetric)
{
   for (auto metric : { Metrics::Type::Euclidean, Metrics::Type::Manhattan, Metrics::Type::Hilbert })
   {
      Editor e;
      setParam(e.processor, Params::METRIC, static_cast<float>(metric));
      addSites(e.processor, 12, 3);
      auto &world = e.processor.getWorld();
      world.syncGeometry();
      const auto before = world.getGeometry()->voronoiLines;

      // a drag across the screen, a few mouse moves at a time like the real thing
      const auto start = siteAt(e.processor, 4);
      ASSERT_TRUE(e.canvas->beginDrag(start));
      for (int move = 1; move <= 30; move++)
         e.canvas->dragTo(start + juce::Point<float>(5.f * static_cast<float>(move), 2.f * static_cast<float>(move)));
      e.canvas->endDrag();
      const auto end = start + juce::Point<float>(150.f, 60.f);

      // the lines catch up with where it ended up, the same as building them from scratch
      ASSERT_TRUE(waitForLines(world)) << static_cast<int>(metric);
      const auto dragged = world.getGeometry();
      EXPECT_NE(dragged->voronoiLines, before);
      ASSERT_EQ(dragged->sites.size(), 12u);
      EXPECT_EQ(dragged->sites[4].toFloat(), end);

      world.rebuildGeometry();
      EXPECT_EQ(world.getGeometry()->voronoiLines, dragged->voronoiLines) << static_cast<int>(metric);
      EXPECT_EQ(world.getGeometry()->edges, dragged->edges);
      EXPECT_EQ(world.getGeometry()->cellCentres, dragged->cellCentres);

      // and the audio side picks them up, so balls bounce off the moved lines
      TestHelpers::runAudio(e.processor, 0.01);
      EXPECT_EQ(world.getAudioGeometry().sites[4].toFloat(), end);
   }
}

TEST(DragSiteTest, DraggingNeverWaitsForHilbertToBuild)
{
   Editor e;
   setParam(e.processor, Params::METRIC, static_cast<float>(Metrics::Type::Hilbert));
   addSites(e.processor, 30, 8); // a full build of these takes a while (longer still in a debug build)
   auto &world = e.processor.getWorld();

   const auto start = siteAt(e.processor, 0);
   ASSERT_TRUE(e.canvas->beginDrag(start));
   double slowest = 0.0;
   for (int move = 1; move <= 20; move++)
   {
      const auto before = juce::Time::getMillisecondCounterHiRes();
      e.canvas->dragTo(start + juce::Point<float>(3.f * static_cast<float>(move), 0.f));
      world.syncGeometry(); // the timers keep picking up whatever's done
      slowest = juce::jmax(slowest, juce::Time::getMillisecondCounterHiRes() - before);
   }
   e.canvas->endDrag();

   EXPECT_LT(slowest, 10.0) << "ms for one mouse move";
   ASSERT_TRUE(waitForLines(world));
   EXPECT_EQ(world.getGeometry()->sites[0].toFloat(), start + juce::Point<float>(60.f, 0.f));
}

TEST(DragSiteTest, AnOldBackgroundBuildNeverReplacesANewerOne)
{
   Editor e;
   setParam(e.processor, Params::METRIC, static_cast<float>(Metrics::Type::Hilbert));
   addSites(e.processor, 20, 5);
   auto &world = e.processor.getWorld();

   // a drag starts building in the background, then a site gets added (built straight away)
   ASSERT_TRUE(e.canvas->beginDrag(siteAt(e.processor, 2)));
   e.canvas->dragTo(siteAt(e.processor, 2) + juce::Point<float>(40.f, 0.f));
   e.canvas->endDrag();
   sitesOf(e.processor).appendChild(juce::ValueTree("Site").setProperty("x", 400.0, nullptr).setProperty("y", 200.0, nullptr), nullptr);
   EXPECT_EQ(world.getGeometry()->sites.size(), 21u);

   // whenever the drag's build finishes, it's out of date and stays out
   juce::Thread::sleep(400);
   for (int i = 0; i < 10; i++)
      world.syncGeometry();
   EXPECT_FALSE(world.isRebuilding());
   EXPECT_EQ(world.getGeometry()->sites.size(), 21u);
}

TEST(DragSiteTest, MovingOneSiteReusesTheOtherBisectorsAndMatchesAFreshBuild)
{
   for (auto type : { Metrics::Type::Manhattan, Metrics::Type::Hilbert })
   {
      juce::Random rng(17);
      std::vector<Metrics::Point> sites;
      for (int i = 0; i < 15; i++)
         sites.push_back({ 5.0 + rng.nextDouble() * 736.0, 5.0 + rng.nextDouble() * 338.0 });

      Metrics::BisectorCache cache;
      Metrics::build(type, sites, BallWorld::WIDTH, BallWorld::HEIGHT, &cache);
      for (int frame = 0; frame < 5; frame++)
      {
         sites[3] += { 7.3, -2.1 };
         const auto cached = Metrics::build(type, sites, BallWorld::WIDTH, BallWorld::HEIGHT, &cache);
         const auto fresh = Metrics::build(type, sites, BallWorld::WIDTH, BallWorld::HEIGHT);
         EXPECT_EQ(cached.voronoiLines, fresh.voronoiLines) << static_cast<int>(type) << " frame " << frame;
         EXPECT_EQ(cached.neighbours, fresh.neighbours);
         EXPECT_EQ(cache.bisectors.size(), 15u * 14u / 2u); // just this build's, it doesn't keep growing
      }
   }
}
