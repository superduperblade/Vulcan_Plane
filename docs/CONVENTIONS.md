# Conventions

## Build

```bash
# Headless core (works in this container, lavapipe):
cmake -S . -B build -G Ninja
cmake --build build

# Windowed app (needs a display / X11):
cmake -S . -B build -G Ninja -DBUILD_WINDOWED=ON
cmake --build build
```

Or use the `cpp_build` tool (auto-detects CMake+Ninja, saves full logs).

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
