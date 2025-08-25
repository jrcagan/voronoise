#pragma once
#include "geometry/Utils.h"
#include <vector>

namespace Delaunay
{
    std::vector<GeoUtils::Triangle> triangulate(const std::vector<GeoUtils::Point> &points);

    GeoUtils::Circumcircle getCircumcircle(const GeoUtils::Triangle &t);
}
