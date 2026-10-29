#include "synth/Wavetable.h"
#include <cmath>

namespace
{
   constexpr int FFT_ORDER = 11; // 2048
   static_assert((1 << FFT_ORDER) == Wavetable::FRAME_SIZE);

   std::vector<float> makeFrame(const std::function<float(float)>& shape)
   {
      std::vector<float> frame(Wavetable::FRAME_SIZE);
      for (int i = 0; i < Wavetable::FRAME_SIZE; i++)
         frame[static_cast<size_t>(i)] = shape(static_cast<float>(i) / Wavetable::FRAME_SIZE);
      return frame;
   }

   float sine(float t) { return std::sin(juce::MathConstants<float>::twoPi * t); }
   float triangle(float t) { return 1.f - 4.f * std::abs(std::fmod(t + 0.25f, 1.f) - 0.5f); } // starts at 0 going up like the sine
   float saw(float t) { return t < 0.5f ? 2.f * t : 2.f * t - 2.f; }                           // also starts at 0
   float pulse(float t, float width) { return t < width ? 1.f : -1.f; }
}

juce::StringArray Wavetable::getPresetNames()
{
   return { "Basic shapes", "Sine", "Saw", "Square", "Pulse sweep", "Harmonic sweep", "Random" };
}

Wavetable::Ptr Wavetable::createPreset(Preset preset, juce::Random* random)
{
   std::vector<std::vector<float>> table;
   switch (preset)
   {
      case Preset::BasicShapes:
         table = { makeFrame(sine), makeFrame(triangle), makeFrame(saw), makeFrame([](float t) { return pulse(t, 0.5f); }) };
         break;
      case Preset::Sine:
         table = { makeFrame(sine) };
         break;
      case Preset::Saw:
         table = { makeFrame(saw) };
         break;
      case Preset::Square:
         table = { makeFrame([](float t) { return pulse(t, 0.5f); }) };
         break;
      case Preset::PulseSweep:
         // the old pulse width knob, as a sweep through the table
         for (int i = 0; i < 16; i++)
         {
            const float width = 0.5f - 0.45f * static_cast<float>(i) / 15.f;
            table.push_back(makeFrame([width](float t) { return pulse(t, width); }));
         }
         break;
      case Preset::HarmonicSweep:
         // one more saw harmonic per frame
         for (int frame = 0; frame < 16; frame++)
         {
            table.push_back(makeFrame([frame](float t) {
               float value = 0.f;
               for (int h = 1; h <= frame + 1; h++)
                  value += std::sin(juce::MathConstants<float>::twoPi * t * static_cast<float>(h)) / static_cast<float>(h);
               return value * 0.6f;
            }));
         }
         break;
      case Preset::Random:
      {
         juce::Random fallback;
         auto& r = random != nullptr ? *random : fallback;
         // a few random harmonics per frame so it's lumpy but not noise
         for (int frame = 0; frame < 8; frame++)
         {
            std::array<float, 12> amps {}, phases {};
            for (size_t h = 0; h < amps.size(); h++)
            {
               amps[h] = r.nextFloat() / static_cast<float>(h + 1);
               phases[h] = r.nextFloat();
            }
            table.push_back(makeFrame([amps, phases](float t) {
               float value = 0.f;
               for (size_t h = 0; h < amps.size(); h++)
                  value += amps[h] * std::sin(juce::MathConstants<float>::twoPi * (t * static_cast<float>(h + 1) + phases[h]));
               return value;
            }));
         }
         break;
      }
      case Preset::END:
      default:
         table = { makeFrame(sine) };
         break;
   }

   // keep everything at the same loudness
   float peak = 0.f;
   for (auto& frame : table)
      for (float v : frame)
         peak = juce::jmax(peak, std::abs(v));
   if (peak > 0.f)
      for (auto& frame : table)
         for (auto& v : frame)
            v /= peak;

   return new Wavetable(table, getPresetNames()[static_cast<int>(preset)]);
}

