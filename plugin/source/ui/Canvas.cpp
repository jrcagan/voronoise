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

       if (geometry->sites.size() > 2)
       {
           if (geometry->lines != Params::Lines::VoronoiOnly)
           {
               g.setColour(Theme::labelDim.withAlpha(0.8f));
               for (const auto &t : geometry->triangles)
               {
                   g.drawLine(static_cast<float>(t.a.getX()), static_cast<float>(t.a.getY()),
                              static_cast<float>(t.b.getX()), static_cast<float>(t.b.getY()), 1.2f);
                   g.drawLine(static_cast<float>(t.b.getX()), static_cast<float>(t.b.getY()),
                              static_cast<float>(t.c.getX()), static_cast<float>(t.c.getY()), 1.2f);
                   g.drawLine(static_cast<float>(t.c.getX()), static_cast<float>(t.c.getY()),
                              static_cast<float>(t.a.getX()), static_cast<float>(t.a.getY()), 1.2f);
               }
           }

           if (geometry->lines != Params::Lines::DelaunayOnly)
           {
               // drawn twice, wide and faint then thin, for a phosphor glow
               for (auto [width, alpha] : { std::pair{ 5.f, 0.12f }, std::pair{ 1.5f, 1.f } })
               {
                   g.setColour(Theme::phosphor.withAlpha(alpha));
                   for (const auto &kv : geometry->cells)
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

       g.setColour(Theme::highlight);
       for (const auto &point : geometry->sites)
           g.drawEllipse(static_cast<float>(point.getX()) - 2.5f, static_cast<float>(point.getY()) - 2.5f, 5.f, 5.f, 1.5f);

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
   }

   void Canvas::mouseDoubleClick(const juce::MouseEvent &event)
   {
       if (mode != Mode::DrawSites)
           return;

       // the processor hears about this and rebuilds the lines
       processor.getValueTree().getChildWithName("Sites").appendChild(
           juce::ValueTree("Site")
               .setProperty("x", static_cast<double>(event.x), nullptr)
               .setProperty("y", static_cast<double>(event.y), nullptr),
           nullptr);
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
