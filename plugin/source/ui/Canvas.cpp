#include "Voronoise/ui/Canvas.h"
#include "Voronoise/ui/Theme.h"

namespace UI
{
   Canvas::Canvas(VoronoiseAudioProcessor &p) : processor(p), snapshot(std::make_unique<BallWorld::Snapshot>())
   {
       startTimerHz(60);
   }

   Canvas::~Canvas()
   {
       stopTimer();
   }

   void Canvas::paint(juce::Graphics &g)
   {
       const auto bounds = getLocalBounds().toFloat();
       const auto geometry = processor.getWorld().getGeometry();

       // dark glass with a faint blue glow in the middle
       g.setGradientFill(juce::ColourGradient(Theme::screen.interpolatedWith(Theme::accent, 0.08f), bounds.getCentreX(), bounds.getCentreY(),
                                              Theme::screen.darker(0.6f), bounds.getX(), bounds.getY(), true));
       g.fillRect(bounds);

       // faint dot grid like graph paper on a terminal
       g.setColour(Theme::phosphor.withAlpha(0.06f));
       for (float x = 20.f; x < bounds.getWidth(); x += 40.f)
           for (float y = 20.f; y < bounds.getHeight(); y += 40.f)
               g.fillRect(x, y, 1.5f, 1.5f);

       // delaunay (in whichever metric) under the voronoi
       if (geometry->lines != Params::Lines::VoronoiOnly)
       {
           g.setColour(Theme::labelDim.withAlpha(0.8f));
           for (const auto &edge : geometry->delaunayLines)
               g.drawLine(static_cast<float>(edge.u.x), static_cast<float>(edge.u.y),
                          static_cast<float>(edge.v.x), static_cast<float>(edge.v.y), 1.2f);
       }

       if (geometry->lines != Params::Lines::DelaunayOnly)
       {
           // the cell walls as paths (manhattan and hilbert ones bend), wide and faint then thin for a glow
           juce::Path walls;
           for (const auto &line : geometry->voronoiLines)
           {
               if (line.size() < 2)
                   continue;
               walls.startNewSubPath(line.front().toFloat());
               for (size_t i = 1; i < line.size(); i++)
                   walls.lineTo(line[i].toFloat());
           }
           for (auto [width, alpha] : { std::pair{ 5.f, 0.12f }, std::pair{ 1.5f, 1.f } })
           {
               g.setColour(Theme::phosphor.withAlpha(alpha));
               g.strokePath(walls, juce::PathStrokeType(width, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
           }
       }

       // straight from the state rather than the built lines, so a dragged site stays under the mouse even
       // while its lines are still being worked out
       const auto sitesTree = processor.getValueTree().getChildWithName("Sites");
       for (int i = 0; i < sitesTree.getNumChildren(); i++)
       {
           const juce::Point<float> point { static_cast<float>(sitesTree.getChild(i)["x"]), static_cast<float>(sitesTree.getChild(i)["y"]) };
           g.setColour(Theme::highlight);
           g.drawEllipse(juce::Rectangle<float>(5.f, 5.f).withCentre(point), 1.5f);

           // a ring around the one under the mouse (or being dragged), so it's clear it can be grabbed
           if (mode == Mode::DrawSites && (i == draggedSite || (draggedSite < 0 && i == hoveredSite)))
           {
               g.setColour(Theme::highlight.withAlpha(i == draggedSite ? 0.9f : 0.5f));
               g.drawEllipse(juce::Rectangle<float>(GRAB_RADIUS * 2.f, GRAB_RADIUS * 2.f).withCentre(point), 1.f);
           }
       }

       for (int i = 0; i < snapshot->numBalls; i++)
       {
           const auto &ball = snapshot->balls[static_cast<size_t>(i)];
           const auto colour = juce::Colour(ball.argb);
           const auto circle = juce::Rectangle<float>(ball.radius * 2.f, ball.radius * 2.f).withCentre({ ball.x, ball.y });
           g.setColour(colour.withMultipliedAlpha(0.25f));
           g.fillEllipse(circle.expanded(ball.radius * 0.6f));
           g.setColour(colour);
           g.fillEllipse(circle);

           // waiting for its key to be let go: a ring around it
           if (ball.waiting)
           {
               g.setColour(colour.withAlpha(0.8f));
               g.drawEllipse(circle.expanded(ball.radius * 0.9f + 2.f), 1.2f);
           }
       }

       if (geometry->sites.empty() && snapshot->numBalls == 0)
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

   void Canvas::setMode(Mode newMode)
   {
       endDrag();
       mode = newMode;
       hoveredSite = -1;
       setMouseCursor(juce::MouseCursor::NormalCursor);
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
       return static_cast<Waveform>(juce::jlimit(0, static_cast<int>(Waveform::END) - 1,
                                                 static_cast<int>(processor.getParamValue(Params::BALL_SOUND))));
   }

   int Canvas::getNoteForHeight(float y) const
   {
       return processor.getWorld().getNoteForHeight(y);
   }

   void Canvas::spawnBall(int noteNumber, int velocity)
   {
       processor.getWorld().requestMidiBall(noteNumber, velocity);
   }

   void Canvas::dropBall(juce::Point<float> position)
   {
       processor.getWorld().requestDrop(position);
   }

   void Canvas::clearBalls()
   {
       processor.getWorld().requestClear();
   }

   void Canvas::clearSites()
   {
       processor.getValueTree().getChildWithName("Sites").removeAllChildren(nullptr);
   }

   void Canvas::mouseDown(const juce::MouseEvent &event)
   {
       if (mode == Mode::DropBalls)
           dropBall(event.position);
       else
           beginDrag(event.position);
   }

   void Canvas::mouseDrag(const juce::MouseEvent &event)
   {
       if (isDragging())
           dragTo(event.position);
   }

   void Canvas::mouseUp(const juce::MouseEvent &event)
   {
       endDrag();
       mouseMove(event);
   }

   void Canvas::mouseMove(const juce::MouseEvent &event)
   {
       const int site = mode == Mode::DrawSites ? findSiteAt(event.position) : -1;
       if (site != hoveredSite)
       {
           hoveredSite = site;
           setMouseCursor(site >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
           repaint();
       }
   }

   void Canvas::mouseExit(const juce::MouseEvent &)
   {
       hoveredSite = -1;
       repaint();
   }

   void Canvas::mouseDoubleClick(const juce::MouseEvent &event)
   {
       // double clicking on a site is grabbing it, not putting another one on top
       if (mode != Mode::DrawSites || findSiteAt(event.position) >= 0)
           return;

       // the processor hears about this and rebuilds the lines
       processor.getValueTree().getChildWithName("Sites").appendChild(
           juce::ValueTree("Site")
               .setProperty("x", static_cast<double>(event.x), nullptr)
               .setProperty("y", static_cast<double>(event.y), nullptr),
           nullptr);
   }

   int Canvas::findSiteAt(juce::Point<float> position) const
   {
       // the closest one within reach, so sites close together can still be picked apart
       const auto sitesTree = processor.getValueTree().getChildWithName("Sites");
       int closest = -1;
       float closestDistance = GRAB_RADIUS;
       for (int i = 0; i < sitesTree.getNumChildren(); i++)
       {
           const juce::Point<float> site { static_cast<float>(sitesTree.getChild(i)["x"]), static_cast<float>(sitesTree.getChild(i)["y"]) };
           const float distance = site.getDistanceFrom(position);
           if (distance <= closestDistance)
           {
               closestDistance = distance;
               closest = i;
           }
       }
       return closest;
   }

   bool Canvas::beginDrag(juce::Point<float> position)
   {
       draggedSite = findSiteAt(position);
       if (draggedSite < 0)
           return false;

       const auto site = processor.getValueTree().getChildWithName("Sites").getChild(draggedSite);
       grabOffset = juce::Point<float>(static_cast<float>(site["x"]), static_cast<float>(site["y"])) - position;
       repaint();
       return true;
   }

   void Canvas::dragTo(juce::Point<float> position)
   {
       auto site = processor.getValueTree().getChildWithName("Sites").getChild(draggedSite);
       if (! site.isValid())
       {
           // cleared (or the state was swapped) mid drag
           draggedSite = -1;
           return;
       }

       // stays on the screen. each property change asks for the lines to be rebuilt in the background
       const auto target = position + grabOffset;
       site.setProperty("x", static_cast<double>(juce::jlimit(0.f, BallWorld::WIDTH, target.x)), nullptr);
       site.setProperty("y", static_cast<double>(juce::jlimit(0.f, BallWorld::HEIGHT, target.y)), nullptr);
       repaint();
   }

   void Canvas::endDrag()
   {
       draggedSite = -1;
       repaint();
   }

   void Canvas::syncWithProcessor()
   {
       processor.getWorld().syncGeometry();
       processor.getWorld().pullSnapshot(*snapshot);
   }

   void Canvas::timerCallback()
   {
       syncWithProcessor();
       repaint();
   }
}
