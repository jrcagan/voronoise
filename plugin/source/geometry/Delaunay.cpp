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

      double ax2 = ax.x * ax.x;
      double ax_y2 = ax.y * ax.y;
      double by2 = by.x * by.x;
      double by_y2 = by.y * by.y;
      double cz2 = cz.x * cz.x;
      double cz_y2 = cz.y * cz.y;

      double ux = ((ax2 + ax_y2) * (by.y - cz.y) + (by2 + by_y2) * (cz.y - ax.y) + (cz2 + cz_y2) * (ax.y - by.y)) / d;
      double uy = ((ax2 + ax_y2) * (cz.x - by.x) + (by2 + by_y2) * (ax.x - cz.x) + (cz2 + cz_y2) * (by.x - ax.x)) / d;

      cc.center = {ux, uy};
      double dx = ax.x - ux;
      double dy = ax.y - uy;
      cc.radiusSq = dx * dx + dy * dy;
      cc.valid = true;
      return cc;
   }

   static bool pointInCircumcircle(const Point &p, const Triangle &t)
   {
      double ax = t.a.x - p.x;
      double ay = t.a.y - p.y;
      double bx = t.b.x - p.x;
      double by = t.b.y - p.y;
      double cx = t.c.x - p.x;
      double cy = t.c.y - p.y;

      double det = (ax * ax + ay * ay) * (bx * cy - cx * by) -
                   (bx * bx + by * by) * (ax * cy - cx * ay) +
                   (cx * cx + cy * cy) * (ax * by - bx * ay);

      double orientation = orient2d(t.a, t.b, t.c);

      const double epsilon = 1e-12;

      if (orientation > 0)
      {
         return det > -epsilon;
      }

      return det < epsilon;
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
            if (pointInCircumcircle(site, t))
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
      const double cleanupEpsilon = 1e-9;
      for (auto &t : triangleList)
      {
         if (pointsEqual(t.a, superTriangle.a, cleanupEpsilon) || pointsEqual(t.a, superTriangle.b, cleanupEpsilon) || pointsEqual(t.a, superTriangle.c, cleanupEpsilon) ||
             pointsEqual(t.b, superTriangle.a, cleanupEpsilon) || pointsEqual(t.b, superTriangle.b, cleanupEpsilon) || pointsEqual(t.b, superTriangle.c, cleanupEpsilon) ||
             pointsEqual(t.c, superTriangle.a, cleanupEpsilon) || pointsEqual(t.c, superTriangle.b, cleanupEpsilon) || pointsEqual(t.c, superTriangle.c, cleanupEpsilon))
            continue;
         result.push_back(t);
      }

      return result;
   }
}

bool inside(const Delaunay::Point &p, int edge, const Delaunay::BBox &box) {
    switch (edge) {
        case 0: return p.x >= box.minX; // left
        case 1: return p.x <= box.maxX; // right
        case 2: return p.y >= box.minY; // bottom
        case 3: return p.y <= box.maxY; // top
    }
    return true;
}

Delaunay::Point Delaunay::intersect(const Delaunay::Point &a, const Delaunay::Point &b,
                          int edge, const Delaunay::BBox &box) {
    double x, y;
    double dx = b.x - a.x, dy = b.y - a.y;

    switch (edge) {
        case 0: // left
            x = box.minX;
            y = a.y + dy * (box.minX - a.x) / dx;
            break;
        case 1: // right
            x = box.maxX;
            y = a.y + dy * (box.maxX - a.x) / dx;
            break;
        case 2: // bottom
            y = box.minY;
            x = a.x + dx * (box.minY - a.y) / dy;
            break;
        case 3: // top
            y = box.maxY;
            x = a.x + dx * (box.maxY - a.y) / dy;
            break;
         default: // should never happen
            y = 0;
            x = 0;
    }
    return {x, y};
}

std::vector<Delaunay::Point> Delaunay::clipPolygon(const std::vector<Delaunay::Point> &poly,
                                         const BBox &box) {
    std::vector<Delaunay::Point> output = poly;

    for (int edge = 0; edge < 4; edge++) {
        std::vector<Delaunay::Point> input = output;
        output.clear();

        if (input.empty()) break;

        Delaunay::Point S = input.back();
        for (auto &E : input) {
            if (inside(E, edge, box)) {
                if (!inside(S, edge, box)) {
                    output.push_back(intersect(S, E, edge, box));
                }
                output.push_back(E);
            } else if (inside(S, edge, box)) {
                output.push_back(intersect(S, E, edge, box));
            }
            S = E;
        }
    }
    return output;
}
