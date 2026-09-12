cmake_minimum_required(VERSION 3.16)
project(Vulkan_plane CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# VS Code (C/C++ extension) IntelliSense.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Reference repos (VMA, glm, ...). Default: docs-search extension volume in
# the dev container. On a host build, override with -DREF_REPOS=<dir>
# containing vma/ and glm/ checkouts (see CONVENTIONS.md).
set(REF_REPOS "/root/.pi/agent/docs-search/repos" CACHE PATH
    "Directory containing reference repo checkouts (vma/, glm/)")

# VMA (header-only in 3.x: INTERFACE library)
add_subdirectory(${REF_REPOS}/vma ${CMAKE_BINARY_DIR}/vma EXCLUDE_FROM_ALL)

# Geospatial coordinate core (WGS-84: LLA <-> ECEF, local ENU frames,
# world cell addressing per decision 0002). Pure CPU (glm only, no Vulkan).
add_library(vp_geo STATIC src/geospatial/geo.cpp src/geospatial/cells.cpp)
target_include_directories(vp_geo PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${REF_REPOS}/glm)

# Vulkan headers live on the system include path in the dev container; on a
# host without the vulkan-headers package, point this at a Vulkan-Headers
# checkout's include/ directory.
set(VULKAN_INCLUDE_DIR "" CACHE PATH
    "Extra include dir providing vulkan/vulkan.h (empty: system path)")

# World model (step 4): resident-cell trie + typed content slots.
# Pure CPU; depends on vp_geo for CellId/positions. See
# docs/decisions/0003-world-model.md.
add_library(vp_world STATIC src/world/world.cpp)
target_include_directories(vp_world PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vp_world PUBLIC vp_geo)

# Core: headless Vulkan runtime (instance, device, VMA, render pass).
# Runs on lavapipe (software) when no GPU is present.
add_executable(vp_core src/main.cpp src/vma_impl.cpp)
target_link_libraries(vp_core PRIVATE VulkanMemoryAllocator vulkan vp_geo
  vp_world)
if(VULKAN_INCLUDE_DIR)
  target_include_directories(vp_core PRIVATE ${VULKAN_INCLUDE_DIR})
endif()

# Unit tests (pure CPU, headless). See test/ and CONVENTIONS.md.
# One executable spanning modules (geo, cells, world); main() lives in
# test/geo_test.cpp.
option(BUILD_TESTS "Build unit tests" ON)
if(BUILD_TESTS)
  enable_testing()
  add_executable(vp_unit_tests test/geo_test.cpp test/cells_test.cpp
    test/world_test.cpp)
  target_link_libraries(vp_unit_tests PRIVATE vp_world)
  add_test(NAME unit_tests COMMAND vp_unit_tests)
endif()

# Windowed app (GLFW + ImGui debug overlay). Needs a display; build with
# -DBUILD_WINDOWED=ON on hardware with X11.
option(BUILD_WINDOWED "Build the glfw+imgui windowed app" OFF)
if(BUILD_WINDOWED)
  add_executable(vp_windowed src/app_windowed.cpp)
  target_include_directories(vp_windowed PRIVATE
    ${REF_REPOS}/glfw/include
    ${REF_REPOS}/imgui
    ${REF_REPOS}/imgui/backends)
  target_sources(vp_windowed PRIVATE
    ${REF_REPOS}/imgui/imgui.cpp
    ${REF_REPOS}/imgui/imgui_demo.cpp
    ${REF_REPOS}/imgui/imgui_draw.cpp
    ${REF_REPOS}/imgui/imgui_tables.cpp
    ${REF_REPOS}/imgui/imgui_widgets.cpp
    ${REF_REPOS}/imgui/backends/imgui_impl_glfw.cpp
    ${REF_REPOS}/imgui/backends/imgui_impl_vulkan.cpp)
  target_link_libraries(vp_windowed PRIVATE VulkanMemoryAllocator vulkan)
  target_include_directories(vp_windowed PRIVATE ${REF_REPOS}/glm)
  # glfw built from the cloned repo (X11 only; no wayland-scanner in bookworm)
  set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
  add_subdirectory(${REF_REPOS}/glfw ${CMAKE_BINARY_DIR}/glfw EXCLUDE_FROM_ALL)
  target_link_libraries(vp_windowed PRIVATE glfw)
  target_compile_definitions(vp_windowed PRIVATE GLFW_INCLUDE_VULKAN VK_USE_PLATFORM_XLIB_KHR)
endif()
