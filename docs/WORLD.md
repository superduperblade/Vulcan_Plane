# World

World representation, geographic coordinate model, terrain model, biome
model, tile hierarchy, streaming, LOD, geographic data sources, and
generation pipeline.

## Coordinate Model (step 2 — implemented)

The canonical world representation is decided in
[`decisions/0001-coordinate-system.md`](decisions/0001-coordinate-system.md)
and implemented in `src/geospatial/` (library `vp_geo`, pure CPU, glm only):

| Role | Representation | Type |
| --- | --- | --- |
| Storage / identity | WGS-84 ECEF, **double** precision | `vp::geo::ECEF` (`glm::dvec3`) |
| Human-facing / geographic data | Geodetic LLA: degrees, meters, **ellipsoidal** altitude | `vp::geo::LLA` |
| Rendering | float32 ENU (East-North-Up) local coordinates relative to a moving origin | `vp::geo::LocalFrame` |

API (see `src/geospatial/geo.h` for details):

- `llaToEcef` / `ecefToLla` — WGS-84 conversions; round-trip < 1 mm in the
  engine's domain (tested).
- `LocalFrame` — ENU frame anchored at an ECEF origin; `toLocal`/`toWorld`
  in double; orthonormal `basis()` (columns E, N, U). Deterministic at the
  poles.
- `convertLocal(from, to, local)` — frame-to-frame conversion; the
  re-anchoring primitive.
- `distanceEcef` — double-precision distance.

### Re-anchoring (moving origin)

The render frame origin follows the camera:

1. All world state is ECEF double (frame-independent).
2. The GPU sees only float32 local coordinates in the current `LocalFrame`.
3. When `distance(camera, frame origin) > 100 km`, create a new
   `LocalFrame` at the camera position and convert active data with
   `convertLocal` (double; error < 1 µm, tested).
4. Within 100 km of the origin, float32 error is ≤ 6 mm (theoretical bound)
   / ~3 mm (measured) — see the precision contract below.

**Precision contract:** [`docs/PRECISION.md`](PRECISION.md) is the contract
every downstream system (terrain, streaming, LOD, camera) codes against.

## Cell Addressing (step 3 — implemented)

The world is partitioned by the **global geodetic quadtree** decided in
[`decisions/0002-world-cell-addressing.md`](decisions/0002-world-cell-addressing.md)
and implemented in `src/geospatial/cells.{h,cpp}` (`vp::geo`, same `vp_geo`
library): level L has 2^(L+1) longitude cells × 2^L latitude cells, level 0
being two hemisphere cells — the exact layout of the OGC WorldCRS84Quad /
TMS global-geodetic / Cesium GeographicTilingScheme families, so standard
geodata tiles map 1:1 onto engine cells.

- `CellId` — packed uint64 (level:6 | x:26 | y:25), `kMaxCellLevel = 25`
  (~0.6 m cells). A derived index for streaming/LOD/spatial partitioning —
  never a second coordinate system; altitude is content, not address.
- `cellOf(LLA|ECEF, level)` — pure function of the canonical position
  (ECEF goes through `ecefToLla`, ~100–200 ns: streaming-decision rates,
  never per-vertex).
- `bounds`, `extentMeters` (width at mid-latitude, height as the exact
  meridional arc — sizing must use meters, cells are not equal-area),
  `contains`, `parent`/`children`/`ancestorAtLevel`, `neighbor` (dateline
  wrap, pole clamp), `acrossPole`.
- Conventions (pinned, tested): y = 0 is the north pole row; longitude
  intervals [west, east), latitude intervals (south, north] (shared floor
  rule from the north/west corner); +180 ≡ −180 → x = 0.
- Ingest mapping: geodetic rasters (SRTM 1° tiles) resample directly;
  Web-Mercator tile sets enter through an inverse-Mercator converter at
  the future ingestion boundary (step 9).

## Terrain Model

Not yet designed. Will consume cells for spatial partitioning (next steps:
basic world representation, then terrain).

## Biome Model

Not yet designed.

## Tile Hierarchy

The cell hierarchy IS the tile hierarchy: 4 children per cell at every
level (NW/NE/SW/SE), `ancestorAtLevel(cellOf(p, L), L') == cellOf(p, L')`
guarantees level-independent addressing decisions (the core property,
tested). Above the two level-0 hemispheres a single conceptual root can be
layered later without changing addressing (decision 0002).

## Streaming, LOD

Not yet designed. Both must respect the precision contract
(`docs/PRECISION.md`): distances and level decisions in double, rendered
geometry in the local float32 frame.

## Geographic Data Sources

Not yet chosen. Elevation/biome data will enter at a deliberate ingestion
boundary (source format → validated → converted → tiled), per `agents.md`.
Note: most real-world elevation data is mean-sea-level; conversion to
ellipsoidal height needs a geoid model (EGM2008) at that boundary.

## Generation Pipeline

Not yet designed.
