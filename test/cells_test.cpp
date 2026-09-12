// Unit tests for world cell addressing (contract: src/geospatial/cells.h,
// decision 0002: docs/decisions/0002-world-cell-addressing.md).
//
// Tested invariants (decision 0002, "Quality Considerations"):
//   - layout constants: 2^(L+1) x 2^L cells per level, exact binary
//     fraction spans (180 degrees at L0);
//   - CellId packing round-trips and field validation (reserved bits zero,
//     out-of-range inputs yield invalid());
//   - pinned root/edge/pole addresses; ECEF <-> LLA addressing equality;
//   - edge uniqueness (each point maps to exactly one cell; longitude
//     intervals are [west, east), latitude intervals (south, north]
//     north-closed);
//   - dateline wrap (+180 == -180, modular east/west neighbors);
//   - pole-row conventions and across-pole adjacency;
//   - parent(children(c)) == c and the core level-consistency property
//     ancestorAtLevel(cellOf(p, L), L') == cellOf(p, L');
//   - neighbor geometric adjacency (bit-exact shared edges, dateline wrap,
//     dy clamping at the poles);
//   - ellipsoidal extent sanity (L0 pins, halving per level, cos-latitude
//     width shrink);
//   - frame independence: CellId depends only on the canonical ECEF
//     position, never on LocalFrame state.
//
// The suite is quiet (no printing; the harness reports failures to
// stderr). main() lives in geo_test.cpp: both files link into the
// vp_geo_tests binary and register their cases through test.h.
//
// Determinism: every random point set comes from a fixed-seed std::mt19937
// generator (agents.md) so failures reproduce exactly.

#include "geospatial/cells.h"

#include <cmath>
#include <cstdio>
#include <random>

#include "geospatial/geo.h"
#include "test.h"

using namespace vp::geo;

namespace {

// Points within kEcefEdgeTolDeg of a cell edge are excluded from checks
// that involve an ECEF round-trip: ecefToLla's ~1e-13 degree rounding
// (amplified for longitude near the poles) may move such a point across
// the edge. Decision 0002 pins only that the same ECEF point always maps
// to the same cell. Level-25 edges are checked because they refine every
// coarser level's grid.
constexpr double kEcefEdgeTolDeg = 1e-9;

// Latitude cuts for ECEF-derived checks: longitude from ecefToLla is
// ill-conditioned near the poles (position error divided by the distance
// to the rotation axis), so points poleward of the cut are skipped there.
// The poles themselves are pinned separately via LLA addressing (poles).
constexpr double kEcefPoleCutDeg = 89.9;
constexpr double kFramePoleCutDeg = 89.9999;

// Frame-independence exclusion radius from any cell edge at the test level
// (decision 0002: "excluding points within round-trip tolerance of cell
// edges").
constexpr double kFrameEdgeTolDeg = 1e-6;

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;

// (coarse, fine) sampled level pairs, 0 <= coarse < fine <= 25, for the
// core level-consistency property.
struct LevelPair {
  int coarse;
  int fine;
};
constexpr LevelPair kLevelPairs[] = {{0, 1},   {1, 2},  {3, 4},  {0, 5},
                                     {5, 10},  {5, 14}, {0, 25}, {7, 25},
                                     {14, 25}, {24, 25}};

// Distance in degrees from p to the nearest lat/lon cell edge at the given
// level (the grid lines include the dateline and the poles). std::fmod is
// exact and the spans are exact binary fractions, so this adds no rounding
// of its own.
double cellEdgeDistanceDeg(const LLA& p, int level) {
  const double lonSpan = cellLonSpanDeg(level);
  double u = std::fmod(p.lonDeg + 180.0, lonSpan);
  if (u < 0.0) {
    u += lonSpan;
  }
  const double lonDist = std::fmin(u, lonSpan - u);
  const double latSpan = cellLatSpanDeg(level);
  double v = std::fmod(90.0 - p.latDeg, latSpan);
  if (v < 0.0) {
    v += latSpan;
  }
  const double latDist = std::fmin(v, latSpan - v);
  return std::fmin(lonDist, latDist);
}

bool nearCellEdge(const LLA& p, int level, double tolDeg) {
  return cellEdgeDistanceDeg(p, level) < tolDeg;
}

// Latitude row containing lat 0. Latitude intervals are north-closed
// (south, north] -- both axes index by flooring a fraction measured from
// the north/west corner -- so the equator belongs to the northernmost row
// OF the southern band, whose north edge is the equator; level 0 has a
// single row.
int equatorRow(int level) { return cellYCount(level) / 2; }

// The 4 children's bounds tile the parent's bounds exactly (every bound is
// an exact binary fraction, so shared edges compare bit-exact).
void checkChildrenTileParent(const CellId& c) {
  const CellBounds b = bounds(c);
  const double midLon = (b.lonWestDeg + b.lonEastDeg) / 2.0;
  const double midLat = (b.latSouthDeg + b.latNorthDeg) / 2.0;
  const std::array<CellId, 4> ch = children(c);
  for (int k = 0; k < 4; ++k) {
    const CellBounds cb = bounds(ch.at(k));
    const bool westHalf = (k % 2) == 0;   // NW, SW
    const bool northHalf = (k / 2) == 0;  // NW, NE
    VP_CHECK(cb.lonWestDeg == (westHalf ? b.lonWestDeg : midLon));
    VP_CHECK(cb.lonEastDeg == (westHalf ? midLon : b.lonEastDeg));
    VP_CHECK(cb.latNorthDeg == (northHalf ? b.latNorthDeg : midLat));
    VP_CHECK(cb.latSouthDeg == (northHalf ? midLat : b.latSouthDeg));
  }
}

}  // namespace

