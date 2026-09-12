// Geospatial coordinate core (implementation step 2).
// See geo.h for the coordinate model and docs/PRECISION.md for the
// precision contract.

#include "geospatial/geo.h"

#include <algorithm>
#include <cmath>

namespace vp::geo {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
constexpr double kRadToDeg = 180.0 / kPi;

// WGS-84 defining constants; the rest are derived so they stay consistent.
constexpr double kA = 6378137.0;            // semi-major axis (m), exact
constexpr double kF = 1.0 / 298.257223563;  // flattening, exact
constexpr double kB = kA * (1.0 - kF);      // semi-minor axis (m)
constexpr double kE2 = kF * (2.0 - kF);     // first eccentricity squared
constexpr double kEp2 = (kA * kA - kB * kB) / (kB * kB);  // second ecc. sq.

double degToRad(double deg) { return deg * kDegToRad; }
double radToDeg(double rad) { return rad * kRadToDeg; }

// Wrap a longitude (degrees) to [-180, 180]. 180 and -180 are the same
// meridian; both are accepted and both may be produced (atan2's sign of
// zero decides at exactly +/-180).
double normalizeLonDeg(double lon) {
  double l = std::fmod(lon + 180.0, 360.0);
  if (l < 0.0) {
    l += 360.0;
  }
  return l - 180.0;
}

}  // namespace

const Ellipsoid Ellipsoid::WGS84{kA, kB, kE2, kEp2};

ECEF llaToEcef(const LLA& in, const Ellipsoid& e) {
  const double latDeg = std::clamp(in.latDeg, -90.0, 90.0);
  const double lonDeg = normalizeLonDeg(in.lonDeg);
  const double lat = degToRad(latDeg);
  const double lon = degToRad(lonDeg);
  const double sinLat = std::sin(lat);
  const double cosLat = std::cos(lat);
  // Radius of curvature in the prime vertical.
  const double N = e.a / std::sqrt(1.0 - e.e2 * sinLat * sinLat);
  const double x = (N + in.altM) * cosLat * std::cos(lon);
  const double y = (N + in.altM) * cosLat * std::sin(lon);
  const double z = (N * (1.0 - e.e2) + in.altM) * sinLat;
  return {x, y, z};
}

LLA ecefToLla(const ECEF& p, const Ellipsoid& e) {
  const double x = p.x, y = p.y, z = p.z;
  const double rho = std::sqrt(x * x + y * y);

  LLA out;
  out.lonDeg = normalizeLonDeg(radToDeg(std::atan2(y, x)));

  // On the polar axis: latitude is +/-90, longitude is undefined (0 by
  // convention). rho < 1e-9 m only happens for points essentially on the
  // axis; the approximation error there is far below a nanometer.
  if (rho < 1e-9) {
    out.latDeg = z >= 0.0 ? 90.0 : -90.0;
    out.altM = std::fabs(z) - e.b;
    return out;
  }

  // Exact fixed-point iteration, derived from the defining equations
  //   p = (N + h) cos(lat),  z = (N (1 - e2) + h) sin(lat)
  // which eliminate h to  p tan(lat) = z + N e2 sin(lat).
  // Starting from Bowring's closed-form estimate, the error shrinks by
  // ~e2 per iteration, so a few iterations reach double precision.
  double lat = std::atan2(z, rho * (1.0 - e.e2));
  for (int i = 0; i < 6; ++i) {
    const double s = std::sin(lat);
    const double N = e.a / std::sqrt(1.0 - e.e2 * s * s);
    const double latNew = std::atan2(z + e.e2 * N * s, rho);
    if (std::fabs(latNew - lat) < 1e-15) {
      lat = latNew;
      break;
    }
    lat = latNew;
  }

  const double s = std::sin(lat), c = std::cos(lat);
  const double N = e.a / std::sqrt(1.0 - e.e2 * s * s);
  out.latDeg = radToDeg(lat);
  // rho/c is stable near the poles: rho ~ (N+h) cos(lat), so the ratio
  // stays finite. Negative altitudes (below the ellipsoid) come out
  // negative naturally.
  out.altM = rho / c - N;
  return out;
}

LocalFrame::LocalFrame(const ECEF& origin)
    : origin_(origin), originLla_(ecefToLla(origin)), basis_() {
  const double lat = degToRad(originLla_.latDeg);
  const double lon = degToRad(originLla_.lonDeg);
  const double sLat = std::sin(lat), cLat = std::cos(lat);
  const double sLon = std::sin(lon), cLon = std::cos(lon);
  // Columns: east, north, up (unit vectors in ECEF).
  basis_ = glm::dmat3(glm::dvec3(-sLon, cLon, 0.0),
                      glm::dvec3(-sLat * cLon, -sLat * sLon, cLat),
                      glm::dvec3(cLat * cLon, cLat * sLon, sLat));
}

LocalFrame::LocalFrame(const LLA& origin) : LocalFrame(llaToEcef(origin)) {}

glm::dvec3 LocalFrame::toLocal(const ECEF& p) const {
  // basis_ is orthonormal, so the inverse transform is the transpose.
  return glm::transpose(basis_) * (p - origin_);
}

ECEF LocalFrame::toWorld(const glm::dvec3& local) const {
  return origin_ + basis_ * local;
}

double distanceEcef(const ECEF& a, const ECEF& b) { return glm::length(a - b); }

glm::dvec3 convertLocal(const LocalFrame& from, const LocalFrame& to,
                        const glm::dvec3& local) {
  return to.toLocal(from.toWorld(local));
}

}  // namespace vp::geo
