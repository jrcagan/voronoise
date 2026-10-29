#include "geometry/Metrics.h"
#include <cmath>
#include <limits>
#include <optional>

namespace Metrics
{
   namespace
   {
      // the rectangle's sides, in the order the hilbert code walks a polygon's faces
      enum Face { TOP = 0, RIGHT = 1, BOTTOM = 2, LEFT = 3 };

      // distance from q to the line a side lies on (positive inside the rectangle)
      double sideDistance(int face, Point q, double width, double height)
      {
         switch (face)
         {
            case TOP:    return q.y;
            case RIGHT:  return width - q.x;
            case BOTTOM: return height - q.y;
            case LEFT:
            default:     return q.x;
         }
      }

      // how that distance changes as q moves
      Point sideGradient(int face)
      {
         switch (face)
         {
            case TOP:    return { 0.0, 1.0 };
            case RIGHT:  return { -1.0, 0.0 };
            case BOTTOM: return { 0.0, -1.0 };
            case LEFT:
            default:     return { 1.0, 0.0 };
         }
      }

      // which side a ray from origin heading in direction hits first
      int sideHit(Point origin, Point direction, double width, double height)
      {
         constexpr double TINY = 1e-12;
         const double inf = std::numeric_limits<double>::infinity();
         double tx = inf, ty = inf;
         int xFace = RIGHT, yFace = BOTTOM;
         if (direction.x > TINY)       { tx = (width - origin.x) / direction.x;  xFace = RIGHT; }
         else if (direction.x < -TINY) { tx = -origin.x / direction.x;           xFace = LEFT; }
         if (direction.y > TINY)       { ty = (height - origin.y) / direction.y; yFace = BOTTOM; }
         else if (direction.y < -TINY) { ty = -origin.y / direction.y;           yFace = TOP; }
         return tx <= ty ? xFace : yFace;
      }

      bool inside(Point p, double width, double height)
      {
         return p.x > 0.0 && p.x < width && p.y > 0.0 && p.y < height;
      }

      // where the segment from p (inside) to q (outside) crosses the rectangle
      Point borderCrossing(Point p, Point q, double width, double height)
      {
         double t = 1.0;
         const auto d = q - p;
         if (q.x < 0.0)    t = juce::jmin(t, -p.x / d.x);
         if (q.x > width)  t = juce::jmin(t, (width - p.x) / d.x);
         if (q.y < 0.0)    t = juce::jmin(t, -p.y / d.y);
         if (q.y > height) t = juce::jmin(t, (height - p.y) / d.y);
         auto hit = p + d * juce::jlimit(0.0, 1.0, t);
         hit.x = juce::jlimit(0.0, width, hit.x);
         hit.y = juce::jlimit(0.0, height, hit.y);
         return hit;
      }

      double length(Point v) { return std::sqrt(v.x * v.x + v.y * v.y); }

      // drops points that are within tolerance of the line between their neighbours (douglas-peucker)
      Polyline simplify(const Polyline& line, double tolerance)
      {
         if (line.size() < 3)
            return line;

         std::vector<bool> keep(line.size(), false);
         keep.front() = keep.back() = true;
         std::vector<std::pair<size_t, size_t>> stack { { 0, line.size() - 1 } };
         while (! stack.empty())
         {
            const auto [first, last] = stack.back();
            stack.pop_back();
            const auto a = line[first], b = line[last];
            const auto ab = b - a;
            const double abLength = length(ab);
            double worst = -1.0;
            size_t worstAt = first;
            for (size_t i = first + 1; i < last; i++)
            {
               const auto ap = line[i] - a;
               const double off = abLength > 1e-12 ? std::abs(ab.x * ap.y - ab.y * ap.x) / abLength : length(ap);
               if (off > worst)
               {
                  worst = off;
                  worstAt = i;
               }
            }
            if (worst > tolerance)
            {
               keep[worstAt] = true;
               stack.push_back({ first, worstAt });
               stack.push_back({ worstAt, last });
            }
         }

         Polyline out;
         for (size_t i = 0; i < line.size(); i++)
            if (keep[i])
               out.push_back(line[i]);
         return out;
      }
   }

   //==============================================================================
   double manhattan(Point a, Point b)
   {
      return std::abs(a.x - b.x) + std::abs(a.y - b.y);
   }

