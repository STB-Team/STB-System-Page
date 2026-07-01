# STB System Page

An SKSE plugin (`STB_SystemPage`) for Skyrim Special Edition. It reworks the Escape ("System") menu — hiding vanilla entries and adding custom ones (MCM shortcuts, mod events, SKSE messages) and extra settings tabs, all driven by a JSON config at `Data/SKSE/Plugins/STBSystemPage.json`.

This repository contains the C++ source needed to build the plugin DLL.

## Requirements

- **Visual Studio 2026** (v145 toolset) with the *Desktop development with C++* workload.
  Using a different VS? Adjust `generator` / `toolset` in `CMakePresets.json`.
- **CMake** ≥ 3.21
- **vcpkg** — with the `VCPKG_ROOT` environment variable pointing at your vcpkg checkout.
  CMake aborts with `Variable VCPKG_ROOT is not set` otherwise.

All library dependencies (CommonLibSSE-NG, spdlog, nlohmann-json, xbyak, …) are resolved
automatically by vcpkg in manifest mode from `vcpkg.json` / `vcpkg-configuration.json`.
CommonLibSSE-NG comes from the [colorglass registry](https://gitlab.com/colorglass/vcpkg-colorglass)
declared in `vcpkg-configuration.json`, so no submodules are required.

## Build

Easiest — run the helper script (it optionally asks for a deploy directory):

```powershell
.\gen.ps1
cmake --build build --config Release
```

Or invoke CMake directly:

```powershell
cmake --preset default -B build -S . --fresh
cmake --build build --config Release
```

The first configure will take a while — vcpkg builds all dependencies from source
into the `build/` tree.

### Deploy

By default (`COPY_BUILD=ON`) the built `STB_SystemPage.dll` + `.pdb` are copied after a
successful build to `STB_WIDGETS_DEPLOY_DIR` (defaults to a local Mod Organizer path in
`CMakeLists.txt`). Override it with the deploy prompt in `gen.ps1`, or:

```powershell
cmake --preset default -B build -S . --fresh -DSTB_WIDGETS_DEPLOY_DIR="<path>/SKSE/Plugins"
```

Set `-DCOPY_BUILD=OFF` to skip the copy step.

## Layout

| Path | Purpose |
|------|---------|
| `src/` | Plugin source (`main.cpp`, `Config.*`, `PCH.h`) |
| `include/STBSystemPageAPI.h` | Public API for other SKSE plugins to receive menu-action messages |
| `cmake/` | CMake templates (`Version.h.in`, `version.rc.in`) and vcpkg overlay ports |
| `CMakeLists.txt`, `CMakePresets.json` | Build configuration |
| `vcpkg.json`, `vcpkg-configuration.json` | Dependency manifest |
| `gen.ps1` | Configure helper |

## License

MIT (see `vcpkg.json`).
