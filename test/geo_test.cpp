// Unit tests for the geospatial coordinate core (vp_geo).
//
// Hard precision budgets (contract: docs/PRECISION.md):
//   - LLA -> ECEF -> LLA round-trip: < 1 mm anywhere
//     (1e-3 m altitude, 1e-8 deg latitude/longitude ~= 1.1 mm).
//   - float32 local-space round-trip at distance d from the frame origin:
//     below the documented budgets at 1 m / 1 km / 100 km / 1000 km, and
//     never above the theoretical bound 2^-24 * d (plus double-rounding
//     margin).
//   - Re-anchoring (frame-to-frame) conversion in double: < 1e-6 m.
//
// Run:
//   ./build/vp_geo_tests            # all tests (exit code = failure count)
//   ./build/vp_geo_tests --report   # print measured float32 error vs distance

#include "geospatial/geo.h"

#include "test.h"

#include <cmath>
#include <cstdio>
#include <random>

using namespace vp::geo;

namespace {

// Round-trip budgets (docs/PRECISION.md).
const double kLatTolDeg = 1e-8;  // ~1.1 mm
const double kLonTolDeg = 1e-8;
const double kAltTolM = 1e-3;    // 1 mm

// Wrap-aware longitude difference in degrees, in [0, 180].
double lonDiffDeg(double a, double b) {
  double d = std::fmod(a - b + 180.0, 360.0);
  if (d < 0.0) d += 360.0;
  return std::fabs(d - 180.0);
}

void checkLlaRoundTrip(const LLA& in) {
  const ECEF e = llaToEcef(in);
  const LLA out = ecefToLla(e);
  VP_CHECK(std::isfinite(out.latDeg) && std::isfinite(out.lonDeg) &&
           std::isfinite(out.altM));
  VP_CHECK_NEAR(out.latDeg, in.latDeg, kLatTolDeg);
  // Longitude is undefined at the exact poles; skip it there.
  if (std::fabs(in.latDeg) < 90.0)
    VP_CHECK_NEAR(lonDiffDeg(out.lonDeg, in.lonDeg), 0.0, kLonTolDeg);
  VP_CHECK_NEAR(out.altM, in.altM, kAltTolM);
}

// Directions sampled for local-frame tests: axes + off-axis.
const glm::dvec3 kDirs[] = {
    {1, 0, 0},
    {0, 1, 0},
    {0, 0, 1},
    glm::normalize(glm::dvec3(1, 1, 1)),
    glm::normalize(glm::dvec3(-1, 2, -3)),
    glm::normalize(glm::dvec3(3, -1, 2)),
};

}  // namespace

VP_TEST(lla_ecef_axis_points) {
  // Exact values on the axes (no computation involved — these pin the
  // WGS-84 constants).
  const ECEF eq = llaToEcef({0.0, 0.0, 0.0});
  VP_CHECK_NEAR(eq.x, 6378137.0, 1e-9);
  VP_CHECK_NEAR(eq.y, 0.0, 1e-9);
  VP_CHECK_NEAR(eq.z, 0.0, 1e-9);
  const ECEF mer = llaToEcef({0.0, 90.0, 0.0});
  VP_CHECK_NEAR(mer.y, 6378137.0, 1e-9);
  VP_CHECK_NEAR(mer.x, 0.0, 1e-9);
  VP_CHECK_NEAR(mer.z, 0.0, 1e-9);
  const ECEF np = llaToEcef({90.0, 0.0, 0.0});
  VP_CHECK_NEAR(np.z, Ellipsoid::WGS84.b, 1e-9);
  VP_CHECK_NEAR(np.x, 0.0, 1e-9);
  VP_CHECK_NEAR(np.y, 0.0, 1e-9);
  const ECEF sp = llaToEcef({-90.0, 0.0, 0.0});
  VP_CHECK_NEAR(sp.z, -Ellipsoid::WGS84.b, 1e-9);

  // Reference values computed independently (double precision, WGS-84
  // closed form): LLA(45, 0, 0) and LLA(-33.8688, 151.2093, 58) [Sydney].
  const ECEF p45 = llaToEcef({45.0, 0.0, 0.0});
  VP_CHECK_NEAR(p45.x, 4517590.878848932, 1e-6);
  VP_CHECK_NEAR(p45.y, 0.0, 1e-6);
  VP_CHECK_NEAR(p45.z, 4487348.408865919, 1e-6);
  const ECEF syd = llaToEcef({-33.8688, 151.2093, 58.0});
  VP_CHECK_NEAR(syd.x, -4646093.477288303, 1e-6);
  VP_CHECK_NEAR(syd.y, 2553229.5358170713, 1e-6);
  VP_CHECK_NEAR(syd.z, -3534404.710910369, 1e-6);
}