   double hilbert(Point a, Point b, double width, double height)
   {
      const auto d = b - a;
      if (length(d) < 1e-12)
         return 0.0;

      // the line through them leaves the rectangle behind a (enter) and beyond b (exit). along that line,
      // distances to those boundary points are proportional to distances to the sides they're on
      const int enter = sideHit(a, a - b, width, height);
      const int exit = sideHit(b, d, width, height);
      const double ratio = (sideDistance(enter, b, width, height) * sideDistance(exit, a, width, height))
                         / (sideDistance(enter, a, width, height) * sideDistance(exit, b, width, height));
      return 0.5 * std::log(ratio);
   }

   double hilbertCrossRatio(Point a, Point b, double width, double height)
   {
      if (length(b - a) < 1e-12)
         return 0.0;

      // intersect the line through a and b with every side, keep the (two) hits that are on the sides
      const Point corners[] = { { 0.0, 0.0 }, { width, 0.0 }, { width, height }, { 0.0, height } };
      std::vector<Point> hits;
      for (int i = 0; i < 4; i++)
      {
         const auto s = corners[i], e = corners[(i + 1) % 4];
         const auto r = b - a, q = e - s;
         const double denominator = r.x * q.y - r.y * q.x;
         if (std::abs(denominator) < 1e-12)
            continue;
         const double u = ((s.x - a.x) * r.y - (s.y - a.y) * r.x) / denominator;
         if (u < -1e-9 || u > 1.0 + 1e-9)
            continue;
         const auto hit = s + q * juce::jlimit(0.0, 1.0, u);
         bool duplicate = false;
         for (const auto& h : hits)
            duplicate = duplicate || length(h - hit) < 1e-9;
         if (! duplicate)
            hits.push_back(hit);
      }
      if (hits.size() < 2)
         return std::numeric_limits<double>::quiet_NaN();

      // first = the boundary point nearer a, last = nearer b, then 1/2 ln of the cross ratio
      const bool firstIsZero = length(a - hits[0]) < length(b - hits[0]);
      const auto first = firstIsZero ? hits[0] : hits[1];
      const auto last = firstIsZero ? hits[1] : hits[0];
      const double cross = (length(a - last) / length(b - last)) * (length(b - first) / length(a - first));
      return 0.5 * std::log(cross);
   }

   //==============================================================================
   std::vector<Point> nudgeSquares(std::vector<Point> sites)
   {
      for (size_t i = 0; i < sites.size(); i++)
      {
         for (size_t j = 0; j < sites.size(); j++)
         {
            auto& d = sites[j];
            const auto& e = sites[i];
            if (i != j && std::abs(d.x - e.x) == std::abs(d.y - e.y))
            {
               d.x = d.x + 1e-10 * d.y;
               d.y = d.y + 2e-10 * d.x;
            }
         }
      }
      return sites;
   }

   std::vector<Point> nudgeAligned(std::vector<Point> sites)
   {
      // two sites in a line with each other (same x, or same y) are exactly as far from every point in the
      // region where both their lines leave through the same pair of opposite sides, so their bisector
      // isn't a curve there but a whole area. a hundredth of a pixel apart fixes it
      constexpr double NUDGE = 0.01;
      for (size_t i = 0; i < sites.size(); i++)
      {
         for (size_t j = i + 1; j < sites.size(); j++)
         {
            if (std::abs(sites[j].x - sites[i].x) < NUDGE * 0.5)
               sites[j].x += NUDGE;
            if (std::abs(sites[j].y - sites[i].y) < NUDGE * 0.5)
               sites[j].y += NUDGE;
         }
      }
      return sites;
   }

   Polyline manhattanBisector(Point p1, Point p2, double width, double height)
   {
      const double xDistance = p1.x - p2.x;
      const double yDistance = p1.y - p2.y;
      const Point midpoint { (p1.x + p2.x) / 2.0, (p1.y + p2.y) / 2.0 };

      // straight lines for sites level with each other
      if (xDistance == 0.0)
         return { { 0.0, midpoint.y }, { width, midpoint.y } };
      if (yDistance == 0.0)
         return { { midpoint.x, 0.0 }, { midpoint.x, height } };

      // otherwise a 45 degree piece through the midpoint, between the two sites' rows or columns
      const double slope = yDistance / xDistance > 0.0 ? -1.0 : 1.0;
      const double intercept = midpoint.y - midpoint.x * slope;

      Polyline points;
      if (std::abs(xDistance) >= std::abs(yDistance))
      {
         // wider than tall: vertical rays up and down from the ends of the diagonal
         Point v1 { (p1.y - intercept) / slope, p1.y }, v2 { (p2.y - intercept) / slope, p2.y };
         if (v1.y > v2.y)
            std::swap(v1, v2);
         points = { { v1.x, 0.0 }, v1, v2, { v2.x, height } };
      }
      else
      {
         // taller than wide: horizontal rays left and right
         Point v1 { p1.x, p1.x * slope + intercept }, v2 { p2.x, p2.x * slope + intercept };
         if (v1.x > v2.x)
            std::swap(v1, v2);
         points = { { 0.0, v1.y }, v1, v2, { width, v2.y } };
      }

      for (auto& p : points)
      {
         p.x = juce::jlimit(0.0, width, p.x);
         p.y = juce::jlimit(0.0, height, p.y);
      }
      return points;
   }

