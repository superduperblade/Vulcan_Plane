// World cell addressing (implementation step 3).
//
// Global geodetic quadtree over lat/lon (decision 0002,
// docs/decisions/0002-world-cell-addressing.md):
//   - Level L has 2^(L+1) longitude cells x 2^L latitude cells. Level 0 is
//     two hemisphere cells (180 x 180 degrees), matching the OGC
//     WorldCRS84Quad / TMS global-geodetic / Cesium GeographicTilingScheme
//     layouts so engine cells map 1:1 onto standard geodetic tile schemes.
//   - Addressing is a pure function of the canonical position (decision
//     0001): cellOf(ECEF) goes through ecefToLla. Cells never replace ECEF
//     as storage/identity; they are a derived integer index for streaming,
//     LOD, and spatial partitioning. Altitude is content, not address.
//   - Conventions: longitude in [-180, 180) with +180 wrapping to -180
//     (same meridian, x = 0); latitude rows north-first (y = 0 touches the
//     north pole); both axes index by flooring a fraction measured from
//     the north/west corner, so longitude intervals are [west, east) while
//     latitude intervals are (south, north] — a latitude exactly on a row
//     boundary belongs to the row south of it; the exact poles are clamped
//     into the edge rows (+90 -> y = 0, -90 -> last row); neighbor() wraps
//     dx across the dateline and clamps dy at the poles.
//
// All math is double precision and pure CPU (glm only, no Vulkan).
// No streaming/caching/LOD policy here — addressing and geometry only.

#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "geospatial/geo.h"

namespace vp::geo {

// Bounded by the packed representation: 6 bits level + 26 bits x + 25 bits
// y = 57 of 64 bits (7 reserved, zero). Level 25 cells are ~0.6 m tall; the
// packing allows raising this to 28 (~7 cm) without a format change.
inline constexpr int kMaxCellLevel = 25;

// Packed cell identifier. Note: the zero value is a VALID cell (level-0
// west hemisphere); use invalid() for "no cell".
struct CellId {
  std::uint64_t bits = 0;

  // Packs (level, x, y). Out-of-range inputs yield invalid(); they are not
  // masked (masking would silently wrap an out-of-range index into a
  // different, valid-looking cell).
  static CellId make(int level, std::uint32_t x, std::uint32_t y);
  static constexpr CellId invalid() { return CellId{~std::uint64_t{0}}; }

  [[nodiscard]] int level() const;  // [0, kMaxCellLevel]
  [[nodiscard]] std::uint32_t x()
      const;  // longitude index [0, 2^(L+1)); x = 0 at lon -180
  [[nodiscard]] std::uint32_t y()
      const;  // latitude index [0, 2^L); y = 0 is the north row

  // Fields in range for their level and reserved bits zero.
  [[nodiscard]] bool valid() const;

  // "L/x/y" for logs, caches, debug UI.
  [[nodiscard]] std::string str() const;

  friend bool operator==(const CellId& a, const CellId& b) {
    return a.bits == b.bits;
  }
  friend bool operator!=(const CellId& a, const CellId& b) {
    return a.bits != b.bits;
  }
  friend bool operator<(const CellId& a, const CellId& b) {
    return a.bits < b.bits;  // map/set keys
  }
};

// Cell geometry in the geodetic domain (degrees). Cells never straddle the
// dateline (x = 0 starts at -180), so bounds are always a plain interval.
struct CellBounds {
  double lonWestDeg;
  double lonEastDeg;
  double latSouthDeg;
  double latNorthDeg;
};

// Physical extent of a cell (meters) on the WGS-84 ellipsoid, evaluated at
// the cell's mid-latitude. Streaming/LOD sizing must use this instead of
// the degree spans: east-west extent shrinks ~cos(latitude).
struct CellExtentMeters {
  double width;   // east-west at mid-latitude
  double height;  // meridional (north-south)
};

// Level layout. 360 and 180 are exact binary fractions, so the spans are
// powers-of-two divisions with no double rounding.
constexpr int cellXCount(int level) { return 1 << (level + 1); }  // 2^(L+1)
constexpr int cellYCount(int level) { return 1 << level; }        // 2^L
constexpr double cellLonSpanDeg(int level) { return 360.0 / cellXCount(level); }
constexpr double cellLatSpanDeg(int level) { return 180.0 / cellYCount(level); }

// Addressing (pure functions of position). A level outside
// [0, kMaxCellLevel] yields invalid().
CellId cellOf(const LLA& p, int level);   // lon wrapped, lat clamped to poles
CellId cellOf(const ECEF& p, int level);  // ecefToLla, then cellOf(LLA)

CellBounds bounds(const CellId& c);
CellExtentMeters extentMeters(const CellId& c);

// Half-open containment with the pole-row conventions (the poles belong to
// the edge rows). Equivalent to cellOf(p, c.level()) == c.
bool contains(const CellId& c, const LLA& p);
bool contains(const CellId& c, const ECEF& p);

// Hierarchy. Every cell has exactly 4 children (the invariant holds at
// every level, including the 2x1 root splitting into 4x2).
CellId parent(const CellId& c);                   // invalid() at level 0
std::array<CellId, 4> children(const CellId& c);  // order: NW, NE, SW, SE
// The ancestor of c at the given level. level > c.level() returns c itself
// (idempotent); level < 0 returns invalid().
CellId ancestorAtLevel(const CellId& c, int level);

// Same-level neighbor. dx: +1 east / -1 west, wrapping across the dateline
// (x is modular). dy: +1 south / -1 north, clamped at the pole rows.
CellId neighbor(const CellId& c, int dx, int dy);

// The cell across a pole from a pole-row cell: crossing a pole from column
// x continues at longitude +180, i.e. column x + 2^L (modular) of the same
// row. Returns invalid() for non-pole-row cells.
CellId acrossPole(const CellId& c);

}  // namespace vp::geo
