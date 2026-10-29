#pragma once

#include <JuceHeader.h>
#include <array>
#include <map>
#include <mutex>
#include <optional>
#include <vector>
#include "DSP/Fifo.h"
#include "geometry/Utils.h"
#include "geometry/Voronoi.h"
#include "geometry/Metrics.h"
#include "primitives/Ball.h"
#include "synth/HitSynth.h"
#include "Voronoise/Params.h"

class VoronoiseAudioProcessor;

// the sites, lines and bouncing balls. the balls live on the audio thread and are stepped in time
// with the audio, so every hit lands on the sample the ball actually touched the line, and they
// keep going with the editor closed. the ui sends requests in and gets drawing snapshots out
class BallWorld
{
public:
   explicit BallWorld(VoronoiseAudioProcessor& processor);
   ~BallWorld();

   // the screen, in the same units the canvas draws in
   static constexpr float WIDTH = 746.f;
   static constexpr float HEIGHT = 348.f;
   static constexpr int STEPS_PER_SECOND = 240; // physics steps, the same as 60fps with 4 substeps
   static constexpr int MAX_BALLS = 500;
   static constexpr int MAX_HITS_PER_BLOCK = 256;

   // lines and drop points built from the sites, read only once it's made
   struct Geometry : juce::ReferenceCountedObject
   {
      using Ptr = juce::ReferenceCountedObjectPtr<Geometry>;

      std::vector<GeoUtils::Point> sites;
      std::vector<GeoUtils::Triangle> triangles;
      std::map<GeoUtils::Point, Voronoi::Cell, GeoUtils::PointComparator> cells;
      std::vector<Metrics::Polyline> voronoiLines;  // the cell walls, in whichever metric
      std::vector<GeoUtils::Edge> delaunayLines;    // between sites that share a wall
      std::vector<GeoUtils::Edge> edges;            // what balls bounce off: whichever lines are on, minus the borders
      std::vector<juce::Point<float>> cellCentres;  // where midi balls drop from
      Params::Lines lines = Params::Lines::Both;
      Metrics::Type metric = Metrics::Type::Euclidean;
   };

   // what the canvas draws
   struct BallView {
      float x = 0.f, y = 0.f, radius = 0.f;
      juce::uint32 argb = 0;
      bool waiting = false; // hanging until its key is let go
   };
   struct Snapshot {
      int numBalls = 0;
      std::array<BallView, MAX_BALLS> balls {};
   };

   // a ball hit, sampleOffset into the current block
   struct HitEvent {
      int sampleOffset = 0;
      HitSynth::Hit hit;
      juce::uint32 ball = 0; // which ball made it
   };

   //==============================================================================
   // message thread
   void rebuildGeometry();  // from the sites in the state and the (modulated) lines setting, right now
   // the same, but built on another thread and picked up by syncGeometry. for dragging a site, where
   // hilbert can take longer than a frame: only the newest request gets built, the ui never waits
   void requestRebuild();
   void syncGeometry();     // takes any finished background build, rebuilds if an lfo moved the lines setting
   bool isRebuilding() const; // a background build hasn't been picked up yet
   Geometry::Ptr getGeometry() const { return geometry; }

   void requestDrop(juce::Point<float> position);
   void requestMidiBall(int noteNumber, int velocity);
   void requestClear();

   bool pullSnapshot(Snapshot& snapshot); // newest snapshot, false if nothing new
   int getNoteForHeight(float y) const;

   //==============================================================================
   // audio thread
   void prepare(double sampleRate);
   // picks up new lines and queued drops, call before handling the block's midi so a note played
   // right after adding a site can already drop from it
   void beginBlock();
   void handleNoteOn(int noteNumber, int velocity, int sampleOffset = 0);
   void handleNoteOff(int noteNumber, int sampleOffset = 0);

   // runs the physics for one block, fills hits (in time order) and returns how many
   int process(int numSamples, std::array<HitEvent, MAX_HITS_PER_BLOCK>& hits);

