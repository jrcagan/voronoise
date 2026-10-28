#include "world/BallWorld.h"
#include "Voronoise/PluginProcessor.h"
#include "geometry/Delaunay.h"
#include "synth/Scale.h"
#include <set>

BallWorld::BallWorld(VoronoiseAudioProcessor& p) : processor(p)
{
   // room for the most balls the knob allows, so adding one on the audio thread never allocates
   balls.reserve(MAX_BALLS);

   geometry = new Geometry();
   audioGeometry = geometry;
   geometryPool.add(geometry);
}

//==============================================================================
void BallWorld::rebuildGeometry()
{
   Geometry::Ptr built = new Geometry();
   built->lines = static_cast<Params::Lines>(static_cast<int>(processor.getParamValue(Params::LINES)));

   const auto sitesTree = processor.getValueTree().getChildWithName("Sites");
   for (int i = 0; i < sitesTree.getNumChildren(); i++)
   {
      const auto site = sitesTree.getChild(i);
      built->sites.push_back(GeoUtils::Point(static_cast<double>(site["x"]), static_cast<double>(site["y"])));
   }

   if (built->sites.size() > 2)
   {
      built->triangles = Delaunay::triangulate(built->sites);

      const GeoUtils::BBox bbox { 0.0, 0.0, static_cast<double>(WIDTH), static_cast<double>(HEIGHT) };
      built->cells = Voronoi::getCells(built->triangles, bbox);

      for (const auto& kv : built->cells)
         built->cellCentres.push_back(getCentroid(kv.second.vertices));

      // balls bounce off whichever lines are switched on, the set drops duplicates
      std::set<GeoUtils::Edge, GeoUtils::EdgeComparator> uniqueEdges;
      if (built->lines != Params::Lines::DelaunayOnly)
         for (const auto& edge : Voronoi::getEdges(built->triangles, bbox))
            uniqueEdges.insert(edge);
      if (built->lines != Params::Lines::VoronoiOnly)
      {
         for (const auto& t : built->triangles)
         {
            uniqueEdges.insert({ t.a, t.b });
            uniqueEdges.insert({ t.b, t.c });
            uniqueEdges.insert({ t.c, t.a });
         }
      }

      // voronoi cells get clipped to the screen, those border pieces are just the walls,
      // which the ball already bounces off (and plays on) by itself, so leave them out
      auto onBorder = [&bbox](const GeoUtils::Edge& e)
      {
         auto same = [](double a, double b) { return std::abs(a - b) < 1e-6; };
         return (same(e.u.x, bbox.minX) && same(e.v.x, bbox.minX)) || (same(e.u.x, bbox.maxX) && same(e.v.x, bbox.maxX))
             || (same(e.u.y, bbox.minY) && same(e.v.y, bbox.minY)) || (same(e.u.y, bbox.maxY) && same(e.v.y, bbox.maxY));
      };

      for (const auto& edge : uniqueEdges)
         if (! onBorder(edge))
            built->edges.push_back(edge);
   }

   // free old ones the audio thread isn't holding anymore
   for (int i = geometryPool.size(); --i >= 0;)
      if (geometryPool.getObjectPointerUnchecked(i)->getReferenceCount() == 1)
         geometryPool.remove(i);

   geometry = built;
   geometryPool.add(built);
   geometryFifo.push(built);
}

void BallWorld::syncGeometry()
{
   const auto lines = static_cast<Params::Lines>(static_cast<int>(processor.getParamValue(Params::LINES)));
   if (lines != geometry->lines)
      rebuildGeometry();
}

