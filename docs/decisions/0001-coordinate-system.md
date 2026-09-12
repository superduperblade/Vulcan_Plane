# Decision: Coordinate System — ECEF double storage, origin-relative float32 rendering

## Date

2026-09-12

## Context

The engine must represent real Earth at planetary scale. Three facts constrain
the coordinate model:

1. **float32 cannot represent the planet.** A 24-bit mantissa gives a ULP of
   0.5 m at Earth's radius (6.378e6 m): global float32 coordinates quantize
   to ~0.25–0.5 m, which breaks terrain alignment, streaming, and camera
   stability.
2. **Geographic data is geodetic.** Real-world sources (terrain, biomes,
   spawn points) are lat/lon/altitude in WGS-84; the engine must convert
   deliberately, at boundaries, not ad hoc.
3. **The GPU renders in float32.** Consumer hardware has no portable 64-bit
   ALUs, so rendered geometry must live in a local float32 frame.

Every downstream system (terrain, streaming, LOD, camera) needs this decided
before it is built.

## Decision

1. **Canonical storage/identity: WGS-84 ECEF, double precision**
   (`vp::geo::ECEF` = `glm::dvec3`). All world state (positions, tile
   origins, camera) is stored and identified in ECEF double.
2. **Human-facing / geographic input: geodetic LLA** (`vp::geo::LLA`):
   latitude/longitude in degrees, altitude in meters **above the WGS-84
   ellipsoid** (not mean sea level; a geoid model is a later concern).
3. **Rendering: float32 in a local ENU frame** (`vp::geo::LocalFrame`)
   relative to a moving origin. The double→float32 conversion happens ONLY
   at the render boundary.
4. **All geographic math is double precision.** No float32 geographic
   computation anywhere in the engine.
5. **Re-anchoring (moving origin):** when the camera is more than
   `R = 100 km` from the current frame origin, create a new `LocalFrame` at
   the camera's ECEF position and convert active data with
   `vp::geo::convertLocal` (double precision). The precision guarantees this
   provides are the contract in `docs/PRECISION.md`.
6. **Datum: WGS-84** (a = 6378137 m, f = 1/298.257223563). Derived constants
   (b, e², e′²) are computed from these two in code, not transcribed.

## Alternatives

- **Global float32 (ECEF or a flat local origin at (0,0,0)):** ~0.25–0.5 m
  quantization at planetary radii; visible jitter and z-fighting. Rejected.
- **Fixed regional origins (one frame per continent/region):** adds a
  frame-selection layer and hard seams at region borders; re-anchoring is
  strictly cheaper (one frame, moved as needed) with no seams. Rejected.
- **Double-precision GPU rendering:** not portable to consumer hardware.
  Rejected.
- **Integer tile addressing (e.g. integer 1 km grid over ECEF):** still
  needed for streaming/LOD, but it is orthogonal to the float/double
  decision and belongs to the world-coordinate model (step 3). Deferred.

## Consequences

- World state is double → ~2× the memory of float32 per position. Accepted:
  CPU-side state is cheap relative to what it enables; GPU buffers stay
  float32.
- Re-anchoring is a real event that terrain, streaming, LOD, and camera must
  handle (convert/re-upload on re-anchor). This is specified in
  `docs/PRECISION.md`, which every downstream system codes against.
- LLA altitude is ellipsoidal. Mean-sea-level data (e.g. most elevation
  datasets) needs a geoid model (EGM2008) at the data-ingestion boundary —
  a later step, isolated to that boundary.
- The Earth's center and far-interior points have a non-unique geodetic
  representation; the engine's domain (h > ~-20 km) is well inside the
  unique region, and `ecefToLla` documents the fold boundary.

## Performance Considerations

- `ecefToLla`: ~6 iterations of trig, ~100–200 ns. Used at spawn and
  frame-setup, never per-vertex — not a hot path.
- `LocalFrame` transforms: one 3×3 double matrix multiply — trivial.
- GPU work stays in float32: full hardware precision for local coordinates,
  no 64-bit ALU dependency.
- Re-anchoring cost is proportional to active (rendered) data, not world
  size — bounded by view distance.

## Quality Considerations

- Round-trip LLA→ECEF→LLA is < 1 mm anywhere in the engine's domain
  (verified by `test/geo_test.cpp`: grid + 10,000 random points).
- float32 error is bounded (2^-24 · d) and characterized with measured
  numbers in `docs/PRECISION.md`.
- Fully deterministic: no randomness, no thread dependence, no platform
  dependence beyond IEEE-754 double.
