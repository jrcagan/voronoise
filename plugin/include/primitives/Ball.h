#pragma once

#include <JuceHeader.h>

class Ball : juce::Point {

private:
   juce::Point pos;
   juce::Colour color;

   float radius;
   float angle;
   float velocity;

   float frequency; // frequency of its associated note

   float timeLeftAlive;
}