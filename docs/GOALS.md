# Goals

## Current Focus

Build the foundational engine systems: core application/runtime, math and
geographic coordinate systems, and a basic world representation.

## Near-Term

- Core application/runtime (window, main loop, basic Vulkan render target) — done (step 1)
- Math and geographic coordinate systems — done (step 2: `vp_geo`, decision
  0001, precision contract in `docs/PRECISION.md`)
- World-coordinate model (tile/cell addressing, spatial partitioning) — next
- Basic world/terrain representation
- Walkable / flyable player movement

## Longer-Term

- Earth-scale world: real terrain/elevation, biomes, water, vegetation
- Streaming, LOD, caching, spatial indexing
- Real geographic data ingestion pipeline
- Configurable rendering quality across hardware levels

## Out of Scope

Flight simulation, aircraft physics, combat, airports, navigation systems
(unless explicitly requested later).
