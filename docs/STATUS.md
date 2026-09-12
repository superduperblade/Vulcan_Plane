# Status

## Last Updated

2026-09-12

## Just Finished

- Implementation step 2: math and coordinate systems — complete.
  - Coordinate decision recorded:
    `docs/decisions/0001-coordinate-system.md` (ECEF double storage /
    identity, LLA for geographic data, origin-relative float32 rendering,
    100 km re-anchoring rule).
  - Math core: `vp_geo` static library (`src/geospatial/geo.{h,cpp}`,
    pure CPU on glm): LLA↔ECEF (WGS-84), `LocalFrame` (ENU, double),
    `convertLocal` for re-anchoring. Linked into `vp_core`, which now
    exercises the transforms at startup.
  - Precision contract: `docs/PRECISION.md` — where float32 degrades
    (linear in distance from origin: ~3 mm at 100 km, ~3.3 cm at 1000 km),
    the re-anchoring guarantees, and per-system obligations (terrain,
    streaming, LOD, camera).
  - Unit tests: `test/geo_test.cpp` (12 cases, minimal in-house harness in
    `test/test.h`, CTest-integrated). All pass headless: LLA round-trip
    < 1 mm (grid + 10,000 random points), float32 budgets at
    1 m / 1 km / 100 km / 1000 km plus the theoretical 2^-24·d bound,
    poles / dateline / altitude-extreme edge cases.

## In Progress

Nothing in flight — step 2 is complete and verified.

## Performance

No meaningful measurements yet. Note: this container renders via lavapipe
(software) — GPU timings measured here are NOT representative of real
hardware. See `PERFORMANCE.md`. (The coordinate math is pure CPU and
platform-independent; its test budgets are absolute.)

## Known Issues

- `vp_windowed` has not been built/run (no display in this container).
- Geodetic altitude is ellipsoidal (WGS-84), not mean sea level; MSL data
  needs a geoid model (EGM2008) at the ingestion boundary (later step).

## Next Step

Step 3: world-coordinate model — frame-independent tile/cell addressing and
spatial partitioning on top of the coordinate foundation (decision 0001
defers integer tile addressing to this step), with unit tests.
