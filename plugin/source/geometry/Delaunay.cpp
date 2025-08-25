#include "geometry/Delaunay.h"
#include <algorithm>
#include <limits>
#include <cmath>
#include <unordered_map>

namespace Delaunay
{
   GeoUtils::Circumcircle getCircumcircle(const GeoUtils::Triangle &t)
   {
      GeoUtils::Circumcircle cc;
      cc.valid = false;

      const auto &ax = t.a;
      const auto &by = t.b;
      const auto &cz = t.c;

      double d = 2.0 * (ax.x * (by.y - cz.y) + by.x * (cz.y - ax.y) + cz.x * (ax.y - by.y));
      if (std::abs(d) < 1e-18)
         return cc;

      double ux = ((ax.x * ax.x + ax.y * ax.y) * (by.y - cz.y) + (by.x * by.x + by.y * by.y) * (cz.y - ax.y) + (cz.x * cz.x + cz.y * cz.y) * (ax.y - by.y)) / d;
      double uy = ((ax.x * ax.x + ax.y * ax.y) * (cz.x - by.x) + (by.x * by.x + by.y * by.y) * (ax.x - cz.x) + (cz.x * cz.x + cz.y * cz.y) * (by.x - ax.x)) / d;

      cc.center = {ux, uy};
      cc.radiusSq = (ax.x - ux) * (ax.x - ux) + (ax.y - uy) * (ax.y - uy);
      cc.valid = true;
      return cc;
   }

   std::vector<GeoUtils::Triangle> triangulate(const std::vector<GeoUtils::Point> &points)
   {
      if (points.size() < 3)
         return {};

      std::vector<GeoUtils::Point> sorted_points = points;
      std::sort(sorted_points.begin(), sorted_points.end(), GeoUtils::PointComparator());

      std::vector<GeoUtils::Triangle> triangles;

      double minX = sorted_points[0].x, maxX = sorted_points.back().x;
      double minY = sorted_points[0].y, maxY = sorted_points[0].y;
      for (const auto &p : sorted_points)
      {
         if (p.y < minY)
            minY = p.y;
         if (p.y > maxY)
            maxY = p.y;
      }
      double dx = maxX - minX, dy = maxY - minY;
      double deltaMax = std::max(dx, dy);
      GeoUtils::Point mid{(minX + maxX) / 2.0, (minY + maxY) / 2.0};

      GeoUtils::Point p1(mid.x - 20 * deltaMax, mid.y - deltaMax);
      GeoUtils::Point p2(mid.x, mid.y + 20 * deltaMax);
      GeoUtils::Point p3(mid.x + 20 * deltaMax, mid.y - deltaMax);
      triangles.push_back({p1, p2, p3});

      for (const auto &point : sorted_points)
      {
         std::vector<GeoUtils::Edge> polygon;
         std::vector<GeoUtils::Triangle> bad_triangles;

         for (const auto &tri : triangles)
         {
            auto cc = getCircumcircle(tri);
            if (cc.valid && (std::pow(point.x - cc.center.x, 2) + std::pow(point.y - cc.center.y, 2)) < cc.radiusSq)
            {
               bad_triangles.push_back(tri);
            }
         }

         for (const auto &tri : bad_triangles)
         {
            GeoUtils::Edge e[3] = {{tri.a, tri.b}, {tri.b, tri.c}, {tri.c, tri.a}};
            for (int i = 0; i < 3; ++i)
            {
               bool shared = false;
               for (const auto &other_tri : bad_triangles)
               {
                  if (&tri == &other_tri)
                     continue;
                  if ((GeoUtils::pointsEqual(other_tri.a, e[i].u) || GeoUtils::pointsEqual(other_tri.b, e[i].u) || GeoUtils::pointsEqual(other_tri.c, e[i].u)) &&
                      (GeoUtils::pointsEqual(other_tri.a, e[i].v) || GeoUtils::pointsEqual(other_tri.b, e[i].v) || GeoUtils::pointsEqual(other_tri.c, e[i].v)))
                  {
                     shared = true;
                     break;
                  }
               }
               if (!shared)
                  polygon.push_back(e[i]);
            }
         }

         triangles.erase(std::remove_if(triangles.begin(), triangles.end(), [&](const GeoUtils::Triangle &t)
                                        {
                for(const auto& bt : bad_triangles) {
                    if(GeoUtils::pointsEqual(t.a, bt.a) && GeoUtils::pointsEqual(t.b, bt.b) && GeoUtils::pointsEqual(t.c, bt.c)) return true;
                }
                return false; }),
                         triangles.end());

         for (const auto &edge : polygon)
         {
            triangles.push_back({edge.u, edge.v, point});
         }
      }

      triangles.erase(std::remove_if(triangles.begin(), triangles.end(), [&](const GeoUtils::Triangle &t)
                                     { return GeoUtils::pointsEqual(t.a, p1) || GeoUtils::pointsEqual(t.b, p1) || GeoUtils::pointsEqual(t.c, p1) ||
                                              GeoUtils::pointsEqual(t.a, p2) || GeoUtils::pointsEqual(t.b, p2) || GeoUtils::pointsEqual(t.c, p2) ||
                                              GeoUtils::pointsEqual(t.a, p3) || GeoUtils::pointsEqual(t.b, p3) || GeoUtils::pointsEqual(t.c, p3); }),
                      triangles.end());

      return triangles;
   }
}
