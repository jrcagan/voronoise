#include "Voronoise/ui/Canvas.h"
#include "Voronoise/ui/Theme.h"
#include "geometry/Delaunay.h"
#include "geometry/Voronoi.h"
#include <set>

namespace UI
{
   Canvas::Canvas(VoronoiseAudioProcessor &p) : processor(p), valueTree(p.getValueTree())
   {
       // sites can change from outside the canvas too (e.g. loading state)
       valueTree.addListener(this);
       lines = getLinesSetting();

       lastTimerMs = juce::Time::getMillisecondCounterHiRes();
       startTimerHz(60);
   }

   Canvas::~Canvas()
   {
       stopTimer();
       valueTree.removeListener(this);
   }

   void Canvas::paint(juce::Graphics &g)
   {
       const auto bounds = getLocalBounds().toFloat();

       // dark glass with a glow in the middle
       g.setGradientFill(juce::ColourGradient(Theme::screen.brighter(0.25f), bounds.getCentreX(), bounds.getCentreY(),
                                              Theme::screen.darker(0.6f), bounds.getX(), bounds.getY(), true));
       g.fillRect(bounds);

       // faint dot grid like graph paper on a terminal
       g.setColour(Theme::phosphor.withAlpha(0.06f));
       for (float x = 20.f; x < bounds.getWidth(); x += 40.f)
           for (float y = 20.f; y < bounds.getHeight(); y += 40.f)
               g.fillRect(x, y, 1.5f, 1.5f);

       if (points.size() > 2)
       {
           if (lines != Params::Lines::VoronoiOnly)
           {
               g.setColour(juce::Colours::grey.withAlpha(0.8f));
               for (const auto &t : triangles)
               {
                   g.drawLine(static_cast<float>(t.a.getX()), static_cast<float>(t.a.getY()),
                              static_cast<float>(t.b.getX()), static_cast<float>(t.b.getY()), 1.2f);
                   g.drawLine(static_cast<float>(t.b.getX()), static_cast<float>(t.b.getY()),
                              static_cast<float>(t.c.getX()), static_cast<float>(t.c.getY()), 1.2f);
                   g.drawLine(static_cast<float>(t.c.getX()), static_cast<float>(t.c.getY()),
                              static_cast<float>(t.a.getX()), static_cast<float>(t.a.getY()), 1.2f);
               }
           }

           if (lines != Params::Lines::DelaunayOnly)
           {
               // drawn twice, wide and faint then thin, for a phosphor glow
               for (auto [width, alpha] : { std::pair{ 5.f, 0.12f }, std::pair{ 1.5f, 1.f } })
               {
                   g.setColour(Theme::phosphor.withAlpha(alpha));
                   for (const auto &kv : voronoiCells)
                   {
                       const auto &verts = kv.second.vertices;
                       if (verts.size() < 2)
                           continue;

                       for (size_t i = 0; i < verts.size(); ++i)
                       {
                           const auto &a = verts[i];
                           const auto &b = verts[(i + 1) % verts.size()];
                           g.drawLine(static_cast<float>(a.getX()), static_cast<float>(a.getY()),
                                      static_cast<float>(b.getX()), static_cast<float>(b.getY()), width);
                       }
                   }
               }
           }
       }

       g.setColour(juce::Colours::white);
       for (const auto &point : points)
           g.drawEllipse(static_cast<float>(point.getX()) - 2.5f, static_cast<float>(point.getY()) - 2.5f, 5.f, 5.f, 1.5f);

       for (const auto &ball : balls)
       {
           const auto colour = ball.color.withMultipliedAlpha(ball.getAlpha());
           const auto circle = juce::Rectangle<float>(ball.radius * 2.f, ball.radius * 2.f).withCentre({ ball.getX(), ball.getY() });
           g.setColour(colour.withMultipliedAlpha(0.25f));
           g.fillEllipse(circle.expanded(ball.radius * 0.6f));
           g.setColour(colour);
           g.fillEllipse(circle);
       }

       if (points.empty() && balls.empty())
       {
           g.setFont(Theme::monoFont(14.f));
           g.setColour(Theme::phosphor.withAlpha(0.7f));
           g.drawText(mode == Mode::DrawSites ? "> DOUBLE CLICK TO PLACE SITES_" : "> CLICK TO DROP BALLS_",
                      getLocalBounds(), juce::Justification::centred);
       }

       // scanlines
       g.setColour(juce::Colours::black.withAlpha(0.18f));
       for (int y = 0; y < getHeight(); y += 3)
           g.drawHorizontalLine(y, 0.f, bounds.getRight());
   }

   void Canvas::resized()
   {
       // voronoi is clipped to the screen so it has to be redone when the size changes
       rebuildGeometry();
   }

   void Canvas::setMode(Mode newMode)
   {
       mode = newMode;
       repaint();
   }

   void Canvas::setSound(Waveform newSound)
   {
       if (newSound >= Waveform::END)
           return;

       if (auto *param = processor.apvts.getParameter(Params::BALL_SOUND))
       {
           param->beginChangeGesture();
           param->setValueNotifyingHost(param->convertTo0to1(static_cast<float>(newSound)));
           param->endChangeGesture();
       }
   }