VP_TEST(cell_layout_constants) {
  // Spot values.
  VP_CHECK(cellXCount(0) == 2);
  VP_CHECK(cellYCount(0) == 1);
  VP_CHECK(cellXCount(1) == 4);
  VP_CHECK(cellYCount(1) == 2);
  VP_CHECK(cellXCount(2) == 8);
  VP_CHECK(cellYCount(2) == 4);
  VP_CHECK(cellXCount(kMaxCellLevel) == 1 << (kMaxCellLevel + 1));
  VP_CHECK(cellYCount(kMaxCellLevel) == 1 << kMaxCellLevel);

  // Every level: 2^(L+1) longitude cells x 2^L latitude cells, with spans
  // of exactly 360/2^(L+1) and 180/2^L degrees. 360 and 180 are exact
  // binary fractions, so the division is exact: compare bit-exact.
  for (int level = 0; level <= kMaxCellLevel; ++level) {
    VP_CHECK(cellXCount(level) == 1 << (level + 1));
    VP_CHECK(cellYCount(level) == 1 << level);
    VP_CHECK(cellLonSpanDeg(level) == 360.0 / (1 << (level + 1)));
    VP_CHECK(cellLatSpanDeg(level) == 180.0 / (1 << level));
  }

  // Pinned spans, including the OGC WorldCRS84Quad level-8 cell size.
  VP_CHECK(cellLonSpanDeg(0) == 180.0);
  VP_CHECK(cellLatSpanDeg(0) == 180.0);
  VP_CHECK(cellLonSpanDeg(1) == 90.0);
  VP_CHECK(cellLatSpanDeg(1) == 90.0);
  VP_CHECK(cellLonSpanDeg(2) == 45.0);
  VP_CHECK(cellLonSpanDeg(3) == 22.5);
  VP_CHECK(cellLonSpanDeg(8) == 0.703125);
  VP_CHECK(cellLonSpanDeg(25) == 360.0 / 67108864.0);
  VP_CHECK(cellLatSpanDeg(25) == 180.0 / 33554432.0);
}

