#include "synth/Lfo.h"
#include <algorithm>
#include <cmath>

namespace
{
   // a bit of bend on each quarter makes 4 straight lines look like a sine
   constexpr float SINE_BEND = 0.143f;
}

juce::StringArray LfoShape::getPresetNames()
{
   return { "Sine", "Triangle", "Saw up", "Saw down", "Square", "Random steps", "Flat" };
}

LfoShape::LfoShape()
{
   setPreset(Preset::Sine);
}

void LfoShape::setPreset(Preset preset, juce::Random* random)
{
   switch (preset)
   {
      case Preset::Sine:
         points = { { 0.f, 0.5f, -SINE_BEND }, { 0.25f, 1.f, SINE_BEND }, { 0.5f, 0.5f, -SINE_BEND },
                    { 0.75f, 0.f, SINE_BEND }, { 1.f, 0.5f, 0.f } };
         break;
      case Preset::Triangle:
         points = { { 0.f, 0.f, 0.f }, { 0.5f, 1.f, 0.f }, { 1.f, 0.f, 0.f } };
         break;
      case Preset::SawUp:
         points = { { 0.f, 0.f, 0.f }, { 1.f, 1.f, 0.f } };
         break;
      case Preset::SawDown:
         points = { { 0.f, 1.f, 0.f }, { 1.f, 0.f, 0.f } };
         break;
      case Preset::Square:
         points = { { 0.f, 1.f, 0.f }, { 0.5f, 1.f, 0.f }, { 0.5f, 0.f, 0.f }, { 1.f, 0.f, 0.f } };
         break;
      case Preset::Steps:
      {
         juce::Random fallback;
         auto& r = random != nullptr ? *random : fallback;
         constexpr int STEPS = 8;
         points.clear();
         for (int i = 0; i < STEPS; i++)
         {
            const float value = r.nextFloat();
            points.push_back({ static_cast<float>(i) / STEPS, value, 0.f });
            points.push_back({ static_cast<float>(i + 1) / STEPS, value, 0.f });
         }
         break;
      }
      case Preset::Flat:
      case Preset::END:
      default:
         points = { { 0.f, 0.5f, 0.f }, { 1.f, 0.5f, 0.f } };
         break;
   }
}

void LfoShape::setPoints(std::vector<Point> newPoints)
{
   for (auto& p : newPoints)
   {
      p.x = juce::jlimit(0.f, 1.f, p.x);
      p.y = juce::jlimit(0.f, 1.f, p.y);
      p.curve = juce::jlimit(-1.f, 1.f, p.curve);
   }

   std::stable_sort(newPoints.begin(), newPoints.end(), [](const Point& a, const Point& b) { return a.x < b.x; });

   if (newPoints.size() < 2)
   {
      const float y = newPoints.empty() ? 0.5f : newPoints.front().y;
      newPoints = { { 0.f, y, 0.f }, { 1.f, y, 0.f } };
   }

   newPoints.front().x = 0.f;
   newPoints.back().x = 1.f;
   points = std::move(newPoints);
}

int LfoShape::addPoint(float x, float y)
{
   x = juce::jlimit(0.f, 1.f, x);

   // goes after every point at or before x, so it never lands before the first one
   auto it = std::upper_bound(points.begin() + 1, points.end() - 1, x, [](float value, const Point& p) { return value < p.x; });
   const auto index = static_cast<int>(std::distance(points.begin(), it));
   points.insert(it, { x, juce::jlimit(0.f, 1.f, y), 0.f });
   return index;
}

void LfoShape::removePoint(int index)
{
   if (index <= 0 || index >= static_cast<int>(points.size()) - 1)
      return;

   points.erase(points.begin() + index);
}

void LfoShape::movePoint(int index, float x, float y)
{
   if (index < 0 || index >= static_cast<int>(points.size()))
      return;

   auto& p = points[static_cast<size_t>(index)];
   p.y = juce::jlimit(0.f, 1.f, y);

   const bool isEnd = index == 0 || index == static_cast<int>(points.size()) - 1;
   if (! isEnd)
   {
      const float left = points[static_cast<size_t>(index - 1)].x;
      const float right = points[static_cast<size_t>(index + 1)].x;
      p.x = juce::jlimit(left, right, x);
   }
}

void LfoShape::setCurve(int index, float curve)
{
   if (index < 0 || index >= static_cast<int>(points.size()) - 1)
      return;

   points[static_cast<size_t>(index)].curve = juce::jlimit(-1.f, 1.f, curve);
}

float LfoShape::applyCurve(float t, float curve)
{
   t = juce::jlimit(0.f, 1.f, t);
   if (std::abs(curve) < 1e-3f)
      return t;

   // curve 1 is a steep exponent of 8, curve 0 is a straight line
   const float exponent = 1.f + std::abs(curve) * 7.f;
   return curve > 0.f ? std::pow(t, exponent) : 1.f - std::pow(1.f - t, exponent);
}

float LfoShape::evaluate(float phase) const
{
   phase = juce::jlimit(0.f, 1.f, phase);

   for (size_t i = 0; i + 1 < points.size(); i++)
   {
      const auto& a = points[i];
      const auto& b = points[i + 1];

      // the last matching segment wins so a vertical jump uses the value after it
      if (phase >= a.x && phase <= b.x && (phase < b.x || i + 2 == points.size()))
      {
         const float width = b.x - a.x;
         if (width <= 0.f)
            continue;

         const float t = applyCurve((phase - a.x) / width, a.curve);
         return a.y + (b.y - a.y) * t;
      }
   }

   return points.back().y;
}

juce::ValueTree LfoShape::toValueTree() const
{
   juce::ValueTree tree("Shape");
   for (const auto& p : points)
   {
      tree.appendChild(juce::ValueTree("Point")
                           .setProperty("x", p.x, nullptr)
                           .setProperty("y", p.y, nullptr)
                           .setProperty("curve", p.curve, nullptr),
                       nullptr);
   }
   return tree;
}

void LfoShape::fromValueTree(const juce::ValueTree& tree)
{
   std::vector<Point> newPoints;
   for (int i = 0; i < tree.getNumChildren(); i++)
   {
      const auto child = tree.getChild(i);
      newPoints.push_back({ static_cast<float>(child["x"]), static_cast<float>(child["y"]), static_cast<float>(child["curve"]) });
   }

   if (newPoints.size() >= 2)
      setPoints(std::move(newPoints));
}

bool LfoShape::operator==(const LfoShape& other) const
{
   if (points.size() != other.points.size())
      return false;

   for (size_t i = 0; i < points.size(); i++)
   {
      const auto& a = points[i];
      const auto& b = other.points[i];
      if (std::abs(a.x - b.x) > 1e-6f || std::abs(a.y - b.y) > 1e-6f || std::abs(a.curve - b.curve) > 1e-6f)
         return false;
   }
   return true;
}

//==============================================================================
void LfoTable::fill(const LfoShape& shape)
{
   for (int i = 0; i <= SIZE; i++)
      values[static_cast<size_t>(i)].store(shape.evaluate(static_cast<float>(i) / SIZE), std::memory_order_relaxed);
}

float LfoTable::lookup(float phase) const
{
   const float position = juce::jlimit(0.f, 1.f, phase) * SIZE;
   const int index = juce::jmin(SIZE - 1, static_cast<int>(position));
   const float frac = position - static_cast<float>(index);

   const float a = values[static_cast<size_t>(index)].load(std::memory_order_relaxed);
   const float b = values[static_cast<size_t>(index + 1)].load(std::memory_order_relaxed);
   return a + (b - a) * frac;
}