   Waveform Canvas::getSound() const
   {
       // modulated, so an lfo on the ball sound picks a different wave for each new ball
       return static_cast<Waveform>(juce::jlimit(0, static_cast<int>(Waveform::END) - 1,
                                                 static_cast<int>(processor.getParamValue(Params::BALL_SOUND))));
   }

   void Canvas::clearBalls()
   {
       balls.clear();
       repaint();
   }

   void Canvas::clearSites()
   {
       valueTree.getChildWithName("Sites").removeAllChildren(nullptr);
   }

   int Canvas::getNoteForHeight(float y) const
   {
       // scale, range and probabilities all come from the notes settings
       const float height = juce::jmax(1.f, static_cast<float>(getHeight()));
       return Scale::chooseByHeight(1.f - y / height, processor.getNoteSettings());
   }

   Params::Lines Canvas::getLinesSetting() const
   {
       return static_cast<Params::Lines>(static_cast<int>(processor.getParamValue(Params::LINES)));
   }

   Ball::Physics Canvas::getPhysics()
   {
       Ball::Physics physics;
       physics.gravity = processor.getParamValue(Params::GRAVITY);
       physics.restitution = processor.getParamValue(Params::BOUNCINESS);
       physics.drag = processor.getParamValue(Params::DRAG);
       physics.passThroughChance = processor.getParamValue(Params::PASS_THROUGH);
       physics.passThroughPlays = processor.getParamValue(Params::PASS_THROUGH_PLAYS) > 0.5f;
       physics.random = &random;
       return physics;
   }

   void Canvas::mouseDown(const juce::MouseEvent &event)
   {
       if (mode == Mode::DropBalls)
       {
           dropBall(event.position);
       }
   }

   void Canvas::mouseDoubleClick(const juce::MouseEvent &event)
   {
       if (mode != Mode::DrawSites)
           return;

       // the listener picks this up and rebuilds the voronoi
       auto sitesTree = valueTree.getChildWithName("Sites");
       sitesTree.appendChild(
           juce::ValueTree("Site")
               .setProperty("x", static_cast<double>(event.x), nullptr)
               .setProperty("y", static_cast<double>(event.y), nullptr),
           nullptr);
   }

   void Canvas::valueTreeChildAdded(juce::ValueTree &, juce::ValueTree &)
   {
       rebuildGeometry();
   }

   void Canvas::valueTreeChildRemoved(juce::ValueTree &, juce::ValueTree &, int)
   {
       rebuildGeometry();
   }

   void Canvas::rebuildGeometry()
   {
       auto sitesTree = valueTree.getChildWithName("Sites");
       points.clear();

       for (int i = 0; i < sitesTree.getNumChildren(); i++)
       {
           auto site = sitesTree.getChild(i);
           points.push_back(GeoUtils::Point(static_cast<double>(site["x"]), static_cast<double>(site["y"])));
       }

       triangles.clear();
       voronoiCells.clear();
       edges.clear();
       cellCentres.clear();

       // edge indices are about to change, so balls can't be halfway through any of them
       for (auto &ball : balls)
           ball.passingEdges.clear();

       if (points.size() > 2)
       {
           triangles = Delaunay::triangulate(points);

           GeoUtils::BBox bbox;
           auto box = getLocalBounds().toDouble();
           bbox.minX = box.getX();
           bbox.minY = box.getY();
           bbox.maxX = box.getRight();
           bbox.maxY = box.getBottom();

           voronoiCells = Voronoi::getCells(triangles, bbox);

           for (const auto &kv : voronoiCells)
               cellCentres.push_back(getCentroid(kv.second.vertices));

           // balls bounce off whichever lines are switched on, the set drops duplicates
           std::set<GeoUtils::Edge, GeoUtils::EdgeComparator> uniqueEdges;
           if (lines != Params::Lines::DelaunayOnly)
           {
               for (const auto &edge : Voronoi::getEdges(triangles, bbox))
                   uniqueEdges.insert(edge);
           }
           if (lines != Params::Lines::VoronoiOnly)
           {
               for (const auto &t : triangles)
               {
                   uniqueEdges.insert({ t.a, t.b });
                   uniqueEdges.insert({ t.b, t.c });
                   uniqueEdges.insert({ t.c, t.a });
               }
           }

           // voronoi cells get clipped to the window, those border pieces are just the walls,
           // which the ball already bounces off (and plays on) by itself, so leave them out
           auto onBorder = [&bbox](const GeoUtils::Edge &e)
           {
               auto same = [](double a, double b) { return std::abs(a - b) < 1e-6; };
               return (same(e.u.x, bbox.minX) && same(e.v.x, bbox.minX)) || (same(e.u.x, bbox.maxX) && same(e.v.x, bbox.maxX))
                   || (same(e.u.y, bbox.minY) && same(e.v.y, bbox.minY)) || (same(e.u.y, bbox.maxY) && same(e.v.y, bbox.maxY));
           };

           for (const auto &edge : uniqueEdges)
           {
               if (! onBorder(edge))
                   edges.push_back(edge);
           }
       }

       repaint();
   }

