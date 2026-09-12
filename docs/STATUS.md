# Status

## Last Updated

2026-09-12

## Just Finished

- Tooling pass (uncommitted work from the previous session, completed and
  verified):
  - Formatting: `.clang-format` (Google style), whole codebase reformatted.
  - Static analysis: `.clang-tidy` baseline with reasoned disables (the
    test harness's macros, printf diagnostics, and deliberate patterns are
    documented in the file). Project code is at **0 warnings**.
  - Tidy fixes: `[[nodiscard]]` on `LocalFrame` accessors, Vulkan handle
    initialization, explicit bool conversions, brace hygiene, targeted
    NOLINTs with reasons (fixed callback vtable signatures, deterministic
    test seeds).
  - `src/vma_impl.cpp`: VMA implementation moved out of `main.cpp` into its
    own TU (not analyzed by clang-tidy), CMake updated.
  - Host build support: `REF_REPOS`/`VULKAN_INCLUDE_DIR` are CMake cache
    options; refs live in `~/Documents/code/agents/ref_repos`
    (vma→VulkanMemoryAllocator symlink, glm, Vulkan-Headers). See
    CONVENTIONS.md (also documents the broken system cmake workaround:
    pip-installed cmake in `~/.local/bin`).
  - Portability fix: `std::clamp` in geo.cpp needed `<algorithm>` (newer
    g++ does not pull it transitively).
  - Verified on the host: build clean, 12/12 geo tests pass, clang-format
    clean, clang-tidy 0 warnings, and `vp_core` runs end-to-end on real
    hardware (NVIDIA RTX 4060 — not lavapipe; validation layer not
    installed on the host, instance reports it off).

## In Progress

- Step 3: world-coordinate model. Decision recorded:
  `docs/decisions/0002-world-cell-addressing.md` — global geodetic
  quadtree (OGC WorldCRS84Quad layout: level L = 2^(L+1) x 2^L cells,
  2 hemisphere roots, north-first rows, half-open containment, dateline
  wrap; packed uint64 CellId, kMaxCellLevel 25). Implementation of
  `src/geospatial/cells.{h,cpp}` + `test/cells_test.cpp` starting.

## Performance

No meaningful measurements yet. Container renders via lavapipe (software);
the HOST has a real GPU (RTX 4060) — use the host for anything
timing-related. See `PERFORMANCE.md`. Coordinate math is pure CPU;
its test budgets are absolute.

## Known Issues

- `vp_windowed` has not been built/run (no display configured).
- Host has no Vulkan validation layer installed (instance reports it off);
  container runs still have it.
- Geodetic altitude is ellipsoidal (WGS-84), not mean sea level; MSL data
  needs a geoid model (EGM2008) at the ingestion boundary (later step).
- System cmake/ctest on the host are broken (stale libjsoncpp); use the
  pip-installed cmake (`~/.local/bin/cmake`) — see CONVENTIONS.md.

## Next Step

Finish step 3: implement `src/geospatial/cells.{h,cpp}` and
`test/cells_test.cpp` per decision 0002 (addressing, bounds, extent,
hierarchy, neighbors — no streaming/caching), wire into `vp_geo` and the
test target, verify (build + tests + clang-format + clang-tidy 0
warnings), update GOALS/ARCHITECTURE/WORLD docs, commit.
