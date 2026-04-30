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

   std::vector<GeoUtils::Edge> getEdges(const std::vector<GeoUtils::Triangle> &tris, const GeoUtils::BBox &bbox);
   std::map<GeoUtils::Point, Cell, GeoUtils::PointComparator> getCells(const std::vector<GeoUtils::Triangle> &tris, const GeoUtils::BBox &bbox);
}
