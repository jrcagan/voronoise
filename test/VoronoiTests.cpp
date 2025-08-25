#include <gtest/gtest.h>
#include <vector>
#include <set>
#include <algorithm>
#include <map>
#include <iostream>
#include <cmath>
#include <iomanip>

#include "geometry/Utils.h"
#include "geometry/Delaunay.h"
#include "geometry/Voronoi.h"

void printPoint(const GeoUtils::Point &p)
{
   std::cout << std::fixed << std::setprecision(4) << "(" << p.getX() << ", " << p.getY() << ")";
}

void printEdge(const GeoUtils::Edge &e)
{
   printPoint(e.u);
   std::cout << " -> ";
   printPoint(e.v);
}

GeoUtils::Edge getCanonicalEdge(const GeoUtils::Point &p1, const GeoUtils::Point &p2)
{
   GeoUtils::PointComparator comp;
   if (comp(p1, p2))
   {
      return {p1, p2};
   }
   return {p2, p1};
}

TEST(VoronoiConsistencyTest, EdgesFromGetEdgesMatchEdgesFromGetCells)
{
   std::vector<GeoUtils::Point> sites = {{30, 30}, {40, 40}, {20, 50}, {50, 20}};
   GeoUtils::BBox bbox = {0, 0, 60, 60};

   auto tris = Delaunay::triangulate(sites);

   auto direct_edges_vec = Voronoi::getEdges(tris, bbox);
   std::set<GeoUtils::Edge, GeoUtils::EdgeComparator> direct_edges_set;
   for (const auto &edge : direct_edges_vec)
   {
      direct_edges_set.insert(edge);
   }

   auto voronoi_cells = Voronoi::getCells(tris, bbox);
   std::set<GeoUtils::Edge, GeoUtils::EdgeComparator> edges_from_cells;
   for (const auto &kv : voronoi_cells)
   {
      const auto &vertices = kv.second.vertices;
      if (vertices.size() < 2)
         continue;
      for (size_t i = 0; i < vertices.size(); ++i)
      {
         edges_from_cells.insert({vertices[i], vertices[(i + 1) % vertices.size()]});
      }
   }

   std::cout << "\n--- Final Set Comparison ---\n"
             << std::endl;

   std::cout << "Set 1: Edges from getEdges() (size=" << direct_edges_set.size() << ")" << std::endl;
   for (const auto &edge : direct_edges_set)
   {
      std::cout << "  ";
      printEdge(edge);
      std::cout << std::endl;
   }

   std::cout << "\nSet 2: Edges from getCells() (size=" << edges_from_cells.size() << ")" << std::endl;
   for (const auto &edge : edges_from_cells)
   {
      std::cout << "  ";
      printEdge(edge);
      std::cout << std::endl;
   }
   std::cout << "\n--- End of Log ---\n"
             << std::endl;

   ASSERT_EQ(direct_edges_set, edges_from_cells);
}