std::vector<float> Wavetable::resample(const std::vector<float>& input, int size)
{
   std::vector<float> output(static_cast<size_t>(size), 0.f);
   if (input.empty())
      return output;

   for (int i = 0; i < size; i++)
   {
      const double position = static_cast<double>(i) * static_cast<double>(input.size()) / size;
      const auto index = static_cast<size_t>(position);
      const auto next = (index + 1) % input.size();
      const auto frac = static_cast<float>(position - static_cast<double>(index));
      output[static_cast<size_t>(i)] = input[index] + frac * (input[next] - input[index]);
   }
   return output;
}

Wavetable::Wavetable(const std::vector<std::vector<float>>& rawFrames, const juce::String& tableName) : name(tableName)
{
   for (const auto& raw : rawFrames)
   {
      if (static_cast<int>(frames.size()) >= MAX_FRAMES)
         break;
      frames.push_back(buildFrame(static_cast<int>(raw.size()) == FRAME_SIZE ? raw : resample(raw, FRAME_SIZE)));
   }

   if (frames.empty())
      frames.push_back(buildFrame(makeFrame(sine)));
}

Wavetable::Wavetable(const Wavetable& base, int frameIndex, const std::vector<float>& newFrame)
    : frames(base.frames), name(base.name)
{
   if (frameIndex >= 0 && frameIndex < static_cast<int>(frames.size()))
      frames[static_cast<size_t>(frameIndex)] = buildFrame(static_cast<int>(newFrame.size()) == FRAME_SIZE ? newFrame : resample(newFrame, FRAME_SIZE));
}

Wavetable::Ptr Wavetable::fromAudio(const juce::AudioBuffer<float>& audio, const juce::String& tableName)
{
   const int length = audio.getNumSamples();
   if (length < 2 || audio.getNumChannels() == 0)
      return nullptr;

   // mono mix
   std::vector<float> mono(static_cast<size_t>(length), 0.f);
   for (int c = 0; c < audio.getNumChannels(); c++)
      for (int i = 0; i < length; i++)
         mono[static_cast<size_t>(i)] += audio.getSample(c, i) / static_cast<float>(audio.getNumChannels());

   std::vector<std::vector<float>> table;
   if (length <= FRAME_SIZE)
   {
      // a single cycle, stretched to fit
      table.push_back(resample(mono, FRAME_SIZE));
   }
   else
   {
      // frames spread evenly from the start to the end of the file
      const int numFrames = juce::jlimit(1, MAX_FRAMES, length / FRAME_SIZE);
      for (int f = 0; f < numFrames; f++)
      {
         const int start = numFrames == 1 ? 0 : static_cast<int>(static_cast<juce::int64>(f) * (length - FRAME_SIZE) / (numFrames - 1));
         table.emplace_back(mono.begin() + start, mono.begin() + start + FRAME_SIZE);
      }
   }

   float peak = 0.f;
   for (auto& frame : table)
      for (float v : frame)
         peak = juce::jmax(peak, std::abs(v));
   if (peak < 1e-6f)
      return nullptr; // silent file
   for (auto& frame : table)
      for (auto& v : frame)
         v /= peak;

   return new Wavetable(table, tableName);
}

std::shared_ptr<const Wavetable::Frame> Wavetable::buildFrame(const std::vector<float>& raw)
{
   auto frame = std::make_shared<Frame>();
   frame->raw = raw;

   juce::dsp::FFT fft(FFT_ORDER);
   std::vector<float> spectrum(static_cast<size_t>(FRAME_SIZE * 2), 0.f);
   std::copy(raw.begin(), raw.end(), spectrum.begin());
   fft.performRealOnlyForwardTransform(spectrum.data(), true);

   // the spectrum is interleaved re/im, bin k is a harmonic k. bin 0 is dc, which always goes
   std::vector<float> work(spectrum.size());
   for (int level = 0; level < NUM_LEVELS; level++)
   {
      const int maxHarmonic = (FRAME_SIZE / 2) >> level;
      std::fill(work.begin(), work.end(), 0.f);
      for (int bin = 1; bin <= maxHarmonic && bin < FRAME_SIZE / 2; bin++)
      {
         work[static_cast<size_t>(bin * 2)] = spectrum[static_cast<size_t>(bin * 2)];
         work[static_cast<size_t>(bin * 2 + 1)] = spectrum[static_cast<size_t>(bin * 2 + 1)];
         // mirror for the inverse transform
         work[static_cast<size_t>((FRAME_SIZE - bin) * 2)] = spectrum[static_cast<size_t>(bin * 2)];
         work[static_cast<size_t>((FRAME_SIZE - bin) * 2 + 1)] = -spectrum[static_cast<size_t>(bin * 2 + 1)];
      }
      fft.performRealOnlyInverseTransform(work.data());

      auto& table = frame->levels[static_cast<size_t>(level)];
      table.assign(work.begin(), work.begin() + FRAME_SIZE);
      table.push_back(table.front()); // wrap sample so interpolation never reads past the end
   }

   return frame;
}