VP_TEST(lla_ecef_symmetry) {
  // 90-degree longitude rotation: (x, y) -> (-y, x).
  const ECEF p = llaToEcef({45.0, 30.0, 123.0});
  const ECEF q = llaToEcef({45.0, 120.0, 123.0});
  VP_CHECK_NEAR(q.x, -p.y, 1e-6);
  VP_CHECK_NEAR(q.y, p.x, 1e-6);
  VP_CHECK_NEAR(q.z, p.z, 1e-9);

  // Antipode: LLA(-lat, lon+180, h) -> -ECEF(lat, lon, h).
  const ECEF r = llaToEcef({-45.0, 210.0, 123.0});
  VP_CHECK_NEAR(r.x, -p.x, 1e-6);
  VP_CHECK_NEAR(r.y, -p.y, 1e-6);
  VP_CHECK_NEAR(r.z, -p.z, 1e-6);
}

VP_TEST(lla_roundtrip_grid) {
  const double lats[] = {-90.0, -89.9, -60.0, -45.0, -23.4365, 0.0, 23.4365,
                         45.0, 60.0, 89.9, 90.0};
  const double lons[] = {-180.0, -179.999, -120.0, -45.0, 0.0, 45.0, 120.0,
                         179.999, 180.0};
  const double alts[] = {-5000.0, -1000.0, 0.0, 100.0, 8848.0, 100000.0,
                         1000000.0};
  for (double lat : lats)
    for (double lon : lons)
      for (double alt : alts) checkLlaRoundTrip({lat, lon, alt});
}

VP_TEST(lla_roundtrip_random) {
  std::mt19937 rng(12345);
  std::uniform_real_distribution<double> lat(-90.0, 90.0);
  std::uniform_real_distribution<double> lon(-180.0, 180.0);
  std::uniform_real_distribution<double> alt(-5000.0, 2000000.0);
  for (int i = 0; i < 10000; ++i)
    checkLlaRoundTrip({lat(rng), lon(rng), alt(rng)});
}

VP_TEST(poles) {
  for (double alt : {0.0, 1000.0, -1000.0, 1000000.0})
    for (int sign : {1, -1}) {
      const LLA in{sign * 90.0, 0.0, alt};
      const ECEF e = llaToEcef(in);
      VP_CHECK_NEAR(e.x, 0.0, 1e-9);
      VP_CHECK_NEAR(e.y, 0.0, 1e-9);
      VP_CHECK_NEAR(e.z, sign * (Ellipsoid::WGS84.b + alt), 1e-6);
      const LLA out = ecefToLla(e);
      VP_CHECK_NEAR(out.latDeg, sign * 90.0, 1e-12);
      VP_CHECK_NEAR(out.altM, alt, kAltTolM);
    }
  // Just off the axis, near the pole.
  checkLlaRoundTrip({89.999999, 45.0, 0.0});
  checkLlaRoundTrip({-89.999999, -45.0, 100.0});
}