   //==============================================================================
   Sector sectorAt(Point p, Point a, Point b, double width, double height)
   {
      return { sideHit(a, a - p, width, height), sideHit(p, p - a, width, height),
               sideHit(b, b - p, width, height), sideHit(p, p - b, width, height) };
   }

   double sectorConic(const Sector& s, Point a, Point b, Point p, double width, double height, Point* gradient)
   {
      // equal distance means  D_aEnter(p) D_aExit(a) / (D_aEnter(a) D_aExit(p)) = D_bEnter(p) D_bExit(b) / (D_bEnter(b) D_bExit(p)).
      // cross multiplied it's a product of two linear functions minus another: a conic, the same one
      // bisectorConicFromSector builds from the four face lines
      const double ka = sideDistance(s.aExit, a, width, height) * sideDistance(s.bEnter, b, width, height);
      const double kb = sideDistance(s.bExit, b, width, height) * sideDistance(s.aEnter, a, width, height);

      const double aEnter = sideDistance(s.aEnter, p, width, height), bExit = sideDistance(s.bExit, p, width, height);
      const double bEnter = sideDistance(s.bEnter, p, width, height), aExit = sideDistance(s.aExit, p, width, height);

      if (gradient != nullptr)
      {
         const auto g1 = sideGradient(s.aEnter) * bExit + sideGradient(s.bExit) * aEnter;
         const auto g2 = sideGradient(s.bEnter) * aExit + sideGradient(s.aExit) * bEnter;
         *gradient = g1 * ka - g2 * kb;
      }
      return ka * aEnter * bExit - kb * bEnter * aExit;
   }

