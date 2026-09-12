# Conventions

## Build

```bash
# Dev container (root, refs in /root/.pi/agent/docs-search/repos):
cmake --preset default          # or: cmake -S . -B build -G Ninja
cmake --build build

# Host build (user-owned refs; e.g. this CachyOS machine):
#   deps cloned to ~/Documents/code/agents/ref_repos/{vma,glm,Vulkan-Headers}
#   (vma is a symlink to VulkanMemoryAllocator), no system vulkan headers.
cmake -S . -B build-host -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DREF_REPOS="$HOME/Documents/code/agents/ref_repos" \
  -DVULKAN_INCLUDE_DIR="$HOME/Documents/code/agents/ref_repos/Vulkan-Headers/include"
cmake --build build-host

# Windowed app (needs a display / X11):
cmake --preset windowed         # or add -DBUILD_WINDOWED=ON
```

Note (this host): `/usr/bin/cmake` and `/usr/bin/ctest` are broken (stale
binary, needs libjsoncpp.so.26 which is no longer installed). A working
cmake 4.x is installed via `pip install --user cmake` (`~/.local/bin/cmake`),
or call the wheel's binaries directly under
`~/.local/lib/python3.14/site-packages/cmake/data/bin/`. Ninja and the
compilers are system-installed and fine.

Or use the `cpp_build` tool (auto-detects CMake+Ninja, saves full logs).

## VS Code

Open the project root; CMake Tools picks up `CMakePresets.json` (preset
`default`). IntelliSense uses `build/compile_commands.json` (exported by
CMake). Tasks: `cmake: build`, `cmake: build windowed`, `run: vp_core`
(headless). Debug config: `Debug vp_core` (gdb).

## Run

```bash
./build/vp_core            # container: lavapipe, validation on
./build-host/vp_core       # host: real GPU
./build-host/vp_geo_tests  # or the container's build/vp_geo_tests
./build/vp_windowed        # needs a display
```

Or use the `vulkan_run` tool in the dev container (validation layer on by
default, surfaces VUIDs).

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
- Formatting: `clang-format` with `.clang-format` (Google style).
  Check: `clang-format --dry-run -Werror <files>`; apply with `-i`.
- Static analysis: `clang-tidy` with `.clang-tidy`. Run from a configured
  build dir (uses `compile_commands.json`):

  ```bash
  run-clang-tidy -header-filter='src/.*' -quiet \
    'src/main.cpp' 'src/geospatial/geo.cpp' 'src/geospatial/geo.h' \
    'test/geo_test.cpp' 'test/test.h'
  ```

  `src/vma_impl.cpp` (VMA implementation host) is not analyzed. Disabled
  checks are deliberate — each disable carries its reasoning in
  `.clang-tidy`; do not re-enable them without reading it.

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

- Dev container: Debian bookworm, g++ 12, CMake 3.25, Ninja, lavapipe
  (llvmpipe) software Vulkan, Khronos validation layer, clang-tidy. Refs at
  `/root/.pi/agent/docs-search/repos`.
- Host (this machine): CachyOS, user `soul`, g++ (current), clang/clang-tidy
  (current), Ninja, NVIDIA RTX 4060 (real GPU — `vp_core` runs on it;
  validation layer currently NOT installed, instance reports it off). Refs at
  `~/Documents/code/agents/ref_repos` (see Build). NOTE: project files must
  be owned by `soul` (a chown pass was needed after the container era).


## Benchmark Commands

Not defined yet.

## Performance-Sensitive Areas

Terrain streaming, LOD transitions, and world streaming (anticipated).
