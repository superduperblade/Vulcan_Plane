# Decision: World Cell Addressing — Global Geodetic Quadtree

## Date

2026-09-12

## Context

Step 3 needs frame-independent tile/cell addressing and spatial partitioning
on top of the coordinate foundation. Decision 0001 deferred integer tile
addressing to this step. Constraints:

1. **Ingest compatibility is decisive.** Real-world elevation/geodata enters
   at a deliberate ingestion boundary (agents.md pipeline). The dominant
   sources are geodetic rasters (SRTM: 1°×1° tiles, geographic WGS-84,
   EGM96 vertical) and Web-Mercator XYZ tile sets (Mapbox Terrain-RGB,
   Mapzen Terrarium, OSM-style servers) — the latter themselves derived from
   geodetic DEMs.
2. **Poles, dateline, and domain edges must be correct.** The coordinate
   tests (test/geo_test.cpp) already stress these; Web-Mercator addressing
   cannot address anything poleward of ±85.0511° (arctan(sinh π)).
3. **Addressing must be deterministic and derived from the canonical
   position.** ECEF double is storage/identity (decision 0001); `ecefToLla`
   costs ~100–200 ns — acceptable at streaming-decision rates, never
   per-vertex.
4. **Hierarchical LOD and neighbor lookup**, including wrap-around at the
   dateline, are required by streaming/LOD (next steps).
5. **Essential complexity only** (PROTOCOL.md 2.5): no streaming, caching,
   or LOD policy in the addressing layer.

## Decision

1. **Scheme: global geodetic quadtree.** Level L has 2^(L+1) longitude
   cells × 2^L latitude cells; level 0 is two hemisphere cells of
   180°×180°. This is exactly the layout of the OGC WorldCRS84Quad tile
   matrix set (GoogleCRS84Quad well-known scale set; 2×1 root, 0.703125°
   cell size), the OSGeo TMS "global-geodetic" profile, and Cesium's
   GeographicTilingScheme — engine cells map 1:1 onto standard geodetic
   tile schemes at every level.
2. **Conventions (pinned, tested):**
   - Cell (L, x, y): longitude [-180 + x·360/2^(L+1), … +span),
     latitude (south, north], with y = 0 the row touching the north pole
     (north-first, matching the OGC tile-matrix topLeftCorner [-180, 90]
     and XYZ slippy conventions; TMS 1.0's own global-geodetic example
     counts from the south — a one-flip difference for any TMS consumer).
   - Both axes index by flooring a fraction measured from the north/west
     corner: longitude intervals are [west, east) while latitude intervals
     are (south, north] — a latitude exactly on a row boundary belongs to
     the row south of it. The exact poles are clamped into the edge rows:
     lat +90 → y = 0, lat -90 → y = 2^L − 1.
   - Longitude +180 ≡ −180 (same meridian): both map to x = 0.
   - `neighbor` wraps dx across the dateline (x is modular) and clamps dy
     at the pole rows. Across-pole adjacency is explicit: crossing a pole
     from column x lands in column x + 2^L of the same polar row
     (longitude shifted by 180°).
3. **Packed identifier: `vp::geo::CellId`, uint64.** level:6 | x:26 | y:25
   bits (57 of 64 used; 7 reserved, must be zero). `kMaxCellLevel = 25`
   (~0.6 m cells — far below any streaming need; the packing allows raising
   this to level 28, ~7 cm, without a format change).
4. **Addressing is a pure function of the canonical position:**
   `cellOf(ECEF)` = `ecefToLla` → integer index; `cellOf(LLA)` is a
   wrap/multiply/floor. CellId is a derived index for streaming, LOD, and
   spatial partitioning — never a second coordinate system. It does not
   encode altitude: cells partition the surface; altitude is content.
5. **Ingest rules:** geodetic rasters (SRTM) resample directly into cells.
   Web-Mercator tile sets enter through an inverse-Mercator converter at
   the ingestion boundary (zoom↔level offset, y-direction, and projection
   are per-source converter details; isolated in step 9, not in cells).

## Alternatives

