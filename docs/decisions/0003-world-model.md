# Decision: World Model — Resident-Cell Trie with Typed Content Slots

## Date

2026-09-12

## Context

Step 4 is the basic world representation: a world state layer on top of the
cell addressing (decision 0002) and the coordinate foundation (decision
0001). Constraints:

1. **Systems, not demos** (agents.md): the world model must be reusable by
   terrain (step 5), biomes, streaming (step 7), and the debug overlay
   without rework — the content boundary is the point of the step.
2. **Cells are containers.** The addressing layer (0002) is deliberately
   content-free; something must own per-cell content and its lifecycle.
3. **Streaming comes later** (step 7): which cells are resident, eviction,
   async loading, and LOD selection are policy decisions this step must
   support but NOT make.
4. **Determinism is a contract** (agents.md): generated content must record
   how it was produced (generator, version, parameters, seed) so
   regeneration, caching, and debugging can reproduce it.
5. **Debug tooling early** (agents.md): loaded-cell counts and memory usage
   must be answerable from the start, not retrofitted.

## Decision

1. **New module `vp_world`** (`src/world/`, namespace `vp::world`, links
   `vp_geo`): the first piece of the World layer in the conceptual layout.
   `vp_geo` stays pure math; world state lives here.
2. **Resident cells form a sparse trie.** `ensureCell(id)` creates the cell
   AND all missing ancestors, so a resident non-root cell always has a
   resident parent. `removeCell(id)` removes leaf cells only (no resident
   children); subtree pruning is deferred to streaming policy (step 7),
   which can walk `residentChildCount()`.
3. **Content slots keyed by `ContentKind`** (Terrain, Biomes, Water,
   Vegetation, Structures — extensible enum matching the agents.md world
   hierarchy). Payloads are type-erased `CellContent` subclasses owned by
   the cell, one slot per kind. The interface carries `kind()`,
   `provenance()`, and `sizeBytes()` — kind validation, determinism
   bookkeeping, and memory accounting — and nothing more.
4. **Provenance on every content**: generator id + version, parameter hash,
   seed (`ContentProvenance`). Generators (step 5+) must fill it honestly;
   it is the unit of cache identity later.
5. **The position query the runtime actually asks:**
   `residentCellAt(ECEF, level)` — the deepest resident cell containing the
   position at or above `level` (falls back up the resident trie; nullptr
   if the region is untouched).
6. **Threading: not thread-safe.** World state is single-threaded until
   async streaming (step 7) introduces its own synchronization boundary.
   Cell iteration order is unspecified (hash map); deterministic outputs
   must never depend on visit order — systems that care sort their own
   work.
7. **The unit-test executable is renamed** `vp_geo_tests` → `vp_unit_tests`
   (CTest: `unit_tests`): it now spans modules (geo, cells, world). The
   in-house harness stays.

## Alternatives

- **Flat `CellId → content` map, no hierarchy:** weakest invariants; every
  consumer re-derives parent/child relationships; subtree eviction and
  coverage queries become manual bookkeeping. Rejected.
- **Typed content members on WorldCell** (`terrain*`, `biomes*`, ...):
  strong typing, but every new system edits the core struct and rebuilds
  the world — the opposite of composable. Rejected.
- **`std::any` payloads:** no lifetime hook, no memory accounting, no kind
  validation without extra ceremony. The tiny `CellContent` interface gives
  all three. Rejected.
- **Auto-pruning empty ancestors on removal:** tempting, but pruning policy
  (how aggressively, refcounting vs recency) belongs to streaming; doing it
  now would bake in policy before the workload exists. Deferred.
- **Thread-safe world from day one:** locks on every query before there is
  any concurrency; the streaming step will define the actual boundary.
  Deferred.

## Consequences

- Terrain (step 5) plugs in as a `CellContent` subclass plus a generator
  that produces it — no world-model rework. Same for biomes, water, etc.
- Streaming (step 7) gets: O(1) resident lookup, trie invariants for
  bottom-up eviction, per-kind content lifecycle, and provenance as cache
  identity. It adds policy (which cells, when) and concurrency — the
  structures here are deliberately policy-free.
- Empty ancestor cells left behind by leaf removal remain resident and are
  valid answers from `residentCellAt`: residency, not content, is that
  query's criterion (content-aware fallback would conflate world state
  with generation state and belongs to a higher layer if ever needed). A
  pruning sweep is trivial to add later using `residentChildCount()`.
- The debug overlay (agents.md) has its data source from day one:
  `WorldStats` (resident cells by level, content counts by kind, bytes).

## Performance Considerations

- Resident lookup: one unordered_map probe, O(1).
- `ensureCell`: O(level) ancestor creation on first touch of a region,
  O(1) when the chain exists. Total resident cells are bounded by what
  streaming decides to load — never by world size.
- Queries (`find`, `residentCellAt`, `stats`): no allocation. `stats()`
  walks all resident cells (O(resident)) — fine at engine scale; streaming
  can cache counters later if it ever shows up.
- Content memory: `sizeBytes()` lets the debug overlay account VRAM/RAM
  per kind without intrusive tooling.

## Quality Considerations

- Tested invariants (test/world_test.cpp): ensure idempotence; ancestor
  chain completeness and child counts; leaf-only removal (content
  destroyed exactly once); content attach/detach/replace lifecycle with
  ownership transfer; per-kind slot independence; stats correctness;
  residentCellAt containment/fallback incl. dateline and pole positions;
  re-ensure after removal yields a fresh cell.
- Determinism: provenance round-trips; no hidden global state; iteration
  order explicitly unspecified so tests and systems cannot rely on it.
