#pragma once
#include "geometry/Utils.h"
#include "geometry/Delaunay.h"
#include <vector>
#include <map>

namespace Voronoi
{
   struct Cell
   {
      std::vector<GeoUtils::Point> vertices;
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

   std::vector<GeoUtils::Edge> getEdges(const std::vector<GeoUtils::Triangle> &tris, const GeoUtils::BBox &bbox);
   std::map<GeoUtils::Point, Cell, GeoUtils::PointComparator> getCells(const std::vector<GeoUtils::Triangle> &tris, const GeoUtils::BBox &bbox);
}