- **Web-Mercator quadtree (XYZ slippy, OGC WebMercatorQuad):** direct
  ingest of OSM/terrain-RGB/Terrarium tiles, but nothing poleward of
  ±85.0511° is addressable at all; geodetic sources need forward
  projection; tile latitude spans are non-uniform. Rejected as canonical
  (kept as an ingest format).
- **Cube-sphere quadtree:** more uniform angular cell sizes and no polar
  degeneracy, but zero geodata compatibility (every raster requires
  spherical remeshing), custom projection math, and 12-edge/8-corner
  face-adjacency bookkeeping. Useful complexity that no current
  requirement justifies. Rejected.
- **HEALPix:** equal-area, iso-latitude, 12 base pixels subdividing by 4 —
  designed for full-sky statistical/harmonic analysis (CMB); diamond cells
  do not align with lat/lon rasters and no geodata uses it. Solves a
  problem the engine does not have. Rejected.
- **Icosahedral DGGS (ISEA family):** equal-area with complex hexagon/
  triangle hierarchies and neighbor math; worst ingest compatibility.
  Rejected.
- **Flat ECEF integer grid:** fastest cellOf (shift+mask) but no
  hierarchy — LOD/parent-child would need a second scheme bolted on
  (accidental complexity); volume cells do not match a surface+altitude
  world. Rejected. (Fixed-size local grids *inside* a cell remain a
  content-level choice, not addressing.)

## Consequences

- Engine cells correspond exactly to OGC geodetic tile matrices at every
  level; converters exist only for Mercator sources, isolated at the
  ingestion boundary.
- Cells are **not equal-area**: physical width shrinks ∝ cos(latitude) and
  a full polar row meets at a point. LOD/streaming sizing must use the
  provided ellipsoidal extent helper instead of raw degree spans. Accepted:
  the same tradeoff as the OGC geodetic schemes and Cesium's globe; polar
  area is spatially tiny.
- The root is 2×1 (two hemispheres), not one cell; the 4-children-per-cell
  quadtree invariant holds at every level. A single conceptual root can be
  layered above later without changing addressing.
- Terrain rendering near the poles must handle degenerate patches — a
  terrain-design concern (later step); addressing itself is total and
  deterministic at the poles.
- Practical streaming levels sit around L8–L16 (≈78 km … ≈300 m cell
  height; L10 ≈ 19 km, L14 ≈ 1.2 km); kMaxCellLevel = 25 leaves ample
  headroom.

## Performance Considerations

- `cellOf(ECEF)`: ecefToLla (~100–200 ns) + O(1) integer work — valid at
  streaming-decision rates; never per-vertex (vertex data lives in
  tile-local coordinates per docs/PRECISION.md).
- `cellOf(LLA)`: a wrap, multiply, floor, clamp — tens of ns.
- parent/children/neighbor/bounds/packing: pure integer and double ops, no
  allocation, no trig.
- Numerics: cell spans are 360/2^(L+1) and 180/2^L degrees — exact binary
  fractions, so index math is exact in double; the only rounding present is
  ecefToLla's ~1e-13° (points within that of a cell edge may land on either
  side; the same ECEF point always maps to the same cell).

## Quality Considerations

- Tested invariants (test/cells_test.cpp): packing round-trips and field
  validation; pinned root/edge/pole addresses; ECEF↔LLA addressing
  equality; half-open edge uniqueness (each point maps to exactly one
  cell); dateline wrap (+180 ≡ −180, modular east/west neighbors);
  pole-row conventions and across-pole adjacency; parent(children(c)) == c;
  the core level-consistency property
  ancestorAtLevel(cellOf(p, L), L') == cellOf(p, L') for L' ≤ L over grids
  and seeded random points; neighbor geometric adjacency; ellipsoidal
  extent sanity (halving per level, cos-latitude width shrink).
- Deterministic: pure functions of (position, level); no state, no thread
  or platform dependence beyond IEEE-754 double — same contract as
  decision 0001. (Tests use fixed seeds for the same reason.)
- Frame independence: CellId depends only on the canonical ECEF position,
  never on LocalFrame or re-anchoring state (verified by round-tripping
  points through a LocalFrame before addressing, excluding points within
  round-trip tolerance of cell edges).
