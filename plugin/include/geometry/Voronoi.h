#pragma once
#include "geometry/Delaunay.h"
#include <vector>
#include <map>

namespace Voronoi
{
   using Point = juce::Point<double>;

   struct Cell
   {
      std::vector<Point> vertices; // ordered CCW
      bool closed = true;          // false for hull (unbounded) cells
   };

   std::vector<Delaunay::Edge> getEdges(const std::vector<Delaunay::Triangle> &tris);
   std::map<Delaunay::Point, Voronoi::Cell, Delaunay::PointComparator> 
      getCells(const std::vector<Delaunay::Triangle> &tris, Delaunay::BBox bbox);
}