   // every hit plays exactly this long after the ball touched. a contact can be found up to one physics
   // step after it happened, which might be a block that's already gone, so hits wait one step to
   // always land on the right sample whatever the block size
   int getLatencySamples() const { return latencySamples; }

   // these act straight away instead of going through the request queue. audio thread, or tests
   void dropBall(juce::Point<float> position);
   void spawnMidiBall(int noteNumber, int velocity, juce::uint32 keyPress = 0);
   void clearBalls();
   const std::vector<Ball>& getBalls() const { return balls; }
   const Geometry& getAudioGeometry() const { return *audioGeometry; }

private:
   struct Request {
      enum class Type {
         Drop,
         Midi,
         Clear
      };
      Type type = Type::Drop;
      float x = 0.f, y = 0.f;
      int noteNumber = 60, velocity = 100;
   };

   // what a build needs, read from the state and parameters on the message thread
   struct BuildInputs {
      std::vector<GeoUtils::Point> sites;
      Params::Lines lines = Params::Lines::Both;
      Metrics::Type metric = Metrics::Type::Euclidean;
   };
   BuildInputs getBuildInputs() const;
   Geometry::Ptr build(const BuildInputs& inputs); // any thread, holds buildLock
   void install(Geometry::Ptr built);             // message thread

   // builds the latest request in the background
   class Builder : public juce::Thread
   {
   public:
      explicit Builder(BallWorld& world) : juce::Thread("Voronoise geometry"), world(world) {}
      void run() override;

   private:
      BallWorld& world;
   };

   void handleRequests();
   void addBall(const Ball& ball);
   void setupBall(Ball& ball, int noteNumber, float sizeScale);
   Ball::Physics getPhysics();
   void publishSnapshot();
   static juce::Point<float> getCentroid(const std::vector<GeoUtils::Point>& vertices);

   VoronoiseAudioProcessor& processor;

   // message thread
   Geometry::Ptr geometry;
   juce::ReferenceCountedArray<Geometry> geometryPool; // keeps old ones alive until the audio thread lets go

   // whoever's building (message thread or the builder). manhattan/hilbert diagrams are slow-ish, so the
   // last one is kept for when only the lines setting changes, and their bisectors for when one site moves
   std::mutex buildLock;
   std::vector<GeoUtils::Point> cachedSites;
   Metrics::Type cachedMetric = Metrics::Type::Euclidean;
   Metrics::Diagram cachedDiagram;
   Metrics::BisectorCache bisectorCache;

   // background builds. every request gets a number, and a build only goes in if nothing newer has
   std::mutex jobLock;
   std::optional<BuildInputs> job;
   juce::uint64 jobNumber = 0;
   Geometry::Ptr finished;
   juce::uint64 finishedNumber = 0;
   juce::uint64 requestNumber = 0;   // message thread
   juce::uint64 installedNumber = 0; // message thread
   juce::WaitableEvent jobReady;
   Builder builder { *this };

   // handed across
   SimpleMBComp::Fifo<Geometry::Ptr, 16> geometryFifo;
   SimpleMBComp::Fifo<Request, 256> requests;
   SimpleMBComp::Fifo<Snapshot, 3> snapshots;

   // audio thread
   Geometry::Ptr audioGeometry;
   std::vector<Ball> balls;
   juce::Random random;
   double sampleRate = 44100.0;
   double nextStep = 0.0;          // samples into the coming block when the next physics step ends
   juce::int64 clock = 0;          // samples processed so far
   int latencySamples = 0;

   struct PendingHit {
      juce::int64 when = 0;        // on the clock above
      HitSynth::Hit hit;
      juce::uint32 ball = 0;
   };

   // the press each held key is on, and when it went down (on the clock)
   std::array<juce::uint32, 128> keyPresses {};
   std::array<juce::int64, 128> keyDownAt {};
   juce::uint32 nextKeyPress = 1;
   juce::uint32 nextBallId = 1;
   std::array<PendingHit, MAX_HITS_PER_BLOCK * 2> pending {};
   int numPending = 0;
   double samplesSinceSnapshot = 0.0;
   Snapshot outgoing;              // built here then copied into the fifo, too big for the stack
};
