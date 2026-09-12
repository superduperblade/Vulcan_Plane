// Geospatial coordinate core (implementation step 2).
//
// Canonical world representation (decision 0001, docs/decisions/):
//   - Storage / identity: ECEF, WGS-84, DOUBLE precision (glm::dvec3).
//   - Human-facing / geographic data: geodetic LLA (degrees, meters).
//   - Rendering: float32 in a local ENU frame relative to a moving origin
//     (LocalFrame). The float32 conversion happens ONLY at the render
//     boundary; see docs/PRECISION.md for the precision contract every
//     downstream system (terrain, streaming, LOD, camera) codes against.
//
// All math here is double precision and pure CPU (glm only, no Vulkan).

#pragma once

#include <glm/glm.hpp>
#include <glm/mat3x3.hpp>

namespace vp::geo {

// WGS-84 ellipsoid (ITU-R P.373-7). Derived from the two defining constants
// a and f so there is no transcription risk for the derived values.
struct Ellipsoid {
  double a;    // semi-major axis (m)
  double b;    // semi-minor axis (m)
  double e2;   // first eccentricity squared
  double ep2;  // second eccentricity squared
  static const Ellipsoid WGS84;
};

// Geodetic position (WGS-84).
struct LLA {
  double latDeg = 0.0;  // [-90, 90]; clamped on conversion
  double lonDeg = 0.0;  // wrapped to [-180, 180] on conversion
  double altM = 0.0;    // height above the WGS-84 ellipsoid (NOT mean sea
                        // level; a geoid model is a later concern)
};

// ECEF position (WGS-84), meters. X toward (lat 0, lon 0), Y toward
// (lat 0, lon +90), Z toward the north pole. Double precision is mandatory:
// float32 quantizes to ~0.5 m at planetary radii (docs/PRECISION.md).
using ECEF = glm::dvec3;

// LLA -> ECEF. Closed form, exact for the WGS-84 ellipsoid up to double
// rounding (~1e-9 m).
ECEF llaToEcef(const LLA& in, const Ellipsoid& e = Ellipsoid::WGS84);

// ECEF -> LLA. Exact fixed-point iteration (Bowring's closed-form initial
// estimate, then p*tan(lat) = z + N*e2*sin(lat) to double precision).
// Round-trip LLA->ECEF->LLA is < 1 mm anywhere in the engine's domain
// (verified by test/geo_test.cpp). The geodetic representation is unique
// for h > ~-6.34e6 m; far-deeper interior points have multiple solutions
// and the branch nearest the surface is returned.
LLA ecefToLla(const ECEF& p, const Ellipsoid& ell = Ellipsoid::WGS84);

// East-North-Up frame anchored at a world position.
//
// All transforms are double precision. The float32 conversion for the GPU
// happens at the render boundary and is characterized in docs/PRECISION.md.
//
// At the poles the frame is well-defined and deterministic: "up" points
// away from the pole, "east" follows the convention of the origin's
// longitude (0 at the exact pole, which ecefToLla returns).
class LocalFrame {
 public:
  explicit LocalFrame(const ECEF& origin);  // origin in ECEF (WGS-84)
  explicit LocalFrame(const LLA& origin);

  [[nodiscard]] ECEF origin() const { return origin_; }
  [[nodiscard]] const LLA& originLla() const { return originLla_; }

  // World -> local (meters, ENU).
  [[nodiscard]] glm::dvec3 toLocal(const ECEF& p) const;
  // Local (meters, ENU) -> world.
  [[nodiscard]] ECEF toWorld(const glm::dvec3& local) const;

  // Orthonormal basis; columns are the E, N, U unit vectors in ECEF.
  [[nodiscard]] const glm::dmat3& basis() const { return basis_; }

 private:
  ECEF origin_;
  LLA originLla_;
  glm::dmat3 basis_;  // columns: east, north, up
};

// Distance between two ECEF points (m).
double distanceEcef(const ECEF& a, const ECEF& b);

// Convert a local position from one frame to another (double precision).
// This is the primitive re-anchoring uses when the camera moves far enough
// from the current frame origin (docs/PRECISION.md).
glm::dvec3 convertLocal(const LocalFrame& from, const LocalFrame& to,
                        const glm::dvec3& local);

}  // namespace vp::geo