   void Canvas::syncWithProcessor()
   {
       // loading a project swaps the whole state out, so follow it to the new sites
       if (valueTree != processor.getValueTree())
       {
           valueTree.removeListener(this);
           valueTree = processor.getValueTree();
           valueTree.addListener(this);
           rebuildGeometry();
       }

       const auto newLines = getLinesSetting();
       if (newLines != lines)
       {
           lines = newLines;
           rebuildGeometry();
       }
   }

   // middle of a polygon by area, falls back to the average of the corners if it's flat
   juce::Point<float> Canvas::getCentroid(const std::vector<GeoUtils::Point> &vertices)
   {
       double area = 0.0, cx = 0.0, cy = 0.0;
       for (size_t i = 0; i < vertices.size(); i++)
       {
           const auto &a = vertices[i];
           const auto &b = vertices[(i + 1) % vertices.size()];
           const double cross = a.x * b.y - b.x * a.y;
           area += cross;
           cx += (a.x + b.x) * cross;
           cy += (a.y + b.y) * cross;
       }

       if (std::abs(area) < 1e-9)
       {
           GeoUtils::Point sum;
           for (const auto &v : vertices)
               sum += v;
           return (sum / static_cast<double>(std::max<size_t>(1, vertices.size()))).toFloat();
       }

       return GeoUtils::Point(cx / (3.0 * area), cy / (3.0 * area)).toFloat();
   }

   // runs at 60hz: picks up notes from the audio thread and moves the balls
   void Canvas::timerCallback()
   {
       syncWithProcessor();

       VoronoiseAudioProcessor::NoteEvent event;

       while (processor.noteEventFifo.pull(event))
       {
           spawnBall(event.noteNumber, event.velocity);
       }

       const auto now = juce::Time::getMillisecondCounterHiRes();
       // cap dt so a stall (window dragged, debugger) doesn't launch the balls through everything
       const auto dt = juce::jmin(static_cast<float>((now - lastTimerMs) / 1000.0), 1.f / 30.f);
       lastTimerMs = now;

       if (! balls.empty())
       {
           updateBalls(dt);
           repaint();
       }
   }

   void Canvas::updateBalls(float dt)
   {
       const auto bounds = getLocalBounds().toFloat();
       const auto physics = getPhysics();

       for (auto &ball : balls)
       {
           const float impact = ball.update(dt, bounds, edges, physics);

           if (ball.shouldPlayHit(impact))
           {
               processor.hitFifo.push({ ball.noteNumber, ball.getHitGain(impact), ball.sound });
           }
       }

       balls.erase(std::remove_if(balls.begin(), balls.end(), [](const Ball &b) { return ! b.isAlive(); }),
                   balls.end());
   }

   void Canvas::addBall(Ball ball)
   {
       // drop the oldest balls if there are too many
       const auto maxBalls = static_cast<size_t>(juce::jmax(1.f, processor.getParamValue(Params::MAX_BALLS)));
       while (balls.size() >= maxBalls)
           balls.erase(balls.begin());

       balls.push_back(ball);
       repaint();
   }

   void Canvas::setupBall(Ball &ball, int noteNumber, float sizeScale)
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
       ball.sound = getSound();
   }

   // drops a ball wherever the user clicked in ball mode
   void Canvas::dropBall(juce::Point<float> position)
   {
       const bool randomPitch = processor.getParamValue(Params::PITCH_SOURCE) > 0.5f;
       const int note = randomPitch ? Scale::chooseRandom(random, processor.getNoteSettings())
                                    : getNoteForHeight(position.y);

       Ball ball;
       ball.setXY(position.x, position.y);
       setupBall(ball, note, 1.f);

       addBall(ball);
   }

   // midi notes have a chance of dropping a ball from the centre of a random voronoi cell
   void Canvas::spawnBall(int noteNumber, int velocity)
   {
       auto sitesTree = valueTree.getChildWithName("Sites");

       // no sites to spawn from yet
       if (sitesTree.getNumChildren() == 0)
           return;

       if (random.nextFloat() >= processor.getParamValue(Params::MIDI_DROP_CHANCE))
           return;

       const auto noteSettings = processor.getNoteSettings();
       const bool snap = processor.getParamValue(Params::SNAP_MIDI) > 0.5f;
       const int note = snap ? Scale::snap(noteNumber, noteSettings) : Scale::applyOctaveShift(noteNumber, noteSettings);

       Ball ball;

       if (! cellCentres.empty())
       {
           const auto centre = cellCentres[static_cast<size_t>(random.nextInt(static_cast<int>(cellCentres.size())))];
           ball.setXY(centre.x, centre.y);
       }
       else
       {
           // under 3 sites there's no voronoi yet, so use a site
           auto point = sitesTree.getChild(random.nextInt(sitesTree.getNumChildren()));
           ball.setX(static_cast<float>(point.getProperty("x")));
           ball.setY(static_cast<float>(point.getProperty("y")));
       }

       // louder notes make bigger balls
       setupBall(ball, note, 0.5f + static_cast<float>(velocity) / 127.f);
       ball.noteVelocity = static_cast<float>(velocity);

       addBall(ball);
   }
}