// middle of a polygon by area, falls back to the average of the corners if it's flat
juce::Point<float> BallWorld::getCentroid(const std::vector<GeoUtils::Point>& vertices)
{
   double area = 0.0, cx = 0.0, cy = 0.0;
   for (size_t i = 0; i < vertices.size(); i++)
   {
      const auto& a = vertices[i];
      const auto& b = vertices[(i + 1) % vertices.size()];
      const double cross = a.x * b.y - b.x * a.y;
      area += cross;
      cx += (a.x + b.x) * cross;
      cy += (a.y + b.y) * cross;
   }

   if (std::abs(area) < 1e-9)
   {
      GeoUtils::Point sum;
      for (const auto& v : vertices)
         sum += v;
      return (sum / static_cast<double>(std::max<size_t>(1, vertices.size()))).toFloat();
   }

   return GeoUtils::Point(cx / (3.0 * area), cy / (3.0 * area)).toFloat();
}

void BallWorld::requestDrop(juce::Point<float> position)
{
   requests.push({ Request::Type::Drop, position.x, position.y });
}

void BallWorld::requestMidiBall(int noteNumber, int velocity)
{
   Request request;
   request.type = Request::Type::Midi;
   request.noteNumber = noteNumber;
   request.velocity = velocity;
   requests.push(request);
}

void BallWorld::requestClear()
{
   Request request;
   request.type = Request::Type::Clear;
   requests.push(request);
}

bool BallWorld::pullSnapshot(Snapshot& snapshot)
{
   bool gotOne = false;
   while (snapshots.pull(snapshot))
      gotOne = true;
   return gotOne;
}

int BallWorld::getNoteForHeight(float y) const
{
   // scale, range and probabilities all come from the notes settings
   return Scale::chooseByHeight(1.f - y / HEIGHT, processor.getNoteSettings());
}

//==============================================================================
void BallWorld::prepare(double newSampleRate)
{
   sampleRate = newSampleRate;
   nextStep = sampleRate / STEPS_PER_SECOND;
   latencySamples = static_cast<int>(std::ceil(sampleRate / STEPS_PER_SECOND));
   clock = 0;
   numPending = 0;
   samplesSinceSnapshot = 0.0;
}

void BallWorld::handleNoteOn(int noteNumber, int velocity, int sampleOffset)
{
   const auto note = static_cast<size_t>(juce::jlimit(0, 127, noteNumber));
   const auto press = nextKeyPress++;
   if (nextKeyPress == 0)
      nextKeyPress = 1; // 0 means no key
   keyPresses[note] = press;
   keyDownAt[note] = clock + sampleOffset;
   spawnMidiBall(noteNumber, velocity, press);
}

void BallWorld::handleNoteOff(int noteNumber, int sampleOffset)
{
   const auto note = static_cast<size_t>(juce::jlimit(0, 127, noteNumber));
   const auto press = keyPresses[note];
   if (press == 0)
      return;
   keyPresses[note] = 0;

   // its ball drops now, and holds every hit for exactly as long as this key was down
   const auto heldFor = static_cast<float>(static_cast<double>(clock + sampleOffset - keyDownAt[note]) / sampleRate);
   for (auto& ball : balls)
   {
      if (ball.keyPress == press)
      {
         ball.gateSeconds = heldFor;
         ball.keyPress = 0;
         ball.dropAt = clock + sampleOffset; // on that exact sample, not the start of this block
      }
   }
}

void BallWorld::handleRequests()
{
   Request request;
   while (requests.pull(request))
   {
      switch (request.type)
      {
         case Request::Type::Drop:  dropBall({ request.x, request.y }); break;
         case Request::Type::Midi:  spawnMidiBall(request.noteNumber, request.velocity); break;
         case Request::Type::Clear: clearBalls(); break;
      }
   }
}

Ball::Physics BallWorld::getPhysics()
{
   Ball::Physics physics;
   physics.gravity = processor.getParamValue(Params::GRAVITY);
   physics.restitution = processor.getParamValue(Params::BOUNCINESS);
   physics.drag = processor.getParamValue(Params::DRAG);
   physics.passThroughChance = processor.getParamValue(Params::PASS_THROUGH);
   physics.passThroughPlays = processor.getParamValue(Params::PASS_THROUGH_PLAYS) > 0.5f;
   physics.minBounce = processor.getParamValue(Params::MIN_BOUNCE);
   physics.random = &random;
   return physics;
}

