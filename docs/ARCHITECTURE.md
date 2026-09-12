# Architecture

## Layers

- **Core application/runtime** — implemented (`vp_core`): Vulkan instance
  (validation layer auto-detected), physical/logical device, graphics queue,
  VMA allocator, command pool, render pass. Headless; runs on lavapipe.
- **Windowed app** — implemented (`vp_windowed`, `-DBUILD_WINDOWED=ON`):
  GLFW window + ImGui debug overlay, Vulkan surface. Not yet run (needs a
  display).
- **Geospatial** — implemented (`vp_geo`, steps 2–3): WGS-84 coordinate
  core, pure CPU (glm only). `LLA` (degrees/m, ellipsoidal altitude) ↔
  `ECEF` (double), `LocalFrame` (ENU frame at a moving origin),
  `convertLocal` (re-anchoring). Canonical representation and re-anchoring
  strategy: `docs/decisions/0001-coordinate-system.md`; precision contract:
  `docs/PRECISION.md`. World cell addressing (step 3): global geodetic
  quadtree, `CellId`/`cellOf`/hierarchy/neighbors —
  `docs/decisions/0002-world-cell-addressing.md`.
- **World model** — implemented (`vp_world`, step 4): resident-cell sparse
  trie over `vp::geo::CellId` (ensure/ancestor chain, leaf-only removal),
  per-cell content slots keyed by `ContentKind` with provenance
  (determinism) and byte accounting, `residentCellAt` position query,
  `WorldStats` for the debug overlay. Deliberately policy-free: which
  cells to load, eviction, async loading, and LOD are streaming concerns
  (step 7). Decision: `docs/decisions/0003-world-model.md`.
- Streaming, Rendering, Application layers — not yet implemented. The
  conceptual layering is defined in `agents.md`.

## Major Components

- `src/main.cpp` — `vp_core`: the core runtime (step 1); now also exercises
  the geo transforms at startup.
- `src/geospatial/geo.{h,cpp}` — `vp_geo`: geospatial coordinate core
  (step 2). Static library, pure CPU.
- `src/geospatial/cells.{h,cpp}` — `vp_geo`: world cell addressing, global
  geodetic quadtree (step 3); `src/vma_impl.cpp` hosts the VMA
  implementation (not analyzed by clang-tidy).
- `src/world/world.{h,cpp}` — `vp_world`: world state (step 4). Resident
  cells, content slots, provenance, stats.
- `src/app_windowed.cpp` — `vp_windowed`: GLFW+ImGui shell (from the
  vulkan-dev template).
- `test/` — unit tests: `test.h` (minimal in-house harness, no external
  deps), `geo_test.cpp` (12 cases, step 2), `cells_test.cpp` (14 cases,
  step 3), `world_test.cpp` (step 4) — one executable `vp_unit_tests`
  (CTest `unit_tests`).

## Data Flow

Rendering: none yet (single-shot render pass in `vp_core`).

Coordinates: geographic input (LLA) → `llaToEcef` → ECEF double (canonical
world state) → `LocalFrame::toLocal` → float32 ENU at the render boundary →
GPU. Re-anchoring: `convertLocal` between frames (double). See
`docs/PRECISION.md`.

Cell addressing (step 3): ECEF → `cellOf(level)` → `CellId` — a derived
integer index for streaming/LOD/spatial-partitioning decisions only; world
state identity stays ECEF double. See `docs/decisions/0002`.

World state (step 4): `World::ensureCell`/`removeCell` maintain the
resident-cell trie; content payloads attach per `ContentKind` with
provenance; `residentCellAt(ECEF, level)` is the position query the
runtime uses. Streaming (step 7) will supply the residency policy on top.
See `docs/decisions/0003`.

## Performance Characteristics

No measurements yet. Container renders via lavapipe (software); see
`PERFORMANCE.md` for what is and isn't measurable here.

## Security Boundaries

None yet.

## Known Rough Edges

- `vp_core` is a linear one-shot program, not yet a main loop; it will be
  restructured as world systems are added.
- `vp_windowed` untested (no display in this container).
- `vp_geo` altitude is ellipsoidal; MSL conversion (geoid) is a later
  ingestion-boundary concern.
