#include "geometry/Voronoi.h"
#include <set>
#include <cmath>

namespace Voronoi
{
   struct EdgeKey
   {
      Delaunay::Point u, v;
      bool operator<(const EdgeKey &other) const
      {
         if (u.x != other.u.x)
            return u.x < other.u.x;
         if (u.y != other.u.y)
            return u.y < other.u.y;
         if (v.x != other.v.x)
            return v.x < other.v.x;
         return v.y < other.v.y;
      }
   };

   std::vector<Delaunay::Edge> getEdges(const std::vector<Delaunay::Triangle> &tris)
   {
      std::map<EdgeKey, std::vector<int>> edgeToTriangles;

      for (int i = 0; i < tris.size(); i++)
      {
         auto t = tris[i];
         auto addEdge = [&](Delaunay::Point a, Delaunay::Point b)
         {
            if (b.x < a.x || (b.x == a.x && b.y < a.y))
               std::swap(a, b);
            edgeToTriangles[{a, b}].push_back(i);
         };
         addEdge(t.a, t.b);
         addEdge(t.b, t.c);
         addEdge(t.c, t.a);
      }

      std::vector<Delaunay::Circumcircle> circumcenters(tris.size());
      for (int i = 0; i < (int)tris.size(); i++)
         circumcenters[i] = Delaunay::getCircumcircle(tris[i]);

      std::vector<Delaunay::Edge> voronoiEdges;
      const double drawing_scale = 1000.0;

      for (auto &kv : edgeToTriangles)
      {
         auto &triIndices = kv.second;
         auto delaunayEdge = kv.first;

         if (triIndices.size() == 2)
         {
            auto c1 = circumcenters[triIndices[0]];
            auto c2 = circumcenters[triIndices[1]];
            if (c1.valid && c2.valid)
            {
               voronoiEdges.push_back({c1.center, c2.center});
            }
         }
         else if (triIndices.size() == 1)
         {
            auto c1 = circumcenters[triIndices[0]];
            if (!c1.valid)
               continue;

            Delaunay::Point p1 = delaunayEdge.u;
            Delaunay::Point p2 = delaunayEdge.v;

            const auto &tri = tris[triIndices[0]];
            Delaunay::Point p3;
            if (Delaunay::pointsEqual(tri.a, p1) || Delaunay::pointsEqual(tri.a, p2))
            {
               if (Delaunay::pointsEqual(tri.b, p1) || Delaunay::pointsEqual(tri.b, p2))
               {
                  p3 = tri.c;
               }
               else
               {
                  p3 = tri.b;
               }
            }
            else
            {
               p3 = tri.a;
            }

            Delaunay::Point midpoint = {(p1.x + p2.x) / 2.0, (p1.y + p2.y) / 2.0};

            Delaunay::Point vec_in = {p3.x - midpoint.x, p3.y - midpoint.y};

            Delaunay::Point normal = {p2.y - p1.y, p1.x - p2.x};

            if (normal.x * vec_in.x + normal.y * vec_in.y > 0)
            {
               normal.x = -normal.x;
               normal.y = -normal.y;
            }

            double len = std::sqrt(normal.x * normal.x + normal.y * normal.y);
            if (len > 1e-9)
            {
               normal.x /= len;
               normal.y /= len;
            }

            Delaunay::Point far_point = {
                c1.center.x + normal.x * drawing_scale,
                c1.center.y + normal.y * drawing_scale};

            voronoiEdges.push_back({c1.center, far_point});
         }
      }

      return voronoiEdges;
   }

   std::map<Delaunay::Point, Voronoi::Cell, Delaunay::PointComparator>
   getCells(const std::vector<Delaunay::Triangle> &tris, Delaunay::BBox bbox)
   {

      std::vector<Delaunay::Circumcircle> cc(tris.size());
      for (int i = 0; i < (int)tris.size(); ++i)
         cc[i] = getCircumcircle(tris[i]);

      auto norm = [](Point a, Point b)
      { if (b.x<a.x || (b.x==a.x && b.y<a.y)) std::swap(a,b); return EdgeKey{a,b}; };

      std::map<EdgeKey, std::vector<int>> edgeToTris;
      std::map<Point, std::vector<int>, Delaunay::PointComparator> siteToTris;
      std::map<Point, std::set<Point, Delaunay::PointComparator>, Delaunay::PointComparator> siteNeighbors;

      for (int i = 0; i < (int)tris.size(); ++i)
      {
         const auto &t = tris[i];

         auto addE = [&](Point a, Point b)
         { edgeToTris[norm(a, b)].push_back(i); };
         addE(t.a, t.b);
         addE(t.b, t.c);
         addE(t.c, t.a);

         siteToTris[t.a].push_back(i);
         siteToTris[t.b].push_back(i);
         siteToTris[t.c].push_back(i);

         siteNeighbors[t.a].insert(t.b);
         siteNeighbors[t.a].insert(t.c);
         siteNeighbors[t.b].insert(t.a);
         siteNeighbors[t.b].insert(t.c);
         siteNeighbors[t.c].insert(t.a);
         siteNeighbors[t.c].insert(t.b);
      }

      std::map<Point, Voronoi::Cell, Delaunay::PointComparator> cells;

      auto almostEqual = [](double a, double b)
      { return std::abs(a - b) < 1e-9; };
      auto dedup = [&](std::vector<Point> &v)
      {
         std::vector<Point> out;
         for (auto &p : v)
         {
            if (out.empty() || !almostEqual(p.x, out.back().x) || !almostEqual(p.y, out.back().y))
               out.push_back(p);
         }
         v.swap(out);
      };

      for (auto &kv : siteToTris)
      {
         const Point &site = kv.first;
         const auto &triIdx = kv.second;

         // Collect circumcenters of incident triangles, linked by adjacency
         std::map<int, std::vector<int>> triAdj;
         for (int idx : triIdx)
         {
            const auto &t = tris[idx];
            for (Point v : {t.a, t.b, t.c})
            {
               if (Delaunay::pointsEqual(v, site))
                  continue;
               EdgeKey ek = norm(site, v);
               auto it = edgeToTris.find(ek);
               if (it != edgeToTris.end() && it->second.size() == 2)
               {
                  int t0 = it->second[0], t1 = it->second[1];
                  triAdj[t0].push_back(t1);
                  triAdj[t1].push_back(t0);
               }
            }
         }

         // Walk around site, chaining circumcenters in order
         std::vector<Point> verts;
         if (!triIdx.empty())
         {
            int start = triIdx[0];
            int curr = start, prev = -1;
            do
            {
               if (!cc[curr].valid)
                  break;
               verts.push_back(cc[curr].center);

               // pick next adjacent triangle around site
               int next = -1;
               for (int nb : triAdj[curr])
                  if (nb != prev)
                  {
                     next = nb;
                     break;
                  }

               prev = curr;
               curr = next;
            } while (curr != -1 && curr != start);
         }

         dedup(verts);

         Voronoi::Cell cell;
         cell.vertices = std::move(verts);

         if (!cell.closed)
         {
            cell.vertices = clipPolygon(cell.vertices, bbox);
            cell.closed = true; // after clipping it's now bounded
         }

         // hull test
         bool onHull = false;
         for (const Point &nb : siteNeighbors[site])
         {
            EdgeKey ek = norm(site, nb);
            auto it = edgeToTris.find(ek);
            if (it != edgeToTris.end() && it->second.size() == 1)
            {
               onHull = true;
               break;
            }
         }
         cell.closed = !onHull;

         cells[site] = std::move(cell);
      }

      return cells;
   }
}