VP_TEST(cellid_pack_roundtrip) {
  // make/level/x/y round-trips at representative levels, including the
  // boundary indices 0 and count-1.
  const int levels[] = {0, 1, 2, 10, 24, 25};
  for (int level : levels) {
    const std::uint32_t xs[] = {
        0U, static_cast<std::uint32_t>(cellXCount(level) / 2),
        static_cast<std::uint32_t>(cellXCount(level) - 1)};
    const std::uint32_t ys[] = {
        0U, static_cast<std::uint32_t>(cellYCount(level) / 2),
        static_cast<std::uint32_t>(cellYCount(level) - 1)};
    for (std::uint32_t x : xs) {
      for (std::uint32_t y : ys) {
        const CellId c = CellId::make(level, x, y);
        VP_CHECK(c.valid());
        VP_CHECK(c.level() == level);
        VP_CHECK(c.x() == x);
        VP_CHECK(c.y() == y);
        VP_CHECK((c.bits >> 57) == 0);  // reserved bits stay zero
      }
    }
  }

  // The zero CellId is a VALID cell (level-0 west hemisphere), not "no
  // cell"; use invalid() for that.
  const CellId zero{0};
  VP_CHECK(zero.valid());
  VP_CHECK(zero.level() == 0);
  VP_CHECK(zero.x() == 0);
  VP_CHECK(zero.y() == 0);

  // The maximum fields at the top level are representable.
  const CellId top = CellId::make(25, (1U << 26) - 1, (1U << 25) - 1);
  VP_CHECK(top.valid());
  VP_CHECK(top.x() == (1U << 26) - 1);
  VP_CHECK(top.y() == (1U << 25) - 1);

  // Out-of-range inputs yield invalid(); they are not masked into a
  // different, valid-looking cell.
  VP_CHECK(CellId::make(-1, 0, 0) == CellId::invalid());
  VP_CHECK(CellId::make(26, 0, 0) == CellId::invalid());
  VP_CHECK(CellId::make(0, 0x4000000U, 0) == CellId::invalid());
  VP_CHECK(CellId::make(0, 0, 0x2000000U) == CellId::invalid());
  VP_CHECK(!CellId::make(kMaxCellLevel + 1, 0, 0).valid());

  // Junk in the reserved bits (bits >> 57 != 0) invalidates the id.
  CellId junk = CellId::make(5, 3, 7);
  VP_CHECK(junk.valid());
  junk.bits |= std::uint64_t{1} << 62;
  VP_CHECK((junk.bits >> 57) != 0);
  VP_CHECK(!junk.valid());

  VP_CHECK(!CellId::invalid().valid());
}

VP_TEST(cellid_str) {
  // "L/x/y" for logs, caches, debug UI.
  VP_CHECK(CellId::make(3, 8, 7).str() == "3/8/7");
  VP_CHECK(CellId::make(0, 1, 0).str() == "0/1/0");
  VP_CHECK(CellId::make(0, 0, 0).str() == "0/0/0");
  VP_CHECK(CellId::make(25, (1U << 26) - 1, (1U << 25) - 1).str() ==
           "25/67108863/33554431");
}

VP_TEST(cellof_roots) {
  // Pinned root addresses. y = 0 is the north row; lon 0 is the west edge
  // of the east hemisphere, and half-open [west, east) assigns it to x = 1.
  VP_CHECK(cellOf(LLA{0.0, 0.0, 0.0}, 0) == CellId::make(0, 1, 0));
  VP_CHECK(cellOf(LLA{45.0, -45.0, 0.0}, 1) == CellId::make(1, 1, 0));
  VP_CHECK(cellOf(LLA{-45.0, -135.0, 0.0}, 1) == CellId::make(1, 0, 1));
}

VP_TEST(cellof_ecef_matches_lla) {
  // Addressing is a pure function of the canonical position (decision
  // 0001/0002): going through llaToEcef must not change the cell. Interior
  // grid points here; edges and poles are pinned by the dedicated tests.
  const double lats[] = {-89.3, -62.4,   -45.8, -23.4365, -12.9, -0.7,
                         8.8,   23.4365, 41.2,  63.6,     89.1};
  const double lons[] = {-175.3, -135.7, -90.5, -45.2, -10.9, -3.3,
                         7.7,    22.1,   45.6,  90.3,  135.9, 179.3};
  const int levels[] = {0, 5, 14, 25};
  for (double lat : lats) {
    for (double lon : lons) {
      const LLA p{lat, lon, 0.0};
      if (std::fabs(lat) > kEcefPoleCutDeg ||
          nearCellEdge(p, kMaxCellLevel, kEcefEdgeTolDeg)) {
        continue;  // too close to an edge/pole for ecefToLla's rounding
      }
      const ECEF e = llaToEcef(p);
      for (int level : levels) {
        VP_CHECK(cellOf(e, level) == cellOf(p, level));
      }
    }
  }

  // 10000 fixed-seed random points. The exclusion keeps points whose
  // ecefToLla rounding could cross an edge out of the comparison; every
  // remaining point must match exactly at all tested levels.
  // NOLINTNEXTLINE(bugprone-random-generator-seed)
  std::mt19937 rng(12345);
  std::uniform_real_distribution<double> lat(-90.0, 90.0);
  std::uniform_real_distribution<double> lon(-180.0, 180.0);
  std::uniform_real_distribution<double> alt(-5000.0, 10000.0);
  for (int i = 0; i < 10000; ++i) {
    LLA p;
    do {
      p = {lat(rng), lon(rng), alt(rng)};
    } while (std::fabs(p.latDeg) > kEcefPoleCutDeg ||
             nearCellEdge(p, kMaxCellLevel, kEcefEdgeTolDeg));
    const ECEF e = llaToEcef(p);
    for (int level : levels) {
      VP_CHECK(cellOf(e, level) == cellOf(p, level));
    }
  }
}

