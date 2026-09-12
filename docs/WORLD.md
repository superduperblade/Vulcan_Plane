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

## Terrain Model

Not yet designed. Step 3 (world-coordinate model: tile/cell addressing and
spatial partitioning on top of the coordinate foundation) is next.

## Biome Model

Not yet designed.

## Tile Hierarchy

Not yet designed. Tile addressing will be frame-independent (see decision
0001: integer tile addressing is deferred to step 3).

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
