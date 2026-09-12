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

## Formatting / Linting

Not defined yet.

## Supported Environments

- This container: Debian bookworm, g++ 12, CMake 3.25, Ninja, lavapipe
  (llvmpipe) software Vulkan, Khronos validation layer.
- Real hardware: X11 display for the windowed app.

## Test Commands

Not defined yet (no test framework chosen; step 2 will introduce unit tests
for the coordinate systems).

## Benchmark Commands

Not defined yet.

## Performance-Sensitive Areas

Terrain streaming, LOD transitions, and world streaming (anticipated).