VP_TEST(cellof_containment) {
  // Sampled cells (corners, domain edges, middle) at a few levels.
  const int levels[] = {0, 2, 8, 20};
  for (int level : levels) {
    const int yCount = cellYCount(level);
    const int xSamples[] = {0, cellXCount(level) / 2, cellXCount(level) - 1};
    const int ySamples[] = {0, yCount / 2, yCount - 1};
    const double lonIn = cellLonSpanDeg(level) * 1e-6;
    const double latIn = cellLatSpanDeg(level) * 1e-6;
    for (int x : xSamples) {
      for (int y : ySamples) {
        const CellId c = CellId::make(level, x, y);
        const CellBounds b = bounds(c);
        const double centerLon = (b.lonWestDeg + b.lonEastDeg) / 2.0;
        const double centerLat = (b.latSouthDeg + b.latNorthDeg) / 2.0;

        // The center addresses back to the cell (LLA and ECEF) and the
        // cell contains it.
        const LLA center{centerLat, centerLon, 0.0};
        VP_CHECK(cellOf(center, level) == c);
        VP_CHECK(cellOf(llaToEcef(center), level) == c);
        VP_CHECK(contains(c, center));
        VP_CHECK(contains(c, llaToEcef(center)));

        // The SW corner just inside the west and south edges also
        // addresses back: longitude containment is [west, east), latitude
        // containment north-closed (south, north].
        const LLA sw{b.latSouthDeg + latIn, b.lonWestDeg + lonIn, 0.0};
        VP_CHECK(cellOf(sw, level) == c);
        VP_CHECK(contains(c, sw));

        // Latitude intervals are north-closed (south, north]: the point
        // exactly ON the north edge addresses back to the cell. For the
        // top row the edge is the pole itself, which clamps into row 0.
        const LLA northEdge{b.latNorthDeg, centerLon, 0.0};
        VP_CHECK(cellOf(northEdge, level) == c);
        VP_CHECK(contains(c, northEdge));

        // Just east of the east edge: the east neighbor (wrapping across
        // the dateline for the last column), not this cell.
        const LLA eastOf{centerLat, b.lonEastDeg + lonIn, 0.0};
        const CellId east = neighbor(c, 1, 0);
        VP_CHECK(cellOf(eastOf, level) == east);
        VP_CHECK(!contains(c, eastOf));
        VP_CHECK(contains(east, eastOf));

        // Just north of the north edge: the neighbor toward the pole for
        // interior rows. For the north row the point is past the pole and
        // its latitude clamps back into the same row (+90 belongs to
        // y = 0), so the cell contains it.
        const LLA northOf{b.latNorthDeg + latIn, centerLon, 0.0};
        if (y == 0) {
          VP_CHECK(cellOf(northOf, level) == c);
          VP_CHECK(contains(c, northOf));
        } else {
          const CellId north = neighbor(c, 0, -1);
          VP_CHECK(cellOf(northOf, level) == north);
          VP_CHECK(!contains(c, northOf));
          VP_CHECK(contains(north, northOf));
        }

        // A point just south of the south edge belongs to the neighbor
        // away from the pole; from the last row the latitude clamps back
        // into the cell itself (-90 belongs to the last row).
        const LLA southOf{b.latSouthDeg - latIn, centerLon, 0.0};
        if (y == yCount - 1) {
          VP_CHECK(cellOf(southOf, level) == c);
          VP_CHECK(contains(c, southOf));
        } else {
          const CellId south = neighbor(c, 0, 1);
          VP_CHECK(cellOf(southOf, level) == south);
          VP_CHECK(!contains(c, southOf));
          VP_CHECK(contains(south, southOf));
        }
      }
    }
  }
}

