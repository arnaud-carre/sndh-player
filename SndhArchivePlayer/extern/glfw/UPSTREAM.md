# GLFW dependency

This directory vendors the unmodified GLFW 3.4 source release.

- Upstream: https://github.com/glfw/glfw
- Release: https://github.com/glfw/glfw/releases/tag/3.4
- Source archive: https://github.com/glfw/glfw/archive/refs/tags/3.4.zip
- Archive SHA-256: a133ddc3d3c66143eba9035621db8e0bcf34dba1ee9514a9e23e96afd39fd57a
- License: LICENSE.md (zlib/libpng)

The player's Visual Studio project compiles the common, null, and Win32 GLFW
sources directly, with _GLFW_WIN32 and _CRT_SECURE_NO_WARNINGS defined only for
those compilation units. This follows the source list in src/CMakeLists.txt.
No GLFW DLL, package manager, or separate dependency build is required.

Keep upstream files unchanged. When updating GLFW, update the source list,
this manifest, and the license as needed, then rebuild Debug and Release.
