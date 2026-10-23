#include <gtest/gtest.h>
#include <memory>

#include "Voronoise/PluginProcessor.h"
#include "Voronoise/PluginEditor.h"

namespace
{
   juce::MidiBuffer makeNotes(int startNote, int count, bool noteOn)
   {
      juce::MidiBuffer midi;
      for (int i = 0; i < count; i++)
      {
         auto msg = noteOn ? juce::MidiMessage::noteOn(1, startNote + i, static_cast<juce::uint8>(100))
                           : juce::MidiMessage::noteOff(1, startNote + i);
         midi.addEvent(msg, i * 8);
      }
      return midi;
   }
}

TEST(ProcessorTest, ProcessBlockWithNotesDoesNotCrash)
{
   juce::ScopedJuceInitialiser_GUI juceInit;
   VoronoiseAudioProcessor processor;
   processor.prepareToPlay(48000.0, 512);

   juce::AudioBuffer<float> buffer(2, 512);

   for (int block = 0; block < 200; block++)
   {
      auto midi = makeNotes(block % 100, 20, block % 2 == 0);
      processor.processBlock(buffer, midi);
   }

   for (int c = 0; c < buffer.getNumChannels(); c++)
      for (int s = 0; s < buffer.getNumSamples(); s++)
         EXPECT_TRUE(std::isfinite(buffer.getSample(c, s)));
}

TEST(ProcessorTest, NotesBeforePrepareToPlayDoNotCrash)
{
   juce::ScopedJuceInitialiser_GUI juceInit;
   VoronoiseAudioProcessor processor;

   juce::AudioBuffer<float> buffer(2, 512);
   auto midi = makeNotes(60, 10, true);
   processor.processBlock(buffer, midi);
}

TEST(ProcessorTest, EditorHandlesNotesWithAndWithoutSites)
{
   juce::ScopedJuceInitialiser_GUI juceInit;
   VoronoiseAudioProcessor processor;
   processor.prepareToPlay(48000.0, 512);

   auto editor = std::unique_ptr<juce::AudioProcessorEditor>(processor.createEditor());
   auto* voronoiseEditor = dynamic_cast<VoronoiseAudioProcessorEditor*>(editor.get());
   ASSERT_NE(voronoiseEditor, nullptr);

   // no sites yet
   voronoiseEditor->spawnBall(60, 100);

   auto sites = processor.getValueTree().getChildWithName("Sites");
   for (int i = 0; i < 5; i++)
   {
      sites.appendChild(juce::ValueTree("Site")
                            .setProperty("x", 20.0 + i * 50.0, nullptr)
                            .setProperty("y", 30.0 + (i % 2) * 100.0, nullptr),
                        nullptr);
   }

   juce::AudioBuffer<float> buffer(2, 512);
   for (int block = 0; block < 50; block++)
   {
      auto midi = makeNotes(40 + block % 40, 4, true);
      processor.processBlock(buffer, midi);
      voronoiseEditor->spawnBall(40 + block % 40, 100);
   }

   // paint runs the delaunay/voronoi path with the sites above
   juce::Image image(juce::Image::ARGB, 400, 300, true);
   juce::Graphics g(image);
   voronoiseEditor->paint(g);

   editor.reset();

   // notes queued after the editor is gone must not touch it
   auto midi = makeNotes(60, 10, true);
   processor.processBlock(buffer, midi);
}
