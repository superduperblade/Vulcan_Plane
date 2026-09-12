# Precision Contract

The contract every downstream system (terrain, streaming, LOD, camera) codes
against. Decided in [`decisions/0001-coordinate-system.md`](decisions/0001-coordinate-system.md);
implemented in `src/geospatial/`; enforced by `test/geo_test.cpp`.

## 1. The float32 problem

- float32 has a 24-bit mantissa: per-component rounding error is
  ≤ 2^-24 · |value| ≈ 5.96e-8 · |value| (round to nearest).
- For a local coordinate vector of magnitude d, the orthonormal frame
  transform preserves norm, so the position error is
  **|err| ≤ 2^-24 · d** (plus ~1e-8 m of double-rounding in the transform).
- Global float32 ECEF is unusable: at Earth's radius (6.378e6 m) the ULP is
  0.5 m → ~0.25 m quantization.

## 2. Measured float32 local-space error

Measured by `./build/vp_geo_tests --report` (worst case over 6 directions,
frame at LLA 47.3769/8.5417/540; 2026-09-12):

| Distance from origin | Measured worst error | Theoretical bound (2^-24·d) |
| --- | --- | --- |
| 1 m | 1.9e-08 m | 5.96e-08 m |
| 10 m | 2.7e-07 m | 5.96e-07 m |
| 100 m | 2.8e-06 m | 5.96e-06 m |
| 1 km | 3.7e-05 m | 5.96e-05 m |
| 10 km | 4.1e-04 m | 5.96e-04 m |
| 100 km | 3.0e-03 m | 5.96e-03 m |
| 1000 km | 3.3e-02 m | 5.96e-02 m |
| 10000 km | 3.3e-01 m | 5.96e-01 m |

Measured worst case is ~0.55× the bound (the bound assumes all three
components round outward simultaneously).

## 3. Where float32 degrades

- **< 10 km:** < 0.6 mm — imperceptible; safe for anything.
- **100 km:** ~3–6 mm — fine for terrain and objects.
- **1000 km:** ~3.3–6 cm — visible jitter on smooth surfaces, z-fighting
  risk on co-planar geometry.
- **10000 km (half the planet):** ~0.3–0.6 m — broken.

The error grows **linearly** with distance from the frame origin; there is
no cliff, just steady degradation.

## 4. Re-anchoring guarantees

**Re-anchoring rule:** the frame origin must stay within **R = 100 km** of
the camera. When exceeded, create a new `LocalFrame` at the camera's ECEF
position and convert active data with `vp::geo::convertLocal`.

Guarantees (all verified by `test/geo_test.cpp`):

- **G1.** Any point within 100 km of the frame origin has float32
  representation error ≤ 6 mm (bound) / ~3 mm (measured).
- **G2.** `convertLocal` (frame-to-frame, double) has error < 1 µm.
- **G3.** LLA→ECEF→LLA round-trip is < 1 mm anywhere in the engine's domain
  (h > ~-20 km; grid + 10,000 random points).
- **G4.** `LocalFrame` toWorld/toLocal round-trip (double) is
  < 1e-9 · d.

Re-anchoring cost is proportional to *active* (rendered) data, bounded by
view distance — never to world size.

## 5. Obligations by system

- **Terrain:** tile origins/positions stored as ECEF double; vertex buffers
  uploaded as float32 local coordinates in the current frame; on
  re-anchoring, re-express (or re-upload) affected tiles. Never store
  rendered vertex data in ECEF float32.
- **Streaming:** tile addressing is frame-independent (ECEF double or
  integer tile IDs — step 3). Load/unload decisions use double distances.
- **LOD:** level selection from double distances (camera ECEF → object
  ECEF), never from float32 local distances beyond the re-anchoring radius.
- **Camera:** stored as ECEF double; rendered in the local frame; the
  re-anchoring trigger is `distanceEcef(camera, frame origin) > 100 km`.

## 6. Hard budgets (tested)

`test/geo_test.cpp` enforces, on every run:

- LLA→ECEF→LLA round-trip: < 1e-3 m altitude, < 1e-8 deg lat/lon
  (~1.1 mm), grid + 10,000 random points.
- float32 local round-trip: < 1e-6 m at 1 m, < 1e-4 m at 1 km,
  < 1e-2 m at 100 km, < 0.2 m at 1000 km.
- float32 error never exceeds the theoretical bound 2^-24·d (with double
  rounding margin).
- Edge cases: both poles (exact + near-axis), dateline (±180, wrapping),
  altitude extremes (-20 km to +40,000 km), Earth-center degeneracy.

If a future change weakens any of these, the tests fail — update this
document and the tests together.
