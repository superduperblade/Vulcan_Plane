# Conventions

## Build

```bash
# Headless core (works in this container, lavapipe):
cmake --preset default          # or: cmake -S . -B build -G Ninja
cmake --build build

# Windowed app (needs a display / X11):
cmake --preset windowed         # or: cmake -S . -B build -G Ninja -DBUILD_WINDOWED=ON
cmake --build build
```

Or use the `cpp_build` tool (auto-detects CMake+Ninja, saves full logs).

## VS Code

Open the project root; CMake Tools picks up `CMakePresets.json` (preset
`default`). IntelliSense uses `build/compile_commands.json` (exported by
CMake). Tasks: `cmake: build`, `cmake: build windowed`, `run: vp_core`
(headless). Debug config: `Debug vp_core` (gdb).

## Run

```bash
./build/vp_core            # headless core; runs on lavapipe, validation layer on
./build/vp_windowed        # needs a display
```

Or use the `vulkan_run` tool (validation layer on by default, surfaces VUIDs).

## Reference Repos

Third-party code (VMA, GLFW, ImGui, GLM, stb, ...) is NOT vendored. It is
cloned by the docs-search extension into `/root/.pi/agent/docs-search/repos`
and referenced from CMake via `REF_REPOS`. Do not copy it into the project.

The docs-search BM25 corpus indexes only spec/docs-oriented libraries
(vulkan-docs, vulkan-headers, vma, vk-bootstrap, vulkan-samples). The C
libraries are `cloneOnly` there — their content is served by `cpp_lookup`
(symbol index). If you add a new reference repo, prefer `cloneOnly` in the
docs-search config unless its docs/prose are genuinely useful to search.

## Memory (mnemopi)

- Canonical project knowledge lives in `docs/` (per PROTOCOL.md). Do NOT
duplicate docs/ content into long-term memory.
- Use `remember` for: environment/tooling facts, user preferences,
  cross-project lessons, and decisions that don't fit a docs/ file.
- Use `recall` before answering questions about prior sessions or stored
  context.

## Code Style

- C++17. Namespaces: `vp::` for engine code (`vp::geo` for the geospatial
  core). Types are `CamelCase`, functions/variables `camelCase`, constants
  `kCamelCase`.
- Formatting/linting: not defined yet (clang-tidy is available in the
  container; a config is planned).

## Test Commands

```bash
cmake --build build                 # builds vp_geo_tests too (BUILD_TESTS=ON)
ctest --test-dir build              # run all tests (CTest)
./build/vp_geo_tests                # run the geo tests directly
./build/vp_geo_tests --report       # print measured float32 error vs distance
```

Test framework: a minimal in-house harness (`test/test.h`, no external
dependencies — tests must run headless/offline). If the project outgrows
it, replace with doctest/gtest and keep the `VP_TEST`/`VP_CHECK` names.
Precision budgets are documented in `docs/PRECISION.md` and enforced by
`test/geo_test.cpp`.

## Supported Environments

- This container: Debian bookworm, g++ 12, CMake 3.25, Ninja, lavapipe
  (llvmpipe) software Vulkan, Khronos validation layer, clang-tidy.
- Real hardware: X11 display for the windowed app.

## Benchmark Commands

Not defined yet.

## Performance-Sensitive Areas

Terrain streaming, LOD transitions, and world streaming (anticipated).
