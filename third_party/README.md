# Vendored third-party dependencies

Vendored as source (no vcpkg/NuGet/CMake FetchContent) so both the Windows `.vcxproj` and
the macOS `CMakeLists.txt` build from the same checked-in files with no network access
required at build time.

| Library | Version | Source |
|---|---|---|
| Dear ImGui | v1.92.9 | https://github.com/ocornut/imgui (core + `backends/imgui_impl_glfw`, `backends/imgui_impl_opengl3`) |
| GLFW | 3.4 | https://github.com/glfw/glfw (full source tree, `examples`/`tests`/`docs` stripped) |
| nlohmann/json | v3.11.3 | https://github.com/nlohmann/json (single header, vendored at `nlohmann/json.hpp`) |

To upgrade a dependency, re-fetch the pinned tag/release and replace the files in place.
