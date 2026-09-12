// World cell addressing (implementation step 3).
// See cells.h for the addressing scheme and
// docs/decisions/0002-world-cell-addressing.md for the binding decision
// (global geodetic quadtree over lat/lon; CellId is a derived integer index
// for streaming, LOD, and spatial partitioning — never a second coordinate
// system).

#include "geospatial/cells.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace vp::geo {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;

// Wrap a longitude (degrees) to the addressing domain [-180, 180): the
// dateline meridian must belong to x = 0, so +180 wraps to -180. Same fmod
// approach as geo.cpp's (file-local) normalizeLonDeg; the half-open upper
// end is contractual here because it decides column ownership.
double wrapLonDeg(double lon) {
  double l = std::fmod(lon + 180.0, 360.0);
  if (l < 0.0) {
    l += 360.0;
  }
  return l - 180.0;
}

// Geodetic meridian arc distance from the equator to latitude phi (radians):
// the standard closed-form series (the UTM footpoint series), exact to far
// below a millimeter on WGS-84. The coefficients are written as expressions
// of e2 (not precomputed constants) so they stay consistent if the
// ellipsoid ever changes.
double meridianArc(double phi, const Ellipsoid& e) {
  const double e2 = e.e2;
  const double e4 = e2 * e2;
  const double e6 = e4 * e2;
  return e.a * ((1.0 - e2 / 4.0 - 3.0 * e4 / 64.0 - 5.0 * e6 / 256.0) * phi -
                (3.0 * e2 / 8.0 + 3.0 * e4 / 32.0 + 45.0 * e6 / 1024.0) *
                    std::sin(2.0 * phi) +
                (15.0 * e4 / 256.0 + 45.0 * e6 / 1024.0) * std::sin(4.0 * phi) -
                (35.0 * e6 / 3072.0) * std::sin(6.0 * phi));
}

}  // namespace

CellId CellId::make(int level, std::uint32_t x, std::uint32_t y) {
  // Never mask: masking would silently fold an out-of-range index into a
  // different, valid-looking cell. The level bound is the accessor contract
  // ([0, kMaxCellLevel]), not the packing limit: the level 26+ packing
  // headroom exists only for a future bump of kMaxCellLevel itself
  // (decision 0002), never for raw construction. valid() still re-checks
  // everything, guarding cells built by assigning bits directly.
  if (level < 0 || level > kMaxCellLevel || x > 0x3FFFFFFU || y > 0x1FFFFFFU) {
    return invalid();
  }
  return CellId{(static_cast<std::uint64_t>(level) << 51) |
                (static_cast<std::uint64_t>(x) << 25) |
                static_cast<std::uint64_t>(y)};
}

int CellId::level() const { return static_cast<int>(bits >> 51); }

std::uint32_t CellId::x() const {
  return static_cast<std::uint32_t>((bits >> 25) & 0x3FFFFFF);
}

std::uint32_t CellId::y() const {
  return static_cast<std::uint32_t>(bits & 0x1FFFFFF);
}

bool CellId::valid() const {
  // Reserved bits (57..63) must be zero: anything set there is not a packed
  // cell (invalid() is all-ones and fails this test first).
  if ((bits >> 57) != 0) {
    return false;
  }
  const int l = level();
  if (l < 0 || l > kMaxCellLevel) {
    return false;
  }
  return x() < static_cast<std::uint32_t>(cellXCount(l)) &&
         y() < static_cast<std::uint32_t>(cellYCount(l));
}

std::string CellId::str() const {
  return std::to_string(level()) + "/" + std::to_string(x()) + "/" +
         std::to_string(y());
}

CellId cellOf(const LLA& p, int level) {
  if (level < 0 || level > kMaxCellLevel) {
    return CellId::invalid();
  }
  // Poles belong to the edge rows (clamp), the dateline meridian to x = 0
  // (wrap). Cell spans are exact binary fractions (cells.h), so the index
  // math is exact apart from the u/v divisions themselves.
  const double lat = std::clamp(p.latDeg, -90.0, 90.0);
  const double lon = wrapLonDeg(p.lonDeg);
  const double u = (lon + 180.0) / 360.0;  // [0, 1): +180 wraps to -180
  const double v = (90.0 - lat) / 180.0;   // [0, 1]; v = 1 is the south pole
  const auto countX = static_cast<double>(cellXCount(level));
  const auto countY = static_cast<double>(cellYCount(level));
  // The clamps pin v = 1 (south pole) into the last row and absorb any
  // rounding that could reach exactly 1.0 in u; without them the floor
  // could land one past the last index.
  const auto xScaled = static_cast<std::uint64_t>(u * countX);  // truncates
  const auto yScaled = static_cast<std::uint64_t>(v * countY);
  const auto x = static_cast<std::uint32_t>(
      std::min(xScaled, static_cast<std::uint64_t>(cellXCount(level) - 1)));
  const auto y = static_cast<std::uint32_t>(
      std::min(yScaled, static_cast<std::uint64_t>(cellYCount(level) - 1)));
  return CellId::make(level, x, y);
}

CellId cellOf(const ECEF& p, int level) {
  // Addressing is a pure function of the canonical position (decision
  // 0002): ECEF is storage/identity, so go through ecefToLla. Altitude is
  // content and never part of the address.
  return cellOf(ecefToLla(p), level);
}

