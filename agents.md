# Project Mission

You are the primary long-term engineering agent for this project.
The standing engineering method is defined in [PROTOCOL.md](PROTOCOL.md) and
applies to all work here.

## Objective

Build a large-scale 3D world based on the real Earth, using real-world
geographic information as the foundation for terrain and environmental
generation. The eventual world should represent Earth-scale geography with
real topology/elevation and biome information, at a scale broadly comparable
to a flight simulator's world.

This is not currently a flight simulator. The immediate objective is to build
the underlying world and engine systems correctly so that higher-level
capabilities can be added later. At the current stage, the user should be able
to walk and/or freely move or "fly" around the generated world.

Flight simulation, aircraft physics, combat, airports, navigation systems, and
similar features are not current requirements unless explicitly requested
later.

## Core Objective

Build the project as a **system**, not as a collection of demos. Prioritize
reusable, composable, performant subsystems that can eventually support an
Earth-scale world, so individual systems can be improved or replaced without
rewriting the engine.

The long-term architecture must be capable of supporting: real-world
geographic coordinates; global terrain; large-scale elevation/topology; oceans
and land; biome classification; vegetation; natural geological structures;
rivers and lakes; cliffs, mountains, valleys, forests, deserts, tundra,
wetlands; procedural environmental detail; streaming; level of detail;
asynchronous loading; caching; spatial indexing; world generation; persistence
where appropriate; configurable rendering quality; multiple hardware capability
levels; very large numbers of world objects and environmental elements.

Do not implement every system immediately. Build the foundations that make
them possible.

## World Architecture

Think of the world as a hierarchy:

```text
Earth
 ├── Geographic Regions
 │    ├── Large Regions
 │    │    ├── Tiles / Cells
 │    │    │    ├── Terrain
 │    │    │    ├── Biomes
 │    │    │    ├── Water
 │    │    │    ├── Vegetation
 │    │    │    └── Structures
 │    │    └── ...
 │    └── ...
 └── ...
```

Do not assume every level needs the same resolution or representation. Higher
levels may contain less detail; lower levels more. The player receives detail
appropriate to their position, viewing conditions, and configured quality.

## Geographic Coordinates

Real-world coordinates are a first-class concept. The system should support
spawning and navigation using latitude / longitude / altitude, converted into
world positions through deliberate geographic/world-coordinate abstractions —
not coordinate-conversion logic scattered throughout the codebase. Be explicit
about coordinate systems, units, datum/reference assumptions, precision, and
transformations (local versus global).

## Terrain

Terrain is a system, not a static mesh. The terrain architecture should
support: large-scale elevation; multiple levels of detail; streaming;
asynchronous loading; procedural detail; caching; material/biome information;
editing or regeneration if later required. Terrain generation must not be
tightly coupled to rendering — the terrain data pipeline must be able to exist
independently of the renderer.

## Biomes and Natural Environments

Biome generation must be data-driven, not hardcoded special cases. Biome
determination may depend on latitude, elevation, temperature, precipitation,
moisture, terrain, proximity to water, and real-world biome datasets. The
system should be able to generate multiple environmental layers from a shared
underlying world model (grasslands, forests, rainforests, deserts, tundra,
wetlands, alpine regions, shrublands). Do not initially attempt photorealistic
simulation of every ecological process — build extensible environmental
systems that can become more detailed later.

## Natural Structures and Procedural Generation

The world should contain believable natural structures: mountains, valleys,
cliffs, caves where appropriate, river systems, lakes, coastlines, rock
formations, forests, wetlands, geological features. Prefer procedural or
data-driven systems over manually authored exceptions where scale makes manual
authoring impractical.

Procedural generation creates detail *around* underlying geographic truth, not
instead of it (vegetation placement, surface detail, rocks, small terrain
features, environmental scattering). Do not use procedural generation as an
excuse to ignore available real-world data.

> **Real-world structure at large scales, procedurally generated detail where appropriate.**

## Streaming, LOD, and Scale

- **Streaming** is fundamental: spatial partitioning, prioritised loading,
  unloading, asynchronous generation, caching, prefetching, view-dependent
  detail, player movement prediction where useful. No uncontrolled memory
  usage; no blocking the main thread with large generation or loading work.
- **LOD** is a general mechanism (terrain, vegetation, rocks, structures,
  water, textures, procedural detail), not isolated hacks inside individual
  renderers. Avoid obvious popping where practical.
- **Coordinate precision** is a first-class constraint: planetary scale
  breaks naive floating-point local coordinates. Design for hierarchical
  coordinates, local coordinate frames, origin rebasing, double precision for
  geographic calculations, and integer/tile addressing — chosen and tested
  against actual engine requirements, not locked in prematurely.

## Rendering and Hardware

Rendering supports multiple quality levels (very low → realistic). Graphics
quality changes how much of the world is represented visually, never the
underlying world model. The same world must remain usable on different
hardware by reducing visual detail and resource requirements, not by reducing
the world itself. Keep gameplay/world data separate from rendering quality
wherever practical.

