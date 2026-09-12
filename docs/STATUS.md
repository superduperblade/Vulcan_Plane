# Status

## Last Updated

2026-09-12

## Just Finished

- Step 4: basic world representation — complete.
  - Decision recorded: `docs/decisions/0003-world-model.md` — resident
    cells form a sparse trie (ensureCell creates the ancestor chain,
    leaf-only removal, pruning deferred to streaming); content slots
    keyed by ContentKind (Terrain/Biomes/Water/Vegetation/Structures);
    provenance on every payload (generator/version/paramsHash/seed — the
    determinism contract and future cache identity); residentCellAt as
    the position query; deliberately thread-unsafe and policy-free until
    streaming (step 7).
  - Semantics ruling (recorded in 0003): `residentCellAt` matches on
    RESIDENCY, not content — an empty resident ancestor is a valid
    answer. Content-aware fallback would be a higher-layer concern.
  - Implementation: `src/world/world.{h,cpp}` (library `vp_world`,
    namespace `vp::world`): World/WorldCell/CellContent/
    ContentProvenance/WorldStats. O(1) lookup, O(level) first-touch
    chain creation, no allocation on queries, byte accounting via
    sizeBytes().
  - Tests: `test/world_test.cpp` — 14 cases (ensure idempotence,
    ancestor chains + child counts, leaf-only removal with destruction
    tracking, content lifecycle/ownership/kind independence, stats
    correctness, residentCellAt fallback incl. dateline/poles,
    provenance round-trip, spawn scenario). 40/40 cases pass total
    (12 geo + 14 cells + 14 world). Suite validated against a /tmp
    reference implementation under ASan+UBSan before integration.
  - `vp_core` now exercises the world lifecycle around the spawn point
    (3x3 L12 neighborhood + L14 center, content attach, resident-trie
    queries with fallback, teardown) — verified end-to-end on the RTX
    4060.
  - Test executable renamed `vp_geo_tests` → `vp_unit_tests` (CTest:
    `unit_tests`) — it spans geo, cells, and world.
  - Tooling: all project files clang-format clean; clang-tidy 0
    warnings (the world test file needed a cleanup pass after landing:
    literal suffixes, bounds-safe indexing, analyzer null-shapes — the
    harness's continue-on-failure VP_CHECK is invisible to the static
    analyzer, documented pattern now: capture pointer, check, deref).

## In Progress

Nothing in flight — step 4 is complete and verified.

## Performance

No meaningful measurements yet. The HOST has a real GPU (RTX 4060) —
use it for anything timing-related; the dev container is lavapipe
(software). See `PERFORMANCE.md`. All geo/cells/world math is pure CPU;
`cellOf(ECEF)` costs ecefToLla (~100–200 ns) + O(1) integer work;
`World` queries are O(1) hash lookups; `ensureCell` is O(level) on
first touch of a region.

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
  (addressing is total; patch construction is a terrain-design concern).
- The static analyzer cannot see the test harness's continue-on-failure
  VP_CHECK semantics; null-deref findings in tests are shaped as
  capture-check-deref with NOLINT where unavoidable (pattern documented
  in test/world_test.cpp).

## Next Step

Step 5: terrain representation — the first real `CellContent` payload.
Design a deterministic terrain generator producing heightfield data for
a cell (per decision 0001's precision contract: tile-local float32 for
rendering, double for decisions), a generator interface that fills
provenance honestly, and tests pinning determinism (same seed/params →
same bytes) and precision budgets. The terrain data pipeline stays
renderer-independent (agents.md); rendering terrain on the GPU comes
after. Consider whether procedural detail hooks (noise layers) belong in
the generator interface now or at a later step — decision record
required.
