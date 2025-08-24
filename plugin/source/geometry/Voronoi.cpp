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

      for (int i = 0; i < (int)tris.size(); i++)
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
      for (auto &kv : edgeToTriangles)
      {
         auto &triIndices = kv.second;
         if (triIndices.size() == 2)
         {
            auto c1 = circumcenters[triIndices[0]];
            auto c2 = circumcenters[triIndices[1]];
            if (c1.valid && c2.valid)
            {
               voronoiEdges.push_back({c1.center, c2.center});
            }
         }
      }

      return voronoiEdges;
   }

   std::map<Delaunay::Point, std::vector<Point>, Delaunay::PointComparator> getCells(
       const std::vector<Delaunay::Triangle> &delaunayTriangles)
   {
      
      std::map<Delaunay::Point, std::vector<Point>, Delaunay::PointComparator> siteToCellVertices;

      for (const auto &tri : delaunayTriangles)
      {
         Delaunay::Circumcircle cc = Delaunay::getCircumcircle(tri);

         if (cc.valid)
         {
            siteToCellVertices[tri.a].push_back(cc.center);
            siteToCellVertices[tri.b].push_back(cc.center);
            siteToCellVertices[tri.c].push_back(cc.center);
         }
      }

      for (auto &pair : siteToCellVertices)
      {
         const auto &site = pair.first;
         auto &vertices = pair.second;

         std::sort(vertices.begin(), vertices.end(),
                   [&site](const Point &v1, const Point &v2)
                   {
                      double angle1 = atan2(v1.y - site.y, v1.x - site.x);
                      double angle2 = atan2(v2.y - site.y, v2.x - site.x);
                      return angle1 < angle2;
                   });
      }

      return siteToCellVertices;
   }
}