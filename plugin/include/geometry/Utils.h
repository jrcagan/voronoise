#pragma once
#include <juce_graphics/juce_graphics.h>
#include <vector>
#include <cmath> // Required for std::abs

namespace GeoUtils
{
   using Point = juce::Point<double>;

   bool pointsEqual(const Point &p1, const Point &p2, double eps = 1e-9);

   struct Triangle
   {
      Point a;
      Point b;
      Point c;
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

   struct BBox
   {
      double minX, minY, maxX, maxY;
   };

   struct Circumcircle
   {
      Point center;
      double radiusSq;
      bool valid;
   };

   struct PointComparator
   {
      bool operator()(const Point &p1, const Point &p2) const
      {
         if (p1.getX() < p2.getX())
            return true;
         if (p1.getX() > p2.getX())
            return false;
         return p1.getY() < p2.getY();
      }
   };

   struct EdgeComparator
   {
      bool operator()(const GeoUtils::Edge &a, const GeoUtils::Edge &b) const
      {
         GeoUtils::PointComparator point_comp;

         auto get_canonical = [&](const GeoUtils::Edge &e)
         {
            return point_comp(e.u, e.v) ? e : GeoUtils::Edge{e.v, e.u};
         };

         GeoUtils::Edge canon_a = get_canonical(a);
         GeoUtils::Edge canon_b = get_canonical(b);

         if (point_comp(canon_a.u, canon_b.u))
            return true;
         if (point_comp(canon_b.u, canon_a.u))
            return false;

         return point_comp(canon_a.v, canon_b.v);
      }
   };

   bool clipEdge(Edge &edge, const BBox &box);
   std::vector<Point> clipPolygon(const std::vector<Point> &poly, const BBox &box);
}
