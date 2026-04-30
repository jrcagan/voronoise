#pragma once

#include <JuceHeader.h>

class Ball : public juce::Point<float> {

public:
   juce::Colour color;

   float radius;
   float angle;
   float velocity;

   float frequency; // frequency of its associated note

   float timeLeftAlive;
};