VP_TEST(dateline_wrap) {
  const int levels[] = {0, 1, 5, 14, 25};
  for (int level : levels) {
    // +180 and -180 are the same meridian and both address to x = 0.
    const CellId east = cellOf(LLA{0.0, 180.0, 0.0}, level);
    const CellId west = cellOf(LLA{0.0, -180.0, 0.0}, level);
    const CellId expected = CellId::make(level, 0, equatorRow(level));
    VP_CHECK(east == expected);
    VP_CHECK(west == expected);

    // The last column's east bound is +180; its east neighbor is column 0
    // with west bound -180 (modular wrap); column 0's west neighbor is the
    // last column.
    const CellId last =
        CellId::make(level, cellXCount(level) - 1, equatorRow(level));
    const CellId first = CellId::make(level, 0, equatorRow(level));
    VP_CHECK(bounds(last).lonEastDeg == 180.0);
    VP_CHECK(neighbor(last, 1, 0) == first);
    VP_CHECK(bounds(neighbor(last, 1, 0)).lonWestDeg == -180.0);
    VP_CHECK(neighbor(first, -1, 0) == last);
  }
}

VP_TEST(cell_poles) {
  // The exact poles belong to the edge rows (+90 -> y = 0, -90 -> last
  // row).
  VP_CHECK(cellOf(LLA{90.0, 0.0, 0.0}, 3) == CellId::make(3, 8, 0));
  VP_CHECK(cellOf(LLA{-90.0, 0.0, 0.0}, 3) == CellId::make(3, 8, 7));

  // Every longitude at the poles lands in the same edge row (longitude is
  // degenerate there, so only the row is pinned).
  const double lons[] = {-180.0, -135.0, -90.0, -45.0, 0.0,
                         45.0,   90.0,   135.0, 180.0};
  for (double lon : lons) {
    const CellId north = cellOf(LLA{90.0, lon, 0.0}, 3);
    const CellId south = cellOf(LLA{-90.0, lon, 0.0}, 3);
    VP_CHECK(north.valid());
    VP_CHECK(north.y() == 0);
    VP_CHECK(south.valid());
    VP_CHECK(south.y() == cellYCount(3) - 1);
  }

  // Across-pole adjacency on the north row: crossing the pole from column
  // x continues at column x + 2^L (longitude shifted by 180) of the same
  // row.
  VP_CHECK(acrossPole(CellId::make(3, 2, 0)) == CellId::make(3, 10, 0));

  // Non-pole-row cells have no across-pole neighbor.
  VP_CHECK(!acrossPole(CellId::make(3, 2, 3)).valid());
}

VP_TEST(halfopen_edges) {
  // Pinned boundary points: each maps to exactly one cell.
  // Lon 0 at L0 is the hemisphere edge; [west, east) puts it in x = 1.
  VP_CHECK(cellOf(LLA{45.0, 0.0, 0.0}, 0) == CellId::make(0, 1, 0));
  // Lat 0 sits on the equator edge; latitude intervals are north-closed
  // (south, north], so it belongs to the northernmost row OF the southern
  // band, whose north edge is the equator: y = 1 at L1, y = 4 at L3.
  VP_CHECK(cellOf(LLA{0.0, -45.0, 0.0}, 1) == CellId::make(1, 1, 1));
  VP_CHECK(cellOf(LLA{0.0, 45.0, 0.0}, 3) == CellId::make(3, 10, 4));
  // Lon -90 is the west edge of column 4 at L3; lat 30 -> row 2.
  VP_CHECK(cellOf(LLA{30.0, -90.0, 0.0}, 3) == CellId::make(3, 4, 2));

  // Generic internal-edge points: each maps to exactly one cell -- the one
  // whose west (longitude) or north (latitude) edge it sits on -- and the
  // adjacent cells on the other side of those edges do not contain it.
  const int levels[] = {2, 5};
  for (int level : levels) {
    const int xCount = cellXCount(level);
    const int yCount = cellYCount(level);
    const double lonSpan = cellLonSpanDeg(level);
    const double latSpan = cellLatSpanDeg(level);
    const int lonEdges[] = {1, xCount / 4, xCount / 2, 3 * xCount / 4,
                            xCount - 1};
    const int latEdges[] = {1, yCount / 4, yCount / 2, 3 * yCount / 4,
                            yCount - 1};
    for (int i : lonEdges) {
      const double lonEdge = -180.0 + i * lonSpan;
      const double lonMid = lonEdge + lonSpan / 2.0;
      for (int j : latEdges) {
        const double latEdge = 90.0 - j * latSpan;
        const double latMid = latEdge - latSpan / 2.0;
        const LLA points[] = {
            {latEdge, lonMid, 0.0},   // on a latitude edge
            {latMid, lonEdge, 0.0},   // on a longitude edge
            {latEdge, lonEdge, 0.0},  // on both
        };
        for (const LLA& p : points) {
          const CellId c = cellOf(p, level);
          VP_CHECK(c.valid());
          VP_CHECK(contains(c, p));
          VP_CHECK(!contains(neighbor(c, -1, 0), p));  // across west edge
          // Across the north edge: a boundary latitude belongs to the row
          // south of it (north-closed), so the row north of the boundary
          // must not contain the point. Lat-edge points sit in row j >= 1,
          // so the north neighbor is always a real cell (no dy clamp).
          VP_CHECK(!contains(neighbor(c, 0, -1), p));
        }
      }
    }
  }
}

