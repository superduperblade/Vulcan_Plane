# Status

## Last Updated

2026-09-12

## Just Finished

- Step 3: world-coordinate model — complete.
  - Decision recorded: `docs/decisions/0002-world-cell-addressing.md` —
    global geodetic quadtree (OGC WorldCRS84Quad layout: level L =
    2^(L+1) x 2^L cells; level 0 = two hemispheres; north-first rows;
    longitude [west, east) / latitude (south, north] from a shared
    north/west floor rule; +180 ≡ −180 → x = 0; poles clamped into edge
    rows). Alternatives rejected: web-mercator (no addresses beyond
    ±85°), cube-sphere, HEALPix, icosahedral DGGS, flat ECEF grid.
  - Implementation: `src/geospatial/cells.{h,cpp}` (`vp::geo`, in
    `vp_geo`): packed `CellId` (level:6|x:26|y:25, kMaxCellLevel 25 ≈
    0.6 m), `cellOf(LLA|ECEF, level)` (pure function of canonical
    position; ECEF via `ecefToLla`), `bounds`, `extentMeters` (width =
    mid-latitude parallel arc, height = exact meridional arc —
    sub-mm vs canonical geodesy), `contains`, `parent`/`children`/
    `ancestorAtLevel`, `neighbor` (dateline wrap, pole clamp),
    `acrossPole`.
  - Tests: `test/cells_test.cpp` — 14 cases: layout constants, packing
    round-trips/validation, pinned root/edge/pole addresses, ECEF≡LLA
    addressing, containment/half-open edges, dateline wrap,
    across-pole adjacency, parent/children/level-consistency
    (`ancestorAtLevel(cellOf(p,L),L') == cellOf(p,L')`) over grids +
    seeded random points, neighbor adjacency, ellipsoidal extents,
    frame-independence across re-anchoring. All 26 cases (12 geo + 14
    cells) pass.
  - Tooling regression caught and fixed during integration: the clang-
    tidy pass had inverted the physical-device selection loop condition
    (`!phys` → `phys != VK_NULL_HANDLE` instead of `==`), which made
    `vp_core` fail at runtime despite clean builds/tests — caught by
    the integration smoke run on the RTX 4060, fixed, and vp_core now
    completes end-to-end on real hardware.
  - Host build environment documented in CONVENTIONS.md (REF_REPOS/
    VULKAN_INCLUDE_DIR options, refs in `~/Documents/code/agents/
    ref_repos`, pip cmake workaround, sandbox `env -u APPIMAGE -u
    APPDIR` quirk for direct compiler invocation).

## In Progress

Nothing in flight — step 3 is complete and verified.

## Performance

No meaningful measurements yet. The HOST has a real GPU (RTX 4060) —
use it for anything timing-related; the dev container is lavapipe
(software). See `PERFORMANCE.md`. Coordinate and cell math are pure CPU;
`cellOf(ECEF)` costs ecefToLla (~100–200 ns) + O(1) integer work —
streaming-decision rates, never per-vertex (per decision 0002).

## Known Issues

- `vp_windowed` has not been built/run (no display configured).
- Host has no Vulkan validation layer installed (instance reports it
  off); container runs have it.
- Geodetic altitude is ellipsoidal (WGS-84), not mean sea level; MSL
  data needs a geoid model (EGM2008) at the ingestion boundary (later
  step).
- System cmake/ctest on the host are broken (stale libjsoncpp); use the
  pip-installed cmake — see CONVENTIONS.md.
- Terrain rendering near the poles must handle degenerate patches
  (addressing is total; patch construction is a terrain-design concern
  for step 5).

## Next Step

Step 4: basic world representation — a world state layer on top of the
cell addressing: cells as containers of content (terrain/biome handles),
the world-state store keyed by ECEF/CellId, and the minimal runtime glue
(vp_core main loop restructure) that streaming (step 7) will extend.
Design the content-data boundary so terrain generation (step 5) can be
added without rework, per agents.md (terrain pipeline independent of the
renderer).