void BallWorld::beginBlock()
{
   // new lines from the ui. edge numbers change, so nobody can be halfway through one
   Geometry::Ptr newGeometry;
   while (geometryFifo.pull(newGeometry))
   {
      audioGeometry = newGeometry;
      for (auto& ball : balls)
         ball.clearPassing();
   }

   handleRequests();
}

int BallWorld::process(int numSamples, std::array<HitEvent, MAX_HITS_PER_BLOCK>& hits)
{
   beginBlock(); // cheap if it already ran this block

   const auto physics = getPhysics();
   const float dynamics = processor.getParamValue(Params::HIT_DYNAMICS);
   const juce::Rectangle<float> bounds(0.f, 0.f, WIDTH, HEIGHT);
   const double samplesPerStep = sampleRate / STEPS_PER_SECOND;
   const float h = 1.f / static_cast<float>(STEPS_PER_SECOND);

   // every step that finishes inside this block
   while (nextStep <= static_cast<double>(numSamples))
   {
      const double stepEnd = static_cast<double>(clock) + nextStep;
      for (auto& ball : balls)
      {
         // still waiting on its key, or let go later than this step: hasn't started falling yet
         const double sinceDrop = stepEnd - static_cast<double>(ball.dropAt);
         if (ball.isWaiting() || sinceDrop <= 0.0)
            continue;

         // the first step after it's let go only covers the time since then
         const float stepLength = juce::jmin(h, static_cast<float>(sinceDrop / sampleRate));
         const auto contact = ball.step(stepLength, bounds, audioGeometry->edges, physics);
         if (! ball.shouldPlayHit(contact.speed) || numPending >= static_cast<int>(pending.size()))
            continue;

         // the note starts when the ball touched (plus the fixed latency), not when the step noticed
         const double touched = static_cast<double>(clock) + nextStep - contact.secondsAgo * sampleRate;
         pending[static_cast<size_t>(numPending++)] = { static_cast<juce::int64>(std::llround(touched)) + latencySamples,
                                                        { ball.noteNumber, ball.getHitGain(contact.speed, dynamics), ball.sound, ball.gateSeconds } };
      }

      balls.erase(std::remove_if(balls.begin(), balls.end(), [](const Ball& b) { return ! b.isAlive(); }), balls.end());
      nextStep += samplesPerStep;
   }
   nextStep -= static_cast<double>(numSamples);

   // hand over everything due in this block, keep the rest for later
   int numHits = 0;
   int kept = 0;
   const auto blockEnd = clock + numSamples;
   for (int i = 0; i < numPending; i++)
   {
      const auto& waiting = pending[static_cast<size_t>(i)];
      if (waiting.when < blockEnd && numHits < MAX_HITS_PER_BLOCK)
         hits[static_cast<size_t>(numHits++)] = { static_cast<int>(juce::jmax<juce::int64>(0, waiting.when - clock)), waiting.hit };
      else
         pending[static_cast<size_t>(kept++)] = waiting;
   }
   numPending = kept;
   clock = blockEnd;

   // contacts can be a fraction of a step out of order, the hit synth wants them in order
   std::stable_sort(hits.begin(), hits.begin() + numHits, [](const HitEvent& a, const HitEvent& b) { return a.sampleOffset < b.sampleOffset; });

   // a new picture for the canvas about 60 times a second
   samplesSinceSnapshot += numSamples;
   if (samplesSinceSnapshot >= sampleRate / 60.0)
   {
      samplesSinceSnapshot = 0.0;
      publishSnapshot();
   }

   return numHits;
}

void BallWorld::publishSnapshot()
{
   outgoing.numBalls = static_cast<int>(balls.size());
   for (size_t i = 0; i < balls.size(); i++)
   {
      const auto& ball = balls[i];
      outgoing.balls[i] = { ball.getX(), ball.getY(), ball.radius, ball.color.withMultipliedAlpha(ball.getAlpha()).getARGB(), ball.isWaiting() };
   }
   snapshots.push(outgoing);
}