VP_TEST(hierarchy_parent_children) {
  // Sampled cells at levels 0..24: exactly 4 children in NW, NE, SW, SE
  // order; each child's parent is the cell itself; the children's bounds
  // tile the parent's bounds exactly.
  for (int level = 0; level < kMaxCellLevel; ++level) {
    const int xSamples[] = {0, cellXCount(level) / 2, cellXCount(level) - 1};
    const int ySamples[] = {0, cellYCount(level) / 2, cellYCount(level) - 1};
    for (int x : xSamples) {
      for (int y : ySamples) {
        const CellId c = CellId::make(level, x, y);
        const std::array<CellId, 4> ch = children(c);
        const std::uint32_t cx = c.x();
        const std::uint32_t cy = c.y();
        for (int k = 0; k < 4; ++k) {
          VP_CHECK(ch.at(k).valid());
          VP_CHECK(ch.at(k).level() == level + 1);
          // North-first rows: NW and NE share the northern child row.
          VP_CHECK(ch.at(k) ==
                   CellId::make(level + 1, 2 * cx + (k % 2), 2 * cy + (k / 2)));
          VP_CHECK(parent(ch.at(k)) == c);
        }
        // 4 distinct children.
        VP_CHECK(ch.at(0) != ch.at(1) && ch.at(2) != ch.at(3));
        VP_CHECK(ch.at(0) != ch.at(2) && ch.at(1) != ch.at(3));
        checkChildrenTileParent(c);
      }
    }
  }

  // The two level-0 hemispheres: their 4 children exactly cover them (the
  // 2x1 root splitting into 4x2), and the roots have no parent.
  for (std::uint32_t x = 0; x <= 1; ++x) {
    const CellId root = CellId::make(0, x, 0);
    VP_CHECK(parent(root) == CellId::invalid());
    checkChildrenTileParent(root);
  }
}

VP_TEST(hierarchy_level_consistency) {
  // The core property (decision 0002): the ancestor at level L' of the
  // cell containing p at level L is the cell containing p at level L'.
  // The grid deliberately includes domain edges and the poles.
  const double lats[] = {-90.0, -60.0, -45.0, -22.5, 0.0,
                         22.5,  45.0,  60.0,  90.0};
  const double lons[] = {-180.0, -135.0, -90.0, -45.0, 0.0,
                         45.0,   90.0,   135.0, 180.0};
  for (double lat : lats) {
    for (double lon : lons) {
      const LLA p{lat, lon, 0.0};
      for (const LevelPair& lv : kLevelPairs) {
        const CellId desc = cellOf(p, lv.fine);
        const CellId anc = ancestorAtLevel(desc, lv.coarse);
        VP_CHECK(anc.valid());
        VP_CHECK(anc.level() == lv.coarse);
        VP_CHECK(anc == cellOf(p, lv.coarse));
        // North-first rows: x and y both shift right by the level
        // difference.
        VP_CHECK(anc.x() == desc.x() >> (lv.fine - lv.coarse));
        VP_CHECK(anc.y() == desc.y() >> (lv.fine - lv.coarse));
      }
    }
  }

  // 10000 fixed-seed random points (deterministic by design, agents.md);
  // no edge exclusion needed: both sides are pure functions of p.
  // NOLINTNEXTLINE(bugprone-random-generator-seed)
  std::mt19937 rng(54321);
  std::uniform_real_distribution<double> lat(-90.0, 90.0);
  std::uniform_real_distribution<double> lon(-180.0, 180.0);
  std::uniform_real_distribution<double> alt(-5000.0, 10000.0);
  for (int i = 0; i < 10000; ++i) {
    const LLA p{lat(rng), lon(rng), alt(rng)};
    for (const LevelPair& lv : kLevelPairs) {
      const CellId desc = cellOf(p, lv.fine);
      const CellId anc = ancestorAtLevel(desc, lv.coarse);
      VP_CHECK(anc == cellOf(p, lv.coarse));
      VP_CHECK(anc.x() == desc.x() >> (lv.fine - lv.coarse));
      VP_CHECK(anc.y() == desc.y() >> (lv.fine - lv.coarse));
    }
  }

  // Contract edges: the same level is idempotent, a higher level returns
  // the cell itself, and a negative level is invalid.
  const CellId c = CellId::make(5, 13, 9);
  VP_CHECK(ancestorAtLevel(c, 5) == c);
  VP_CHECK(ancestorAtLevel(c, 7) == c);
  VP_CHECK(ancestorAtLevel(c, -1) == CellId::invalid());
}