VP_TEST(dateline) {
  // +/-180 are the same meridian.
  const ECEF east = llaToEcef({0.0, 180.0, 0.0});
  const ECEF west = llaToEcef({0.0, -180.0, 0.0});
  VP_CHECK(glm::length(east - west) < 1e-6);
  // Longitude wrapping.
  const ECEF w1 = llaToEcef({10.0, 190.0, 50.0});
  const ECEF w2 = llaToEcef({10.0, -170.0, 50.0});
  VP_CHECK(glm::length(w1 - w2) < 1e-6);
  // Round-trip keeps the dateline within tolerance (sign of zero decides
  // which of +/-180 comes back).
  const LLA out = ecefToLla(east);
  VP_CHECK_NEAR(std::fabs(out.lonDeg), 180.0, kLonTolDeg);
}

VP_TEST(altitude_extremes) {
  // Deep below the ellipsoid (deepest trench is ~-11 km), ISS altitude,
  // and far above it. Note: geodetic representation is unique for
  // h > ~-6.34e6 m (the Jacobian fold); far-deeper interior points have
  // multiple (lat, h) solutions and are outside the engine's domain.
  const double alts[] = {-20000.0, -10000.0, -1000.0, 0.0, 1000.0, 42164.0,
                         1000000.0, 40000000.0};
  const double lats[] = {0.0, 45.0, -60.0};
  const double lons[] = {0.0, 90.0, -120.0};
  for (double alt : alts)
    for (double lat : lats)
      for (double lon : lons) checkLlaRoundTrip({lat, lon, alt});
}

VP_TEST(local_frame_roundtrip) {
  const LLA origins[] = {
      {0.0, 0.0, 0.0},          {45.0, 10.0, 100.0},
      {-33.8688, 151.2093, 58}, {90.0, 0.0, 0.0},   // north pole
      {-90.0, 0.0, 0.0},        {0.0, 180.0, 1000}, // dateline
  };
  for (const auto& o : origins) {
    const LocalFrame f(llaToEcef(o));
    // Origin maps to zero.
    VP_CHECK(glm::length(f.toLocal(f.origin())) < 1e-9);
    // Basis is orthonormal.
    const glm::dmat3 I = f.basis() * glm::transpose(f.basis());
    for (int r = 0; r < 3; ++r)
      for (int c = 0; c < 3; ++c)
        VP_CHECK_NEAR(I[r][c], r == c ? 1.0 : 0.0, 1e-12);
    // toWorld/toLocal round-trip at increasing distances.
    for (double d : {1.0, 100.0, 1e3, 1e5, 1e6})
      for (const auto& dir : kDirs) {
        const glm::dvec3 local = dir * d;
        const ECEF w = f.toWorld(local);
        VP_CHECK(glm::length(f.toLocal(w) - local) < 1e-9 * d);
      }
  }
}

VP_TEST(float32_local_precision) {
  // The render-boundary conversion: local double -> float32 -> double.
  // Budgets: 1 m -> 1e-6 m, 1 km -> 1e-4 m, 100 km -> 1e-2 m,
  // 1000 km -> 0.2 m (docs/PRECISION.md).
  const LocalFrame f(llaToEcef({47.3769, 8.5417, 540.0}));
  struct {
    double d;
    double budget;
  } cases[] = {{1.0, 1e-6}, {1e3, 1e-4}, {1e5, 1e-2}, {1e6, 0.2}};
  for (const auto& c : cases) {
    double worst = 0.0;
    for (const auto& dir : kDirs) {
      const glm::dvec3 local = dir * c.d;
      const ECEF trueW = f.toWorld(local);
      const glm::vec3 flocal = glm::vec3(local);  // double -> float32
      const ECEF backW = f.toWorld(glm::dvec3(flocal));
      worst = std::max(worst, glm::length(trueW - backW));
    }
    std::fprintf(stderr,
                 "  float32 local precision: d=%8.0f m -> worst %.3e m "
                 "(budget %.1e m)\n",
                 c.d, worst, c.budget);
    VP_CHECK(worst < c.budget);
  }
}

