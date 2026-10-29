#pragma once

#include <juce_graphics/juce_graphics.h>
#include <array>
#include <map>
#include <utility>
#include <vector>
#include "geometry/Utils.h"

// voronoi / delaunay under other ways of measuring distance. both use the screen rectangle:
// manhattan just clips to it, hilbert measures distance relative to it (it's the convex domain)
namespace Metrics
{
   enum class Type {
      Euclidean,
      Manhattan,
      Hilbert,
      END
   };

   inline juce::StringArray getNames() {
      return { "Euclid", "Manhattan", "Hilbert" };
   }

   using Point = GeoUtils::Point;
   using Polyline = std::vector<Point>;

   struct Diagram {
      std::vector<Point> sites;                     // as used: duplicates dropped, square pairs nudged
      std::vector<Polyline> voronoiLines;           // the walls between cells, each one between two sites
      std::vector<std::pair<int, int>> neighbours;  // sites (indices into sites) that share a wall, the delaunay graph
      std::vector<juce::Point<float>> cellCentres;  // middle of each cell (by area)
   };

   // the bisectors from the last build, by the two sites' positions. dragging a site only changes the
   // bisectors it's part of, so the rest come from here instead of being traced again
   struct BisectorCache {
      Type type = Type::Euclidean;
      double width = 0.0, height = 0.0;
      std::map<std::array<double, 4>, Polyline> bisectors;
   };

   // manhattan and hilbert only, euclidean stays with the delaunay/voronoi code it had already
   Diagram build(Type type, const std::vector<Point>& sites, double width, double height, BisectorCache* cache = nullptr);

   //==============================================================================
   // distances
   double manhattan(Point a, Point b);

   // hilbert distance inside the rectangle: 1/2 ln of the cross ratio of the two points and where the line
   // through them meets the boundary. with a rectangle that's a ratio of distances to the sides it hits
   double hilbert(Point a, Point b, double width, double height);

   // the same distance worked out the long way (boundary intersections + cross ratio), the way
   // Hilbert_K_Voronoi's calculateHilbertDistance does it. used to check the fast one
   double hilbertCrossRatio(Point a, Point b, double width, double height);

   //==============================================================================
   // bisectors, each a polyline from one side of the rectangle to another

   // JDragovich/manhattan-voronoi's findL1Bisector: vertical or horizontal rays with a 45 degree piece between
   Polyline manhattanBisector(Point a, Point b, double width, double height);

   // nudges any two sites that sit exactly on the corners of a square (equal x and y distance),
   // where the L1 bisector isn't a line but a whole region. their cleanData
   std::vector<Point> nudgeSquares(std::vector<Point> sites);

   // nudges sites that share an x or y, for hilbert. in a rectangle those two are equally far from
   // everything in a whole region, the same kind of problem as manhattan's squares
   std::vector<Point> nudgeAligned(std::vector<Point> sites);

   // Hilbert_K_Voronoi's bisector: inside each sector (fixed boundary faces for both sites' lines)
   // the bisector is a conic, traced sector to sector from the midpoint out to the boundary
   Polyline hilbertBisector(Point a, Point b, double width, double height);

   // which sides the lines through the two sites and p hit, the sector p is in
   struct Sector {
      int aEnter = 0, aExit = 0, bEnter = 0, bExit = 0; // 0 top, 1 right, 2 bottom, 3 left
   };
   Sector sectorAt(Point p, Point a, Point b, double width, double height);

   // the bisector conic for that sector, evaluated at p (0 on the bisector), with its gradient.
   // the conic from bisectorConicFromSector, written as a product of the side distances
   double sectorConic(const Sector& sector, Point a, Point b, Point p, double width, double height, Point* gradient = nullptr);
}