VP_TEST(neighbor_adjacency) {
  const int levels[] = {0, 3, 10, 20};
  for (int level : levels) {
    const int xCount = cellXCount(level);
    const int yCount = cellYCount(level);
    const int xSamples[] = {0, xCount / 2, xCount - 1};
    const int ySamples[] = {0, yCount / 2, yCount - 1};
    for (int x : xSamples) {
      for (int y : ySamples) {
        const CellId c = CellId::make(level, x, y);
        const CellBounds b = bounds(c);

        // East neighbor: the shared edge is bit-exact; across the
        // dateline the wrap lands on column 0 at -180.
        const CellId east = neighbor(c, 1, 0);
        const CellBounds eb = bounds(east);
        if (x == xCount - 1) {
          VP_CHECK(east.x() == 0);
          VP_CHECK(b.lonEastDeg == 180.0);
          VP_CHECK(eb.lonWestDeg == -180.0);
        } else {
          VP_CHECK(eb.lonWestDeg == b.lonEastDeg);
        }
        VP_CHECK(eb.latSouthDeg == b.latSouthDeg);
        VP_CHECK(eb.latNorthDeg == b.latNorthDeg);

        // West neighbor (mirror of the wrap).
        const CellId west = neighbor(c, -1, 0);
        const CellBounds wb = bounds(west);
        if (x == 0) {
          VP_CHECK(west.x() == xCount - 1);
          VP_CHECK(b.lonWestDeg == -180.0);
          VP_CHECK(wb.lonEastDeg == 180.0);
        } else {
          VP_CHECK(wb.lonEastDeg == b.lonWestDeg);
        }

        // South neighbor: shared edge is bit-exact; the last row clamps
        // to itself instead of crossing the pole.
        const CellId south = neighbor(c, 0, 1);
        if (y == yCount - 1) {
          VP_CHECK(south == c);
        } else {
          const CellBounds sb = bounds(south);
          VP_CHECK(sb.latNorthDeg == b.latSouthDeg);
          VP_CHECK(sb.lonWestDeg == b.lonWestDeg);
          VP_CHECK(sb.lonEastDeg == b.lonEastDeg);
        }

        // North neighbor: shared edge is bit-exact; row 0 clamps to
        // itself.
        const CellId north = neighbor(c, 0, -1);
        if (y == 0) {
          VP_CHECK(north == c);
        } else {
          const CellBounds nb = bounds(north);
          VP_CHECK(nb.latSouthDeg == b.latNorthDeg);
          VP_CHECK(nb.lonWestDeg == b.lonWestDeg);
          VP_CHECK(nb.lonEastDeg == b.lonEastDeg);
        }
      }
    }
  }

  // dy clamps at the pole rows no matter how far the request goes.
  for (int level : levels) {
    const CellId northEdge = CellId::make(level, 0, 0);
    const CellId southEdge = CellId::make(level, 0, cellYCount(level) - 1);
    VP_CHECK(neighbor(northEdge, 0, -1000) == northEdge);
    VP_CHECK(neighbor(southEdge, 0, 1000) == southEdge);
  }
}

