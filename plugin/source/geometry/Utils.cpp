#include "geometry/Utils.h"
#include <algorithm>
#include <cmath>

namespace GeoUtils
{
   bool pointsEqual(const Point &p1, const Point &p2, double eps)
   {
      return std::abs(p1.x - p2.x) <= eps && std::abs(p1.y - p2.y) <= eps;
   }

   enum OutCode
   {
      INSIDE = 0,
      LEFT = 1,
      RIGHT = 2,
      BOTTOM = 4,
      TOP = 8
   };

   OutCode computeOutCode(const Point &p, const BBox &box)
   {
      OutCode code = INSIDE;
      if (p.getX() < box.minX)
         code = (OutCode)(code | LEFT);
      else if (p.getX() > box.maxX)
         code = (OutCode)(code | RIGHT);
      if (p.getY() < box.minY)
         code = (OutCode)(code | BOTTOM);
      else if (p.getY() > box.maxY)
         code = (OutCode)(code | TOP);
      return code;
   }

   bool clipEdge(Edge &edge, const BBox &box)
   {
      OutCode outcode0 = computeOutCode(edge.u, box);
      OutCode outcode1 = computeOutCode(edge.v, box);

      while (true)
      {
         if (!(outcode0 | outcode1))
         { // both points inside
            return true;
         }
         else if (outcode0 & outcode1)
         { // both points outside, same region
            return false;
         }
         else
         {
            double x, y;
            OutCode outcodeOut = outcode0 ? outcode0 : outcode1;
            if (outcodeOut & TOP)
            {
               x = edge.u.getX() + (edge.v.getX() - edge.u.getX()) * (box.maxY - edge.u.getY()) / (edge.v.getY() - edge.u.getY());
               y = box.maxY;
            }
            else if (outcodeOut & BOTTOM)
            {
               x = edge.u.getX() + (edge.v.getX() - edge.u.getX()) * (box.minY - edge.u.getY()) / (edge.v.getY() - edge.u.getY());
               y = box.minY;
            }
            else if (outcodeOut & RIGHT)
            {
               y = edge.u.getY() + (edge.v.getY() - edge.u.getY()) * (box.maxX - edge.u.getX()) / (edge.v.getX() - edge.u.getX());
               x = box.maxX;
            }
            else
            { // LEFT
               y = edge.u.getY() + (edge.v.getY() - edge.u.getY()) * (box.minX - edge.u.getX()) / (edge.v.getX() - edge.u.getX());
               x = box.minX;
            }

            if (outcodeOut == outcode0)
            {
               edge.u = {x, y};
               outcode0 = computeOutCode(edge.u, box);
            }
            else
            {
               edge.v = {x, y};
               outcode1 = computeOutCode(edge.v, box);
            }
         }
      }
   }

   std::vector<Point> clipPolygon(const std::vector<Point> &poly, const BBox &box)
   {
      std::vector<Point> output = poly;
      for (int edge = 0; edge < 4; ++edge)
      { // 0: left, 1: right, 2: bottom, 3: top
         std::vector<Point> input = output;
         output.clear();
         if (input.empty())
            break;

         Point S = input.back();
         for (const Point &E : input)
         {
            bool s_inside = (edge == 0) ? S.x >= box.minX : (edge == 1) ? S.x <= box.maxX
                                                        : (edge == 2)   ? S.y >= box.minY
                                                                        : S.y <= box.maxY;
            bool e_inside = (edge == 0) ? E.x >= box.minX : (edge == 1) ? E.x <= box.maxX
                                                        : (edge == 2)   ? E.y >= box.minY
                                                                        : E.y <= box.maxY;

            if (e_inside)
            {
               if (!s_inside)
               {
                  // Intersect S->E with the boundary
                  double ix, iy;
                  if (edge == 0)
                  {
                     ix = box.minX;
                     iy = S.y + (E.y - S.y) * (box.minX - S.x) / (E.x - S.x);
                  }
                  else if (edge == 1)
                  {
                     ix = box.maxX;
                     iy = S.y + (E.y - S.y) * (box.maxX - S.x) / (E.x - S.x);
                  }
                  else if (edge == 2)
                  {
                     iy = box.minY;
                     ix = S.x + (E.x - S.x) * (box.minY - S.y) / (E.y - S.y);
                  }
                  else
                  {
                     iy = box.maxY;
                     ix = S.x + (E.x - S.x) * (box.maxY - S.y) / (E.y - S.y);
                  }
                  output.push_back({ix, iy});
               }
               output.push_back(E);
            }
            else if (s_inside)
            {
               double ix, iy;
               if (edge == 0)
               {
                  ix = box.minX;
                  iy = S.y + (E.y - S.y) * (box.minX - S.x) / (E.x - S.x);
               }
               else if (edge == 1)
               {
                  ix = box.maxX;
                  iy = S.y + (E.y - S.y) * (box.maxX - S.x) / (E.x - S.x);
               }
               else if (edge == 2)
               {
                  iy = box.minY;
                  ix = S.x + (E.x - S.x) * (box.minY - S.y) / (E.y - S.y);
               }
               else
               {
                  iy = box.maxY;
                  ix = S.x + (E.x - S.x) * (box.maxY - S.y) / (E.y - S.y);
               }
               output.push_back({ix, iy});
            }
            S = E;
         }
      }
      return output;
   }
}