const std::vector<float>& Wavetable::getFrame(int index) const
{
   return frames[static_cast<size_t>(juce::jlimit(0, getNumFrames() - 1, index))]->raw;
}

std::vector<std::vector<float>> Wavetable::getFrames() const
{
   std::vector<std::vector<float>> raw;
   for (const auto& frame : frames)
      raw.push_back(frame->raw);
   return raw;
}

int Wavetable::pickLevel(double increment)
{
   // the most harmonics that stay under nyquist at this pitch
   const double safeHarmonics = 0.5 / juce::jmax(1e-9, increment);
   int level = 0;
   while (level < NUM_LEVELS - 1 && static_cast<double>((FRAME_SIZE / 2) >> level) > safeHarmonics)
      level++;
   return level;
}

float Wavetable::getSample(double phase, double increment, float position) const
{
   const int level = pickLevel(increment);

   const float framePosition = juce::jlimit(0.f, 1.f, position) * static_cast<float>(getNumFrames() - 1);
   const int frameA = static_cast<int>(framePosition);
   const int frameB = juce::jmin(frameA + 1, getNumFrames() - 1);
   const float morph = framePosition - static_cast<float>(frameA);

   const double index = (phase - std::floor(phase)) * FRAME_SIZE;
   const auto i = static_cast<size_t>(index);
   const auto frac = static_cast<float>(index - static_cast<double>(i));

   auto read = [&](int frameIndex) {
      const auto& table = frames[static_cast<size_t>(frameIndex)]->levels[static_cast<size_t>(level)];
      return table[i] + frac * (table[i + 1] - table[i]);
   };

   const float a = read(frameA);
   return frameB == frameA ? a : a + morph * (read(frameB) - a);
}

std::vector<float> Wavetable::getFrameAt(float position, int numPoints) const
{
   std::vector<float> points(static_cast<size_t>(juce::jmax(2, numPoints)));
   const float framePosition = juce::jlimit(0.f, 1.f, position) * static_cast<float>(getNumFrames() - 1);
   const int frameA = static_cast<int>(framePosition);
   const int frameB = juce::jmin(frameA + 1, getNumFrames() - 1);
   const float morph = framePosition - static_cast<float>(frameA);

   const auto& a = frames[static_cast<size_t>(frameA)]->raw;
   const auto& b = frames[static_cast<size_t>(frameB)]->raw;
   for (size_t p = 0; p < points.size(); p++)
   {
      const auto i = juce::jmin(static_cast<size_t>(FRAME_SIZE - 1), p * static_cast<size_t>(FRAME_SIZE) / (points.size() - 1));
      points[p] = a[i] + morph * (b[i] - a[i]);
   }
   return points;
}

juce::MemoryBlock Wavetable::toMemoryBlock() const
{
   juce::MemoryBlock block;
   for (const auto& frame : frames)
      block.append(frame->raw.data(), frame->raw.size() * sizeof(float));
   return block;
}

Wavetable::Ptr Wavetable::fromMemoryBlock(const juce::MemoryBlock& block, const juce::String& tableName)
{
   const auto floatsPerFrame = static_cast<size_t>(FRAME_SIZE);
   const auto numFrames = block.getSize() / (floatsPerFrame * sizeof(float));
   if (numFrames == 0)
      return nullptr;

   std::vector<std::vector<float>> table(numFrames, std::vector<float>(floatsPerFrame));
   const auto* data = static_cast<const float*>(block.getData());
   for (size_t f = 0; f < numFrames; f++)
      std::copy(data + f * floatsPerFrame, data + (f + 1) * floatsPerFrame, table[f].begin());

   return new Wavetable(table, tableName);
}