void BallWorld::addBall(const Ball& ball)
{
   // drop the oldest balls if there are too many
   const auto maxBalls = static_cast<size_t>(juce::jlimit(1.f, static_cast<float>(MAX_BALLS), processor.getParamValue(Params::MAX_BALLS)));
   while (balls.size() >= maxBalls)
      balls.erase(balls.begin());

   balls.push_back(ball);
}

void BallWorld::clearBalls()
{
   balls.clear();
}

void BallWorld::setupBall(Ball& ball, int noteNumber, float sizeScale)
{
   // size with some random spread, variation 1 = anywhere from 0 to double
   const float variation = processor.getParamValue(Params::SIZE_VARIATION);
   const float spread = 1.f + variation * (random.nextFloat() * 2.f - 1.f);
   ball.radius = juce::jmax(1.f, processor.getParamValue(Params::BALL_SIZE) * sizeScale * spread);

   // launched sideways in a random direction
   const float launchSpeed = processor.getParamValue(Params::LAUNCH_SPEED);
   ball.velocity = { (random.nextFloat() * 2.f - 1.f) * launchSpeed, 0.f };

   ball.timeLeftAlive = processor.getParamValue(Params::LIFETIME);

   // each pitch class gets its own colour
   ball.noteNumber = noteNumber;
   ball.frequency = AudioUtils::getFreq(noteNumber);
   ball.color = juce::Colour::fromHSV(static_cast<float>(noteNumber % 12) / 12.f, 0.8f, 1.f, 1.f);

   // modulated, so an lfo on the ball sound can pick a different one for each new ball
   ball.sound = static_cast<Waveform>(juce::jlimit(0, static_cast<int>(Waveform::END) - 1,
                                                   static_cast<int>(processor.getParamValue(Params::BALL_SOUND))));
}

// drops a ball wherever the user clicked in ball mode
void BallWorld::dropBall(juce::Point<float> position)
{
   const bool randomPitch = processor.getParamValue(Params::PITCH_SOURCE) > 0.5f;
   const auto noteSettings = processor.getNoteSettings();
   const int note = randomPitch ? Scale::chooseRandom(random, noteSettings)
                                : Scale::chooseByHeight(1.f - position.y / HEIGHT, noteSettings);

   Ball ball;
   ball.setXY(position.x, position.y);
   setupBall(ball, note, 1.f);
   addBall(ball);
}

// midi notes have a chance of making a ball at the centre of a random voronoi cell. one from a key
// press waits there until the key is let go
void BallWorld::spawnMidiBall(int noteNumber, int velocity, juce::uint32 keyPress)
{
   const auto& sites = audioGeometry->sites;
   if (sites.empty())
      return;

   if (random.nextFloat() >= processor.getParamValue(Params::MIDI_DROP_CHANCE))
      return;

   const auto noteSettings = processor.getNoteSettings();
   const bool snap = processor.getParamValue(Params::SNAP_MIDI) > 0.5f;
   const int note = snap ? Scale::snap(noteNumber, noteSettings) : Scale::applyOctaveShift(noteNumber, noteSettings);

   Ball ball;
   const auto& centres = audioGeometry->cellCentres;
   if (! centres.empty())
   {
      const auto centre = centres[static_cast<size_t>(random.nextInt(static_cast<int>(centres.size())))];
      ball.setXY(centre.x, centre.y);
   }
   else
   {
      // under 3 sites there's no voronoi yet, so use a site
      const auto site = sites[static_cast<size_t>(random.nextInt(static_cast<int>(sites.size())))];
      ball.setXY(static_cast<float>(site.x), static_cast<float>(site.y));
   }

   // louder notes make bigger balls
   setupBall(ball, note, 0.5f + static_cast<float>(velocity) / 127.f);
   ball.noteVelocity = static_cast<float>(velocity);
   ball.keyPress = keyPress;
   addBall(ball);
}