   Polyline hilbertBisector(Point a, Point b, double width, double height)
   {
      auto difference = [&](Point p) { return hilbert(a, p, width, height) - hilbert(b, p, width, height); };

      // the hilbert midpoint, on the segment between them
      double lo = 0.0, hi = 1.0;
      for (int i = 0; i < 60; i++)
      {
         const double mid = 0.5 * (lo + hi);
         (difference(a + (b - a) * mid) < 0.0 ? lo : hi) = mid;
      }
      const auto start = a + (b - a) * (0.5 * (lo + hi));

      // in a sector the bisector is that sector's conic. its two terms are the two distance ratios, so
      // 1/2 ln of them (the distance difference) has the same zero set but doesn't jump in scale from one
      // sector to the next the way the raw conic does, which is what newton needs near the spokes
      auto differenceAndGradient = [&](Point q, Point& gradient) {
         const auto s = sectorAt(q, a, b, width, height);
         const double aEnter = sideDistance(s.aEnter, q, width, height), aExit = sideDistance(s.aExit, q, width, height);
         const double bEnter = sideDistance(s.bEnter, q, width, height), bExit = sideDistance(s.bExit, q, width, height);
         gradient = (sideGradient(s.aEnter) / aEnter - sideGradient(s.aExit) / aExit
                     - sideGradient(s.bEnter) / bEnter + sideGradient(s.bExit) / bExit) * 0.5;
         return difference(q);
      };

      // onto the bisector near q: newton steps, or if those wander off, a bisection across it
      auto project = [&](Point q) -> std::optional<Point> {
         const auto guess = q;
         Point g;
         for (int i = 0; i < 20; i++)
         {
            const double value = differenceAndGradient(q, g);
            const double g2 = g.x * g.x + g.y * g.y;
            if (g2 < 1e-24 || ! inside(q, width, height))
               break;
            if (std::abs(value) / std::sqrt(g2) < 1e-10)
               return q;
            q = q - g * (value / g2);
         }

         differenceAndGradient(guess, g);
         const double gl = length(g);
         if (gl < 1e-24)
            return std::nullopt;
         const auto normal = g / gl;
         for (double reach : { 1.0, 4.0, 16.0 })
         {
            auto lo = guess - normal * reach, hi = guess + normal * reach;
            if (! inside(lo, width, height) || ! inside(hi, width, height))
               continue;
            double flo = difference(lo), fhi = difference(hi);
            if ((flo < 0.0) == (fhi < 0.0))
               continue;
            for (int i = 0; i < 60; i++)
            {
               const auto mid = (lo + hi) * 0.5;
               const double fmid = difference(mid);
               ((fmid < 0.0) == (flo < 0.0) ? lo : hi) = mid;
            }
            return (lo + hi) * 0.5;
         }
         return std::nullopt;
      };
      auto tangent = [&](Point q) {
         Point g;
         differenceAndGradient(q, g);
         Point t { -g.y, g.x };
         const double l = length(t);
         return l > 1e-24 ? t / l : Point { 0.0, 0.0 };
      };

      // walk along it each way until it reaches the boundary
      auto walk = [&](double direction) {
         Polyline points;
         Point p = start;
         Point t = tangent(start) * direction;
         double step = 2.0;
         for (int i = 0; i < 20000 && step > 1e-3; i++)
         {
            const auto guess = p + t * step;
            if (! inside(guess, width, height))
            {
               points.push_back(borderCrossing(p, guess, width, height));
               break;
            }

            const auto next = project(guess);
            if (! next || length(*next - p) > 1.5 * step || std::abs(difference(*next)) > 1e-7)
            {
               step *= 0.5;
               continue;
            }

            // the straight piece to it has to stay on the curve too (checked in pixels at its middle)
            {
               Point g;
               const auto middle = (p + *next) * 0.5;
               const double off = differenceAndGradient(middle, g);
               const double gl = length(g);
               if (gl > 1e-12 && std::abs(off) / gl > 0.05 && step > 0.05)
               {
                  step *= 0.5;
                  continue;
               }
            }
            if (! inside(*next, width, height))
            {
               points.push_back(borderCrossing(p, *next, width, height));
               break;
            }

            auto nextTangent = tangent(*next);
            if (nextTangent.x * t.x + nextTangent.y * t.y < 0.0)
               nextTangent = nextTangent * -1.0;

            // small steps where it bends, bigger where it's straight
            const double turn = nextTangent.x * t.x + nextTangent.y * t.y;
            if (turn < 0.995 && step > 0.05)
            {
               step *= 0.5;
               continue;
            }
            points.push_back(*next);
            p = *next;
            t = nextTangent;
            if (turn > 0.9995)
               step = juce::jmin(step * 1.5, 4.0);
         }
         return points;
      };

      auto backward = walk(-1.0);
      const auto forward = walk(1.0);
      Polyline line(backward.rbegin(), backward.rend());
      line.push_back(start);
      line.insert(line.end(), forward.begin(), forward.end());
      return line;
   }

