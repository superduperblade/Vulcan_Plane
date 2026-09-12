# Status

## Last Updated

2026-09-12

## Just Finished

- Project documentation established: mission (`agents.md`), standing protocol
  (`PROTOCOL.md`), and the `docs/` skeleton.
- Toolchain installed in the container: g++/cmake/ninja, libvulkan-dev,
  lavapipe (llvmpipe) ICD, Khronos validation layer.
- Implementation step 1: core application/runtime scaffolded (`vp_core`):
  instance (validation layer auto-detected) → device → VMA allocator →
  image → render pass. Builds with CMake+Ninja; runs headless on lavapipe
  with zero validation errors.
- Windowed app (`vp_windowed`, GLFW + ImGui) wired in CMake behind
  `-DBUILD_WINDOWED=ON`; needs a display, so it is built/run on real
  hardware.

## In Progress

Nothing in flight — step 1 is complete and verified.

## Performance

No meaningful measurements yet. Note: this container renders via lavapipe
(software) — GPU timings measured here are NOT representative of real
hardware. See `PERFORMANCE.md`.

## Known Issues

- `vp_windowed` has not been built/run (no display in this container).

## Next Step

Step 2: math and coordinate systems — geographic coordinate model
(lat/lon/alt ↔ world position), local coordinate frames / origin strategy,
with unit tests. Then step 3: world-coordinate model.
