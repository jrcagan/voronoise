#pragma once
#include "geometry/Delaunay.h"
#include <vector>
#include <map>

namespace Voronoi
{
   using Point = juce::Point<double>;

   std::vector<Delaunay::Edge> getEdges(const std::vector<Delaunay::Triangle>& tris);
   std::map<Delaunay::Point, std::vector<Point>, Delaunay::PointComparator> getCells(const std::vector<Delaunay::Triangle> &delaunayTriangles);
}