#pragma once
#include <juce_graphics/juce_graphics.h>
#include <vector>
#include <functional>

namespace Delaunay
{

   using Point = juce::Point<double>;
   bool pointsEqual(const Point &p1, const Point &p2, double eps = 1e-6);
   struct PointComparator
   {
      bool operator()(const Point &p1, const Point &p2) const
      {
         if (p1.getX() != p2.getX())
         {
            return p1.getX() < p2.getX();
         }
         return p1.getY() < p2.getY();
      }
   };

   struct Triangle
   {
      Point a;
      Point b;
      Point c;
   };

   struct Circumcircle
   {
      Point center;
      double radiusSq;
      bool valid;
   };

   struct Edge
   {
      Point u;
      Point v;
      bool operator==(const Edge &other) const
      {
         return (pointsEqual(u, other.u) && pointsEqual(v, other.v)) ||
                (pointsEqual(u, other.v) && pointsEqual(v, other.u));
      }
   };

   Circumcircle getCircumcircle(const Triangle &t);

   std::vector<Triangle> triangulate(std::vector<Point> points);

}