## World Data Pipeline

Treat external geographic data as a pipeline:

```text
Source Data → Acquisition → Validation → Conversion → Processing
→ Tiling / Indexing → Storage → Runtime Streaming
```

Do not let raw source formats leak throughout the engine. Normalize important
data at deliberate boundaries. Keep source-data processing tools separate from
runtime systems where practical.

## System Separation

A likely conceptual structure (a starting point, not a mandatory layout):

```text
Core        ├── Math ├── Memory ├── Tasks / Jobs └── Utilities
Geospatial  ├── Coordinates ├── Projections/Transforms ├── Geographic Data └── Spatial Indexing
World       ├── World Model ├── Terrain ├── Biomes ├── Water ├── Vegetation ├── Structures └── Generation
Streaming   ├── Tile Management ├── Loading ├── Caching ├── LOD └── Prioritisation
Rendering   ├── Terrain Renderer ├── Object Renderer ├── Water ├── Atmosphere └── Post Processing
Application ├── Player ├── Camera ├── Input ├── Settings └── Debugging
```

## Debug and Development Tools

Build diagnostics into the engine: FPS/frame time, CPU/GPU timing, memory/VRAM
usage, loaded tile count, streaming queue, LOD state, player geographic and
tile coordinates, generation timings, cache hit/miss statistics,
visible-object counts. These must answer: what is the engine doing, where is
the performance going, what is loaded, why is this tile here, why this detail
level, what is causing the frame-time spike. Do not wait until the project
becomes impossible to diagnose.

## Spawn and Navigation

The application should allow spawning using real-world coordinates (latitude /
longitude / altitude), converted consistently into the world representation so
the same real-world position can always be located. Movement may initially be
simple: walk, move freely, and optionally fly/debug-fly. No realistic aircraft
simulation until explicitly requested.

## Long-Term Extensibility

Future systems may include: vehicles, aircraft, roads, buildings, cities,
infrastructure, weather, atmosphere, day/night, navigation, multiplayer,
simulation, dynamic ecosystems. Do not implement these now, but avoid
architecture that makes their eventual existence unnecessarily impossible. Do
not design the entire future of the project around hypothetical features.

## Determinism

Procedural generation should be reproducible: the same source data, generation
version, parameters, and seed must produce the same result unless
nondeterminism is deliberately required. This matters for debugging, caching,
testing, regeneration, and distributed generation. Do not introduce
nondeterminism accidentally through uncontrolled concurrency or unstable data
ordering.

## Configuration

User-facing settings should control meaningful behaviour (graphics quality,
terrain quality, vegetation density, view distance, LOD, shadows, atmosphere,
water, performance limits, spawn coordinates, movement mode) with sensible
defaults. Do not expose every internal engine variable as a user setting.

## Implementation Strategy

Build upward from foundational systems:

```text
1. Core application/runtime
2. Math and coordinate systems
3. World-coordinate model
4. Basic world representation
5. Terrain representation
6. Spatial partitioning
7. Streaming
8. LOD
9. Real geographic data ingestion
10. Biome system
11. Environmental generation
12. Water and geological systems
13. Rendering quality system
14. Performance infrastructure
15. Larger-scale world coverage
```

This order is not mandatory. Change it when actual technical dependencies
justify another sequence. Do not build large high-level features on unstable
foundations merely because they are visually exciting.

## Scope and Definition of Done

Do not silently turn a feature request into an unrelated rewrite. However,
directly related architectural or performance problems that prevent the
feature may be addressed. Use initiative; do not ask for permission for
ordinary engineering decisions; do not expand scope for unrelated improvements.

A feature is complete when: the intended behaviour works; important edge cases
are handled; relevant tests or validation exist; the implementation fits the
architecture; performance is acceptable for the intended workload; resource
usage is reasonable; important documentation is updated; known limitations are
recorded. The exact verification should match the size and risk of the change.

## Session Start / End

Session start and end procedures, the persistent documentation layout
(`docs/`), and decision records are defined in [PROTOCOL.md](PROTOCOL.md).
At the start of a meaningful session, read `docs/STATUS.md`, `docs/GOALS.md`,
and `docs/ARCHITECTURE.md`, plus relevant parts of `docs/WORLD.md` and
`docs/PERFORMANCE.md`, and compare documentation claims against the
implementation.

## Central Principle

> Build systems before features. Build reusable foundations before specialized
> implementations. Treat geographic scale and performance as first-class
> constraints. Use real-world data as the foundation and procedural generation
> as a means of extending it. Design for enormous scale without prematurely
> implementing everything. Measure important performance questions. Verify
> important assumptions. Preserve useful project knowledge between sessions.

The central question for every significant decision:

> What does the world actually need, what constraints does Earth-scale
> representation impose, what is the simplest architecture that can honestly
> satisfy those requirements, and where does additional complexity provide
> enough value to justify itself?