   //==============================================================================
   Diagram build(Type type, const std::vector<Point>& input, double width, double height, BisectorCache* cache)
   {
      Diagram diagram;
      if (type == Type::Euclidean)
         return diagram;

      // no duplicates (they have no bisector), hilbert sites have to be strictly inside the rectangle
      std::vector<Point> sites;
      for (auto p : input)
      {
         if (type == Type::Hilbert)
         {
            p.x = juce::jlimit(0.5, width - 0.5, p.x);
            p.y = juce::jlimit(0.5, height - 0.5, p.y);
         }
         bool duplicate = false;
         for (const auto& s : sites)
            duplicate = duplicate || length(s - p) < 1e-9;
         if (! duplicate)
            sites.push_back(p);
      }
      if (type == Type::Manhattan)
         sites = nudgeSquares(sites);
      else
         sites = nudgeAligned(sites);

      auto distance = [&](size_t k, Point p) {
         return type == Type::Manhattan ? manhattan(sites[k], p) : hilbert(sites[k], p, width, height);
      };

      // a cell wall between i and j is the part of their bisector where nobody else is any closer,
      // the same way Hilbert_K_Voronoi splits every bisector where the others cross it
      // only the yes/no matters, so it stops at the first site that's closer. the one that was closer last
      // time (along the same bisector) almost always still is, so that gets checked first
      auto onWall = [&](size_t i, size_t j, Point p, size_t& blocker) {
         const double own = 0.5 * (distance(i, p) + distance(j, p));
         if (blocker < sites.size() && distance(blocker, p) < own)
            return false;
         for (size_t k = 0; k < sites.size(); k++)
         {
            if (k != i && k != j && distance(k, p) < own)
            {
               blocker = k;
               return false;
            }
         }
         return true;
      };

      // anything left from another metric or size is no use
      if (cache != nullptr && (cache->type != type || cache->width != width || cache->height != height))
         *cache = { type, width, height, {} };
      std::map<std::array<double, 4>, Polyline> traced; // what this build used, the next one's cache

      auto getBisector = [&](Point a, Point b) {
         const std::array<double, 4> key { a.x, a.y, b.x, b.y };
         if (cache != nullptr)
            if (const auto found = cache->bisectors.find(key); found != cache->bisectors.end())
               return traced[key] = found->second;

         return traced[key] = type == Type::Manhattan ? manhattanBisector(a, b, width, height) : hilbertBisector(a, b, width, height);
      };

      constexpr double SAMPLE_SPACING = 2.0; // px between checks along a bisector
      const double tolerance = type == Type::Manhattan ? 1e-6 : 0.1;

      for (size_t i = 0; i < sites.size(); i++)
      {
         for (size_t j = i + 1; j < sites.size(); j++)
         {
            const auto bisector = getBisector(sites[i], sites[j]);
            if (bisector.size() < 2)
               continue;

            Polyline run;
            double runLength = 0.0;
            auto finish = [&] {
               if (run.size() >= 2 && runLength > 0.5)
               {
                  diagram.voronoiLines.push_back(simplify(run, tolerance));
                  diagram.neighbours.push_back({ static_cast<int>(i), static_cast<int>(j) });
               }
               run.clear();
               runLength = 0.0;
            };
            auto add = [&](Point p) {
               if (! run.empty())
                  runLength += length(p - run.back());
               run.push_back(p);
            };

            size_t blocker = sites.size();
            bool previousKept = onWall(i, j, bisector.front(), blocker);
            if (previousKept)
               add(bisector.front());

            for (size_t s = 0; s + 1 < bisector.size(); s++)
            {
               const auto u = bisector[s], v = bisector[s + 1];
               const int pieces = juce::jmax(1, static_cast<int>(std::ceil(length(v - u) / SAMPLE_SPACING)));
               for (int piece = 1; piece <= pieces; piece++)
               {
                  const double t0 = static_cast<double>(piece - 1) / pieces, t1 = static_cast<double>(piece) / pieces;
                  const auto q = u + (v - u) * t1;
                  const bool kept = onWall(i, j, q, blocker);

                  if (kept != previousKept)
                  {
                     // exactly where another site takes over (or lets go)
                     double lo = t0, hi = t1;
                     for (int k = 0; k < 40; k++)
                     {
                        const double mid = 0.5 * (lo + hi);
                        (onWall(i, j, u + (v - u) * mid, blocker) == previousKept ? lo : hi) = mid;
                     }
                     add(u + (v - u) * (0.5 * (lo + hi)));
                     if (previousKept)
                        finish();
                  }
                  if (kept && (piece == pieces || kept != previousKept))
                     add(q); // corners of the bisector, and right after it starts again
                  previousKept = kept;
               }
            }
            finish();
         }
      }

      // the middle of each cell, from a grid of samples
      constexpr double GRID = 8.0;
      std::vector<Point> sums(sites.size(), { 0.0, 0.0 });
      std::vector<int> counts(sites.size(), 0);
      for (double y = GRID * 0.5; y < height; y += GRID)
      {
         for (double x = GRID * 0.5; x < width; x += GRID)
         {
            size_t nearest = 0;
            double best = std::numeric_limits<double>::infinity();
            for (size_t k = 0; k < sites.size(); k++)
            {
               const double d = distance(k, { x, y });
               if (d < best)
               {
                  best = d;
                  nearest = k;
               }
            }
            if (! sites.empty())
            {
               sums[nearest] += Point { x, y };
               counts[nearest]++;
            }
         }
      }
      for (size_t k = 0; k < sites.size(); k++)
         diagram.cellCentres.push_back(counts[k] > 0 ? (sums[k] / static_cast<double>(counts[k])).toFloat() : sites[k].toFloat());

      if (cache != nullptr)
         cache->bisectors = std::move(traced);

      diagram.sites = sites;
      return diagram;
   }
}
