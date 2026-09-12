# Architecture

## Layers

- **Core application/runtime** — implemented (`vp_core`): Vulkan instance
  (validation layer auto-detected), physical/logical device, graphics queue,
  VMA allocator, command pool, render pass. Headless; runs on lavapipe.
- **Windowed app** — implemented (`vp_windowed`, `-DBUILD_WINDOWED=ON`):
  GLFW window + ImGui debug overlay, Vulkan surface. Not yet run (needs a
  display).
- Geospatial, World, Streaming, Rendering, Application layers — not yet
  implemented. The conceptual layering is defined in `agents.md`.

## Major Components

- `src/main.cpp` — `vp_core`: the core runtime (step 1).
- `src/app_windowed.cpp` — `vp_windowed`: GLFW+ImGui shell (from the
  vulkan-dev template).

## Data Flow

None yet (single-shot render pass in `vp_core`).

## Performance Characteristics

No measurements yet. Container renders via lavapipe (software); see
`PERFORMANCE.md` for what is and isn't measurable here.

## Security Boundaries

None yet.

## Known Rough Edges

- `vp_core` is a linear one-shot program, not yet a main loop; it will be
  restructured as world systems are added.
- `vp_windowed` untested (no display in this container).
