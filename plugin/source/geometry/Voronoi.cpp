#include "geometry/Voronoi.h"
#include <set>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace Voronoi
{

   std::vector<GeoUtils::Edge> getEdges(const std::vector<GeoUtils::Triangle> &tris, const GeoUtils::BBox &bbox)
   {
      auto voronoiCells = getCells(tris, bbox);
      std::set<GeoUtils::Edge, GeoUtils::EdgeComparator> edges;

      for (const auto &kv : voronoiCells)
      {
         const auto &vertices = kv.second.vertices;
         if (vertices.size() < 2)
            continue;
         for (size_t i = 0; i < vertices.size(); ++i)
         {
            GeoUtils::Edge e = {vertices[i], vertices[(i + 1) % vertices.size()]};
            if (e.u.x < e.v.x || (e.u.x == e.v.x && e.u.y < e.v.y))
               edges.insert(e);
            else
               edges.insert({e.v, e.u});
         }
      }
      return std::vector<GeoUtils::Edge>(edges.begin(), edges.end());
   }

   std::map<GeoUtils::Point, Cell, GeoUtils::PointComparator> getCells(const std::vector<GeoUtils::Triangle> &tris, const GeoUtils::BBox &bbox)
   {
      std::map<GeoUtils::Point, Cell, GeoUtils::PointComparator> cells;
      if (tris.empty())
         return cells;

      std::map<GeoUtils::Point, std::vector<size_t>, GeoUtils::PointComparator> siteToTriangles;
      for (size_t i = 0; i < tris.size(); ++i)
      {
         siteToTriangles[tris[i].a].push_back(i);
         siteToTriangles[tris[i].b].push_back(i);
         siteToTriangles[tris[i].c].push_back(i);
      }

      std::vector<GeoUtils::Circumcircle> circumcenters;
      circumcenters.reserve(tris.size());
      for (const auto &t : tris)
      {
         circumcenters.push_back(Delaunay::getCircumcircle(t));
      }

      for (const auto &pair : siteToTriangles)
      {
         const auto &site = pair.first;
         const auto &tri_indices = pair.second;

         std::vector<GeoUtils::Point> cell_vertices;
         for (auto tri_idx : tri_indices)
         {
            if (circumcenters[tri_idx].valid)
            {
               cell_vertices.push_back(circumcenters[tri_idx].center);
            }
         }

         for (auto tri_idx : tri_indices)
         {
            const auto &tri = tris[tri_idx];
            GeoUtils::Point p[3] = {tri.a, tri.b, tri.c};
            for (int i = 0; i < 3; ++i)
            {
               GeoUtils::Point p1 = p[i];
               GeoUtils::Point p2 = p[(i + 1) % 3];
               if (!((GeoUtils::pointsEqual(p1, site) || GeoUtils::pointsEqual(p2, site)) && !GeoUtils::pointsEqual(p1, p2)))
                  continue;

               bool is_hull_edge = true;
               for (auto other_tri_idx : tri_indices)
               {
                  if (tri_idx == other_tri_idx)
                     continue;
                  const auto &other_tri = tris[other_tri_idx];
                  if ((GeoUtils::pointsEqual(other_tri.a, p1) || GeoUtils::pointsEqual(other_tri.b, p1) || GeoUtils::pointsEqual(other_tri.c, p1)) &&
                      (GeoUtils::pointsEqual(other_tri.a, p2) || GeoUtils::pointsEqual(other_tri.b, p2) || GeoUtils::pointsEqual(other_tri.c, p2)))
                  {
                     is_hull_edge = false;
                     break;
                  }
               }
               if (is_hull_edge)
               {
                  GeoUtils::Point mid = {(p1.x + p2.x) / 2.0, (p1.y + p2.y) / 2.0};
                  GeoUtils::Point normal = {p2.y - p1.y, p1.x - p2.x};
                  GeoUtils::Point third_pt = p[(i + 2) % 3];
                  if ((mid.x - third_pt.x) * normal.x + (mid.y - third_pt.y) * normal.y < 0)
                  {
                     normal.x = -normal.x;
                     normal.y = -normal.y;
                  }
                  double far_dist = 2.0 * (bbox.maxX - bbox.minX + bbox.maxY - bbox.minY);
                  cell_vertices.push_back({circumcenters[tri_idx].center.x + normal.x * far_dist,
                                           circumcenters[tri_idx].center.y + normal.y * far_dist});
               }
            }
         }

         if (cell_vertices.size() < 2)
            continue;
         GeoUtils::Point center = {0, 0};
         for (const auto &v : cell_vertices)
         {
            center.x += v.x;
            center.y += v.y;
         }
         center.x /= cell_vertices.size();
         center.y /= cell_vertices.size();

         std::sort(cell_vertices.begin(), cell_vertices.end(), [center](const GeoUtils::Point &a, const GeoUtils::Point &b)
                   { return std::atan2(a.y - center.y, a.x - center.x) < std::atan2(b.y - center.y, b.x - center.x); });

         Voronoi::Cell cell;
         cell.vertices = GeoUtils::clipPolygon(cell_vertices, bbox);

         if (!cell.vertices.empty())
         {
            cells[site] = cell;
         }
      }
      return cells;
   }
}