CellBounds bounds(const CellId& c) {
  if (!c.valid()) {
    return {};
  }
  // Cells never straddle the dateline (x = 0 starts at -180), so a plain
  // west/east interval needs no wrap handling.
  const double lonSpan = cellLonSpanDeg(c.level());
  const double latSpan = cellLatSpanDeg(c.level());
  const double west = -180.0 + static_cast<double>(c.x()) * lonSpan;
  const double north = 90.0 - static_cast<double>(c.y()) * latSpan;
  return {west, west + lonSpan, north - latSpan, north};
}

CellExtentMeters extentMeters(const CellId& c) {
  if (!c.valid()) {
    return {};
  }
  const Ellipsoid& e = Ellipsoid::WGS84;
  const CellBounds b = bounds(c);
  // Width: the parallel (east-west) arc at the cell's mid-latitude — the
  // documented representative for an extent that genuinely varies
  // ~cos(latitude) across the cell. Height: the exact geodetic meridian arc
  // between the cell's south and north latitudes (no mid-latitude radius
  // approximation), so streaming/LOD sizing sees the true north-south size
  // at every level, including the level-0 hemispheres.
  const double phi = 0.5 * (b.latSouthDeg + b.latNorthDeg) * kDegToRad;
  const double sinPhi = std::sin(phi);
  const double cosPhi = std::cos(phi);
  const double w = 1.0 - e.e2 * sinPhi * sinPhi;
  // N: prime-vertical radius of curvature (east-west).
  const double N = e.a / std::sqrt(w);
  return {cellLonSpanDeg(c.level()) * kDegToRad * N * cosPhi,
          std::fabs(meridianArc(b.latNorthDeg * kDegToRad, e) -
                    meridianArc(b.latSouthDeg * kDegToRad, e))};
}

bool contains(const CellId& c, const LLA& p) {
  // Addressing equality is the single source of truth: the half-open and
  // pole conventions then cannot diverge between the two entry points.
  return c.valid() && cellOf(p, c.level()) == c;
}

bool contains(const CellId& c, const ECEF& p) {
  return c.valid() && cellOf(p, c.level()) == c;
}

CellId parent(const CellId& c) {
  // Level 0 is the two hemispheres; the scheme defines nothing above them
  // (a conceptual single root may be layered on later, decision 0002).
  if (c.level() == 0) {
    return CellId::invalid();
  }
  return CellId::make(c.level() - 1, c.x() / 2U, c.y() / 2U);
}

std::array<CellId, 4> children(const CellId& c) {
  // y = 0 is the north row, so the first pair of children is the northern
  // half: NW, NE, SW, SE.
  if (!c.valid() || c.level() == kMaxCellLevel) {
    return {CellId::invalid(), CellId::invalid(), CellId::invalid(),
            CellId::invalid()};
  }
  const int next = c.level() + 1;
  const std::uint32_t x0 = c.x() * 2U;
  const std::uint32_t y0 = c.y() * 2U;
  return {CellId::make(next, x0, y0),             // NW
          CellId::make(next, x0 + 1U, y0),        // NE
          CellId::make(next, x0, y0 + 1U),        // SW
          CellId::make(next, x0 + 1U, y0 + 1U)};  // SE
}

CellId ancestorAtLevel(const CellId& c, int level) {
  if (level < 0) {
    return CellId::invalid();
  }
  // Idempotent at or above the cell's own level; below it, one parent step
  // (halving x and y) per level, so the result equals addressing the same
  // point directly at `level` — the level-consistency property that lets
  // streaming decisions be level-independent.
  CellId cur = c;
  while (cur.valid() && cur.level() > level) {
    cur = parent(cur);
  }
  return cur;
}

CellId neighbor(const CellId& c, int dx, int dy) {
  if (!c.valid()) {
    return CellId::invalid();
  }
  const int countX = cellXCount(c.level());
  const int countY = cellYCount(c.level());
  // Reduce dx before adding: any int (however large) then cannot overflow
  // the sum, and the dateline wraps in both directions (x is modular).
  const int dxw = ((dx % countX) + countX) % countX;
  const int nx = (static_cast<int>(c.x()) + dxw) % countX;
  // Latitude has no wrap: dy clamps at the pole rows (pre-clamped for the
  // same overflow reason; |dy| beyond the row count clamps identically).
  const int dyC = std::clamp(dy, -countY, countY);
  const int ny = std::clamp(static_cast<int>(c.y()) + dyC, 0, countY - 1);
  return CellId::make(c.level(), static_cast<std::uint32_t>(nx),
                      static_cast<std::uint32_t>(ny));
}

CellId acrossPole(const CellId& c) {
  if (!c.valid()) {
    return CellId::invalid();
  }
  const int countX = cellXCount(c.level());
  const int countY = cellYCount(c.level());
  if (c.y() != 0U && c.y() != static_cast<std::uint32_t>(countY - 1)) {
    return CellId::invalid();
  }
  // Crossing a pole continues at longitude +180, i.e. column x + 2^L of the
  // same row — and cellYCount(L) == 2^L is exactly that column offset.
  return CellId::make(
      c.level(),
      static_cast<std::uint32_t>((static_cast<int>(c.x()) + countY) % countX),
      c.y());
}

}  // namespace vp::geo
