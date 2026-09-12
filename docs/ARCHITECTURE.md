# Architecture

## Layers

- **Core application/runtime** — implemented (`vp_core`): Vulkan instance
  (validation layer auto-detected), physical/logical device, graphics queue,
  VMA allocator, command pool, render pass. Headless; runs on lavapipe.
- **Windowed app** — implemented (`vp_windowed`, `-DBUILD_WINDOWED=ON`):
  GLFW window + ImGui debug overlay, Vulkan surface. Not yet run (needs a
  display).
- **Geospatial** — implemented (`vp_geo`, step 2): WGS-84 coordinate core,
  pure CPU (glm only). `LLA` (degrees/m, ellipsoidal altitude) ↔ `ECEF`
  (double), `LocalFrame` (ENU frame at a moving origin), `convertLocal`
  (re-anchoring). Canonical representation and re-anchoring strategy:
  `docs/decisions/0001-coordinate-system.md`; precision contract:
  `docs/PRECISION.md`.
- World, Streaming, Rendering, Application layers — not yet implemented. The
  conceptual layering is defined in `agents.md`.

## Major Components

- `src/main.cpp` — `vp_core`: the core runtime (step 1); now also exercises
  the geo transforms at startup.
- `src/geospatial/geo.{h,cpp}` — `vp_geo`: geospatial coordinate core
  (step 2). Static library, pure CPU.
- `src/app_windowed.cpp` — `vp_windowed`: GLFW+ImGui shell (from the
  vulkan-dev template).
- `test/` — unit tests: `test.h` (minimal in-house harness, no external
  deps), `geo_test.cpp` (12 cases, CTest target `geo_tests`).

## Data Flow

Rendering: none yet (single-shot render pass in `vp_core`).

Coordinates: geographic input (LLA) → `llaToEcef` → ECEF double (canonical
world state) → `LocalFrame::toLocal` → float32 ENU at the render boundary →
GPU. Re-anchoring: `convertLocal` between frames (double). See
`docs/PRECISION.md`.

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
