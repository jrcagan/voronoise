#include "geometry/Delaunay.h"
#include <algorithm>
#include <limits>
#include <cmath>
#include <unordered_map>

namespace Delaunay
{
   bool pointsEqual(const Point &p1, const Point &p2, double eps)
   {
      return std::abs(p1.x - p2.x) <= eps && std::abs(p1.y - p2.y) <= eps;
   }

   static double orient2d(const Point &a,
                          const Point &b,
                          const Point &c)
   {
      return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
   }

   Circumcircle getCircumcircle(const Triangle &t)
   {
      Circumcircle cc;
      cc.valid = false;

      Point ax{t.a.x, t.a.y};
      Point by{t.b.x, t.b.y};
      Point cz{t.c.x, t.c.y};

      double d = 2.0 * (ax.x * (by.y - cz.y) + by.x * (cz.y - ax.y) + cz.x * (ax.y - by.y));
      if (std::abs(d) < 1e-18)
         return cc;

      double ax2ay2 = ax.x * ax.x + ax.y * ax.y;
      double bx2by2 = by.x * by.y + by.y * by.y;
      double cx2cy2 = cz.x * cz.x + cz.y * cz.y;

      double ux = (ax2ay2 * (by.y - cz.y) + bx2by2 * (cz.y - ax.y) + cx2cy2 * (ax.y - by.y)) / d;
      double uy = (ax2ay2 * (cz.x - by.x) + bx2by2 * (ax.x - cz.x) + cx2cy2 * (by.x - ax.x)) / d;

      cc.center = {ux, uy};
      double dx = ax.x - ux;
      double dy = ax.y - uy;
      cc.radiusSq = dx * dx + dy * dy;
      cc.valid = true;
      return cc;
   }

   static bool pointInCircumcircle(const Point &p, const Circumcircle &cc)
   {
      if (!cc.valid)
         return false;
      double dx = p.x - cc.center.x;
      double dy = p.y - cc.center.y;
      double d2 = dx * dx + dy * dy;
      return d2 <= cc.radiusSq + 1e-12;
   }

   struct EdgeHash
   {
      size_t operator()(const Edge &e) const noexcept
      {
         auto roundl = [](double f)
         { return std::lround(f * 1e6); };
         long ux = roundl(e.u.x), uy = roundl(e.u.y);
         long vx = roundl(e.v.x), vy = roundl(e.v.y);
         if (vx < ux || (vx == ux && vy < uy))
         {
            std::swap(ux, vx);
            std::swap(uy, vy);
         }
         size_t h = 1469598103934665603ull;
         auto mix = [&h](long v)
         {
            h ^= static_cast<size_t>(v);
            h *= 1099511628211ull;
         };
         mix(ux);
         mix(uy);
         mix(vx);
         mix(vy);
         return h;
      }
   };

   static std::vector<Point> sortByAxis(std::vector<Point> pts)
   {
      double minX = std::numeric_limits<double>::infinity();
      double maxX = -minX;
      double minY = std::numeric_limits<double>::infinity();
      double maxY = -minY;
      for (auto &p : pts)
      {
         minX = std::min(minX, p.x);
         maxX = std::max(maxX, p.x);
         minY = std::min(minY, p.y);
         maxY = std::max(maxY, p.y);
      }
      bool useX = (maxX - minX) >= (maxY - minY);
      std::sort(pts.begin(), pts.end(), [useX](const Point &a, const Point &b)
                { return useX ? (a.x < b.x) : (a.y < b.y); });
      return pts;
   }

   static Triangle getSuperTriangle(const std::vector<Point> &pts)
   {
      double minX = std::numeric_limits<double>::infinity();
      double maxX = -minX;
      double minY = std::numeric_limits<double>::infinity();
      double maxY = -minY;
      for (auto &p : pts)
      {
         minX = std::min(minX, p.x);
         maxX = std::max(maxX, p.x);
         minY = std::min(minY, p.y);
         maxY = std::max(maxY, p.y);
      }
      double dx = maxX - minX;
      double dy = maxY - minY;
      double dMax = std::max(dx, dy);
      double midx = (minX + maxX) * 0.5;
      double midy = (minY + maxY) * 0.5;

      Point a{midx - 2.0 * dMax, midy - dMax};
      Point b{midx, midy + 2.0 * dMax};
      Point c{midx + 2.0 * dMax, midy - dMax};

      return {a, b, c};
   }

   std::vector<Triangle> triangulate(std::vector<Point> points)
   {
      std::vector<Triangle> triangleList;
      if (points.size() < 3)
         return triangleList;

      points = sortByAxis(points);
      Triangle superTriangle = getSuperTriangle(points);
      triangleList.push_back(superTriangle);

      for (auto &site : points)
      {
         std::vector<int> badTriangles;
         std::vector<Edge> edges;

         for (int i = 0; i < (int)triangleList.size(); i++)
         {
            auto &t = triangleList[i];
            Circumcircle cc = getCircumcircle(t);
            if (cc.valid && pointInCircumcircle(site, cc))
            {
               badTriangles.push_back(i);
               edges.push_back({t.a, t.b});
               edges.push_back({t.b, t.c});
               edges.push_back({t.c, t.a});
            }
         }

         std::sort(badTriangles.begin(), badTriangles.end());
         for (int k = (int)badTriangles.size() - 1; k >= 0; --k)
            triangleList.erase(triangleList.begin() + badTriangles[k]);

         std::unordered_map<size_t, int> edgeCount;
         std::unordered_map<size_t, Edge> edgeMap;
         for (auto &e : edges)
         {
            size_t h = EdgeHash{}(e);
            edgeCount[h]++;
            edgeMap[h] = e;
         }

         for (auto &kv : edgeCount)
         {
            if (kv.second == 1)
            {
               auto e = edgeMap[kv.first];
               Triangle newT{e.u, e.v, site};
               if (std::abs(orient2d(newT.a, newT.b, newT.c)) > 1e-12)
                  triangleList.push_back(newT);
            }
         }
      }

      std::vector<Triangle> result;
      for (auto &t : triangleList)
      {
         if (pointsEqual(t.a, superTriangle.a) || pointsEqual(t.a, superTriangle.b) || pointsEqual(t.a, superTriangle.c) ||
             pointsEqual(t.b, superTriangle.a) || pointsEqual(t.b, superTriangle.b) || pointsEqual(t.b, superTriangle.c) ||
             pointsEqual(t.c, superTriangle.a) || pointsEqual(t.c, superTriangle.b) || pointsEqual(t.c, superTriangle.c))
            continue;
         result.push_back(t);
      }

      return result;
   }
}