VP_TEST(extent_meters) {
  // L0 pins (0.1% relative tolerance): a 180-degree meridional arc is
  // ~20004 km tall; the L0 row is centered on the equator, so its width
  // is a 180-degree parallel arc, half the equatorial circumference
  // (~20038 km).
  const CellExtentMeters root = extentMeters(CellId::make(0, 0, 0));
  VP_CHECK_NEAR(root.height, 20004000.0, 20004000.0 * 0.001);
  VP_CHECK_NEAR(root.width, 20037508.0, 20037508.0 * 0.001);

  // Width halves per level. The mid-latitude evaluation makes this only
  // approximate (the parallel radius varies across the cell), so use
  // equator-row cells, where it varies least, and a 1% tolerance.
  const int levels[] = {4, 5, 10, 14};
  for (int level : levels) {
    const CellId parent = CellId::make(level, 0, equatorRow(level));
    const CellId child = children(parent).at(0);  // NW: equator-side half
    const double parentWidth = extentMeters(parent).width;
    const double childWidth = extentMeters(child).width;
    VP_CHECK_NEAR(childWidth, parentWidth / 2.0, parentWidth * 0.01);
    // Meridional height halves too (the arc split at mid-latitude).
    const double parentHeight = extentMeters(parent).height;
    const double childHeight = extentMeters(child).height;
    VP_CHECK_NEAR(childHeight, parentHeight / 2.0, parentHeight * 0.005);
  }

  // Width shrinks ~cos(latitude): a cell whose mid-latitude is ~60 degrees
  // has ~half the width of a same-level equator-row cell (cos 60 = 0.5;
  // 1% tolerance; the nearest row center is within 0.03 degrees of 60 at
  // these levels).
  const int latLevels[] = {10, 14};
  for (int level : latLevels) {
    const int row = static_cast<int>(std::floor(30.0 / cellLatSpanDeg(level)));
    const CellId midLat = CellId::make(level, 0, row);
    const CellId equator = CellId::make(level, 0, equatorRow(level));
    const double w60 = extentMeters(midLat).width;
    const double wEq = extentMeters(equator).width;
    VP_CHECK_NEAR(w60, wEq / 2.0, wEq * 0.01);
  }

  // Height/width relation at mid-latitude: height is the meridional arc,
  // width the parallel arc, so the ratio at the cell's mid-latitude phi is
  // sec(phi) with a small ellipsoidal correction,
  // (1 - e^2) / ((1 - e^2 sin^2 phi) * cos(phi)). Swapping the meridian
  // and prime-vertical radii (or dropping the cos) shifts this by far
  // more than the 0.1% tolerance.
  const CellId c45 = CellId::make(5, 0, 8);  // mid-latitude 42.1875 deg
  const CellBounds b45 = bounds(c45);
  const CellExtentMeters ext45 = extentMeters(c45);
  const double phi45 = 0.5 * (b45.latSouthDeg + b45.latNorthDeg) * kDegToRad;
  const double sinPhi45 = std::sin(phi45);
  const double w45 = 1.0 - Ellipsoid::WGS84.e2 * sinPhi45 * sinPhi45;
  const double ratio45 = (1.0 - Ellipsoid::WGS84.e2) / (w45 * std::cos(phi45));
  VP_CHECK_NEAR(ext45.height / ext45.width, ratio45, ratio45 * 0.001);
}

VP_TEST(frame_independence) {
  // CellId depends only on the canonical ECEF position (decision 0001):
  // round-tripping a point through a LocalFrame must not change its cell.
  // Frames at a near-pole/dateline origin and a mid-latitude origin.
  const LocalFrame frames[] = {
      LocalFrame(LLA{89.9, 179.9, 0.0}),
      LocalFrame(LLA{0.0, 180.0, 0.0}),
      LocalFrame(LLA{45.0, 10.0, 100.0}),
  };
  const int levels[] = {5, 14};

  // Fixed-seed random points (deterministic by design, agents.md).
  // NOLINTNEXTLINE(bugprone-random-generator-seed)
  std::mt19937 rng(424242);
  std::uniform_real_distribution<double> lat(-90.0, 90.0);
  std::uniform_real_distribution<double> lon(-180.0, 180.0);
  std::uniform_real_distribution<double> alt(-5000.0, 10000.0);
  int checked = 0;
  for (int i = 0; i < 10000; ++i) {
    const LLA p{lat(rng), lon(rng), alt(rng)};
    if (std::fabs(p.latDeg) > kFramePoleCutDeg) {
      continue;  // polar longitude is ill-conditioned (see poles)
    }
    const ECEF e = llaToEcef(p);
    for (const LocalFrame& f : frames) {
      const ECEF q = f.toWorld(f.toLocal(e));
      for (int level : levels) {
        // Excluded per decision 0002: points within round-trip tolerance
        // of a cell edge may land on either side.
        if (nearCellEdge(p, level, kFrameEdgeTolDeg)) {
          continue;
        }
        VP_CHECK(cellOf(q, level) == cellOf(e, level));
        ++checked;
      }
    }
  }
  VP_CHECK(checked > 0);  // the exclusions must not swallow the whole set
}