VP_TEST(float32_error_bound) {
  // Theoretical bound from docs/PRECISION.md: per-component float32
  // rounding is <= 2^-24 * |component|, so the vector error is
  // <= 2^-24 * |local|; add a margin for the double-precision matrix
  // multiply in toWorld.
  const LocalFrame f(llaToEcef({45.0, 10.0, 100.0}));
  for (double d : {1.0, 1e2, 1e4, 1e6}) {
    double worst = 0.0;
    for (const auto& dir : kDirs) {
      const glm::dvec3 local = dir * d;
      const ECEF trueW = f.toWorld(local);
      const ECEF backW = f.toWorld(glm::dvec3(glm::vec3(local)));
      worst = std::max(worst, glm::length(trueW - backW));
    }
    const double bound = 6e-8 * d + 1e-7;
    std::fprintf(stderr, "  float32 bound check: d=%8.0f m -> worst %.3e m "
                         "(bound %.3e m)\n",
                 d, worst, bound);
    VP_CHECK(worst < bound);
  }
}

VP_TEST(reanchor_frame_conversion) {
  // convertLocal between frames ~500 km apart stays at double precision.
  const LocalFrame a(llaToEcef({47.3769, 8.5417, 540.0}));   // Zurich-ish
  const LocalFrame b(llaToEcef({48.8566, 2.3522, 35.0}));    // Paris-ish
  VP_CHECK(distanceEcef(a.origin(), b.origin()) > 4e5);
  for (double d : {1.0, 1e3, 1e5})
    for (const auto& dir : kDirs) {
      const glm::dvec3 la = dir * d;
      const ECEF w = a.toWorld(la);
      const glm::dvec3 lb = convertLocal(a, b, la);
      VP_CHECK(glm::length(b.toWorld(lb) - w) < 1e-6);
      // The point's distance from the new origin is preserved.
      VP_CHECK_NEAR(glm::length(lb), distanceEcef(w, b.origin()), 1e-6);
    }
}

VP_TEST(ecef_center_degenerate) {
  // The Earth's center is a degenerate LLA; it must not crash and must
  // produce finite values (convention: lat 0, alt -b).
  const LLA c = ecefToLla({0.0, 0.0, 0.0});
  VP_CHECK(std::isfinite(c.latDeg) && std::isfinite(c.lonDeg) &&
           std::isfinite(c.altM));
  VP_CHECK_NEAR(c.altM, -Ellipsoid::WGS84.b, 1e-6);
}

// --report: measure float32 error across distances (for docs/PRECISION.md).
void reportFloat32() {
  const LocalFrame f(llaToEcef({47.3769, 8.5417, 540.0}));
  std::fprintf(stderr, "float32 local-space error vs distance from origin:\n");
  for (double d : {1.0, 10.0, 100.0, 1e3, 1e4, 1e5, 1e6, 1e7}) {
    double worst = 0.0;
    for (const auto& dir : kDirs) {
      const glm::dvec3 local = dir * d;
      const ECEF trueW = f.toWorld(local);
      const ECEF backW = f.toWorld(glm::dvec3(glm::vec3(local)));
      worst = std::max(worst, glm::length(trueW - backW));
    }
    std::fprintf(stderr, "  d=%9.0f m  worst error = %.3e m  (bound %.3e m)\n",
                 d, worst, 5.960464477539063e-8 * d);
  }
}

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--report") {
    reportFloat32();
    return 0;
  }
  const int failures = vpt::runAll();
  const int total = static_cast<int>(vpt::cases().size());
  if (failures == 0)
    std::fprintf(stderr, "OK: %d test cases, 0 failures\n", total);
  else
    std::fprintf(stderr, "FAILED: %d failed checks in %d test cases\n",
                 failures, total);
  return failures == 0 ? 0 : 1;
}
