# Build System and Packaging

This document describes how CMake configures, builds, and packages ANAFINEN, and how each dependency is detected.

> **Document status**
> Verified against: `v0.3.0-alpha` (in development; latest release `v0.2.0-alpha`, 2026-10-05), content checked 2026-10-08 (version 0.3.0; `anaf_bridge` library, `anafinen_cli` target and `ANAFINEN_BUILD_CLI` option, sections 1-3, 7, 8; 2026-10-05: v0.2.0-alpha release check: package descriptions and `.desktop` comment, section 8; Debian GCC 14 `-Wmaybe-uninitialized` fixed, release packages built in the containers, section 8.1.1; 2026-10-04: `anaf_beam_library_tool`, beam library sources in `anaf_core`; section catalogue synced like the material library; beam sources and `objectCalcs/common/` in `anaf_core`, `anaf_beam_tests`; solver sources in `src/solvers/`; `check.sh` turns tests on and fails when none run; HDF5 added, MinGW cross-build removed; new logo with a small-size variant and 16/24/32 px icons; SVG MIME sniffing fix, RPM no longer owns shared icon directories).

## 1. Overall flow

```text
CMakeLists.txt
   |
   +-- project(anafinen VERSION 0.3.0), C++23, compile_commands.json
   |
   +-- include(CompilerOptions)  -> project_warnings_and_optimizations (INTERFACE)
   +-- include(Dependencies)     -> Eigen, OpenMP, OpenGL, PNG, HDF5, CHOLMOD?, Gmsh, Spectra, glm
   +-- include(ExternalGui)      -> glad_local, GLFW target, imgui_suite
   |
   +-- add_library(anaf_io STATIC ...)     mesh I/O + HDF5 array store (Gmsh, zlib, HDF5 PRIVATE)
   +-- add_library(anaf_core STATIC ...)   FEM + truss adapter (links anaf_io)
   +-- add_library(anaf_bridge STATIC ...) front-end state shared by GUI and CLI (links anaf_core)
   +-- add_executable(anafinen ...)        GUI + main (+ portable-file-dialogs), links anaf_bridge
   +-- src/cli/ (ANAFINEN_BUILD_CLI=ON)    anafinen_cli -> anafinen-cli, next to anafinen, links anaf_bridge
   +-- tests/ (ANAFINEN_BUILD_TESTS=ON)    anaf_core_tests, anaf_beam_tests (anaf_core only), anaf_io_tests, anaf_array_tests, anaf_io_tool,
   |                                       anaf_truss_io_tests, vtk_reference_check,
   |                                       anaf_truss_library_tool (regenerates assets/objects/truss/truss1D)
   |                                       anaf_beam_library_tool  (regenerates assets/objects/beam/beam3D)
   |        |
   |        +-- POST_BUILD: copy assets/ (+ generated icon, + gmsh DLL on Windows)
   |
   +-- install(...) rules
   +-- include(Packaging)         -> CPack: RPM/TGZ (Linux), ZIP (Windows)
```

## 2. Top-level settings (`CMakeLists.txt`)

| Setting | Value | Why |
|---|---|---|
| `cmake_minimum_required` | `3.20...3.31` | Policy range; `CMP0207` set to NEW when available |
| `CMAKE_CXX_STANDARD` | 23, required, extensions off | `std::format`, `std::jthread`, `std::span`, `starts_with` |
| `CMAKE_EXPORT_COMPILE_COMMANDS` | ON | `.clangd` reads `build/compile_commands.json` |
| `CMAKE_INTERPROCEDURAL_OPTIMIZATION` | FALSE | LTO disabled explicitly |
| `ANAFINEN_NATIVE_OPTIMIZATIONS` | option, OFF | Adds `/arch:AVX2` (MSVC) or `-march=native` (GCC/Clang). Local builds only: the binary then needs the build machine's CPU. Packages are protected twice: `package.sh` and `PKGBUILD` pass `-DANAFINEN_NATIVE_OPTIMIZATIONS=OFF` (so a value cached in `build/` cannot leak in), and when the option is ON, `Packaging.cmake` adds a `CPACK_PRE_BUILD_SCRIPTS` guard that makes every `cpack` run (RPM, DEB, TGZ, Windows ZIP) fail with an explanation. (Before 0.1.3 it passed `-march=x86-64`, the baseline, which had no effect.) |
| `ANAFINEN_WARNINGS_AS_ERRORS` | option, OFF (`cmake/CompilerOptions.cmake`) | Adds `-Werror` / `/WX`. For CI and pre-commit checks; the tree builds warning-free with it. |
| `ANAFINEN_BUILD_TESTS` | option, OFF | Builds the `tests/` directory and enables `ctest` |
| `ANAFINEN_BUILD_CLI` | option, ON | Adds `src/cli/` (target `anafinen_cli`, binary `anafinen-cli`) |

## 3. Targets

| Target | Type | Contents | Links |
|---|---|---|---|
| `project_warnings_and_optimizations` | INTERFACE | Release flags and defines | - |
| `anaf_io` | STATIC | `src/io/*` (model, formats, service, `array/` HDF5 store) | Gmsh, ZLIB, HDF5 (all PRIVATE). The Eigen and CHOLMOD adapters in `src/io/array/` are header-only, so `anaf_io` itself links neither. |
| `anaf_core` | STATIC | `src/objectCalcs/truss_1D/*` (incl. `trussIO/trussMeshAdapter.cpp`), `src/objectCalcs/beam/*` (beam solver), `src/objectCalcs/common/*` (support bases), `src/solvers/*` (solver portfolio), `src/material/materialLibrary.cpp`, `src/log/anaf_info.cpp`, `src/directory/getExecutableDirectory.cpp`, `src/platform/systemInfo.cpp`. Self-contained: a front end links it and calls `solveStatic()` (no bridge or GUI code). `MAIN_DIR` (PRIVATE) for the source-tree asset fallback. | anaf_io, Eigen3, Spectra, OpenMP, CHOLMOD (optional, PRIVATE), nlohmann_json (PRIVATE); Windows: shell32, ole32, uuid (user config folder), dxgi, advapi32 (`systemInfo`: VRAM, CPU name from the registry) |
| `glad_local` | STATIC | `external/glad/src/gl.c` | - |
| `imgui_suite` | STATIC | ImGui core + GLFW/OpenGL3 backends + ImGuizmo + ImPlot | glad, GLFW, OpenGL |
| `anaf_bridge` | STATIC | `src/bridge/generalStatus.cpp` (`Gui_Calc_Bridge`): front-end state shared by the GUI, the CLI and `anaf_truss_io_tests`. No GUI dependency. | `anaf_core` (PUBLIC) |
| `anafinen` | EXECUTABLE | `main.cpp`, GUI, `platform/resourceMonitor.cpp` | `anaf_bridge`, `imgui_suite`, glad, GLFW, OpenGL, glm, PNG; Windows: ole32, comdlg32, shell32, uuid (file dialogs), psapi (resource monitor) |

| `anafinen_cli` | EXECUTABLE (`src/cli/CMakeLists.txt`) | `cli.cpp`, `main_cli.cpp`. `OUTPUT_NAME` `anafinen-cli`, `RUNTIME_OUTPUT_DIRECTORY` = the `anafinen` target's binary directory (multi-config generators add `<Config>/` to both), so it shares the copied `assets/`. Defines `ANAFINEN_VERSION`. Windows: copies the Gmsh DLL next to itself. | `anaf_bridge` (OpenMP, Eigen and include directories come through it) |

Compile definitions on `anafinen` (there is no `ANAF_GUI` / `ANAF_CLI` macro: the log picks its sinks at run time, `anaf::LOG::setCallback()` / `setConsoleOutput()`):
- `GLFW_INCLUDE_NONE`: GLAD provides the GL headers.
- `MAIN_DIR="<source dir>"`: used as an asset search fallback in development builds.

**Adding a source file:** append it to `ANAF_IO_SOURCES` (file formats), `ANAF_CORE_SOURCES` (FEM, adapters), `ANAF_BRIDGE_SOURCES` (front-end state) or `ANAFINEN_SOURCES` (GUI) in `CMakeLists.txt`, or to `ANAFINEN_CLI_SOURCES` in `src/cli/CMakeLists.txt`. There is no globbing.

**Front ends and macros:** shared libraries never change their types with `ANAF_GUI` or similar macros. A static library is compiled once and linked into several executables (GUI and CLI), and a type that differs between them would violate the ODR. GUI-only code lives in `anafinen` sources, CLI-only code in `src/cli/`; both executables link `anaf_bridge` (and through it `anaf_core` + `anaf_io`). The GUI's `main()` installs the console callback (`setCallback`), the CLI's turns on stdout (`setConsoleOutput(true)`).

## 4. Compiler options (`cmake/CompilerOptions.cmake`)

| Compiler | Release flags | Extra |
|---|---|---|
| GCC / Clang | `-O3 -ffast-math -fno-finite-math-only` | Linker: `mold` if found, else `lld` (Linux only) |
| MSVC | `/O2`, `/utf-8` (sources and literals are UTF-8) | `NOMINMAX`, `_CRT_SECURE_NO_WARNINGS`, `WIN32_LEAN_AND_MEAN` |

Warnings (all configurations, through `project_warnings_and_optimizations`, so only first-party targets: `anaf_io`, `anaf_core`, `anaf_bridge`, `anafinen`, `anafinen_cli`, tests):

| Compiler | Flags | Notes |
|---|---|---|
| GCC / Clang | `-Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion` | Clang's `-Wconversion` includes `-Wsign-conversion` (GCC's does not in C++); it is turned off because signed OpenMP indices and `int` / `size_t` mixing make it noise. |
| MSVC | `/W4 /permissive- /external:anglebrackets /external:W0` | `<...>` includes count as external and are not checked. |

Third-party include directories are marked `SYSTEM` (`imgui_suite`, `glad_local`, `spectra_local`, portable-file-dialogs; imported targets such as Eigen are SYSTEM by default), so their headers do not trigger these flags. Status on 2026-10-04: zero warnings with GCC 16 and Clang; MSVC `/W4` has not been run yet. (The MinGW cross-build was removed on 2026-10-04: Windows is built and tested natively with MSVC + vcpkg.)
| All | `NDEBUG`, `EIGEN_NO_DEBUG` in Release | `ccache` as compiler launcher if found |

`-ffast-math` allows reassociation, so floating-point results can differ slightly between Debug and Release. `-fno-finite-math-only` keeps `std::isfinite` checks working, which the solver referee relies on.

## 5. Dependency detection (`cmake/Dependencies.cmake`)

| Dependency | Method | Required | Notes |
|---|---|---|---|
| Eigen3 | `find_package(Eigen3 CONFIG REQUIRED)` | yes | - |
| OpenMP | `find_package(OpenMP REQUIRED)` | yes | MSVC uses `/openmp` (OpenMP 2.0, `vcomp140.dll`) instead of the imported target |
| OpenGL, ZLIB, PNG | `find_package(... REQUIRED)` | yes | Windows fallback creates `OpenGL::GL` -> `opengl32` |
| HDF5 (C API) | `find_package(HDF5 REQUIRED COMPONENTS C)`, `HDF5_PREFER_PARALLEL OFF` | yes | See section 5.3. Target in `ANAFINEN_HDF5_TARGET` (`HDF5::HDF5`, or the `anafinen_hdf5` INTERFACE fallback built from `HDF5_C_*` variables). An MPI build stops the configure. |
| SuiteSparse CHOLMOD | see section 5.1 | no | Sets `ANAFINEN_HAS_CHOLMOD` |
| Gmsh SDK | `find_path` / `find_library` | yes | Windows: `GMSH_SDK_DIR`; when empty or without `include/gmsh.h` it is auto-detected (section 5.2), also resolves `GMSH_DLL` |
| Spectra | submodule `external/spectra`, else `find_package(Spectra)` | no | Header-only; imported as `spectra_local` |
| glm | `find_package(glm CONFIG)`, else header search | yes | - |
| nlohmann/json | `find_package(nlohmann_json 3.11 CONFIG)`, else `FetchContent` of the v3.12.0 release tarball (SHA-256 pinned) | yes | Header-only, linked PRIVATE into `anaf_core` for the material library. |
| librsvg / ImageMagick | `find_program(rsvg-convert)`, else `find_program(magick convert)` (only `magick` on Windows, where `convert` is `System32\convert.exe`) | no | Renders the icons at configure time (`anafinen_render_icon()`): `assets/icons/anafinen.svg` (drawn for 48 px and up) to 128x128 (`ANAFINEN_ICON_PNG`), and `assets/icons/anafinen-small.svg` (16-32 px variant with heavier strokes, so the A's counter stays open) to 32x32 (`ANAFINEN_ICON_PNG_32`) and 16 / 24 px (`ANAFINEN_ICON_PNG_SMALL_SIZES`, hicolor only). `rsvg-convert` is preferred: Debian's ImageMagick has no rsvg delegate, and its internal MSVG renderer draws only the background while still exiting with 0. Without a converter the committed renders `assets/icons/anafinen.png` / `anafinen-32.png` are used and the 16 / 24 px hicolor icons are skipped. On Windows `src/anafinen.rc` also embeds `assets/icons/anafinen.ico` (16/24/32 from the small SVG, 48-256 from the large one, PNG entries) into the `.exe`. After changing either SVG, run `package/tools/render-icons.py` (rsvg-convert or inkscape) to regenerate the committed `.ico` and PNGs. (Before 0.1.3 the SVG was copied under the `.png` name, which shipped a broken hicolor icon.) Both SVGs start the `<svg>` element right after the XML declaration and keep their comments inside it: shared-mime-info looks for `<svg` only in the first 256 bytes, and a file that misses it is typed `application/xml`, which GNOME Shell cannot load as an icon (blank app grid entry). |
| portable-file-dialogs | vendored header `external/portable-file-dialogs/` (commit `c12ea8c`, WTFPL) | yes | Native file chooser. Linux runtime needs `zenity`, `kdialog`, `matedialog` or `qarma` |
| Python 3 + `vtk` module | `find_package(Python3)` + `import vtk` probe | no | Enables the `vtk_reference_check` test |

### 5.1 CHOLMOD detection

```text
find_package(SuiteSparse CONFIG QUIET)
      |
      +-- TARGET SuiteSparse::CHOLMOD exists? --yes--> CHOLMOD_LIBRARIES = SuiteSparse::CHOLMOD
      |
      no
      v
find_path(cholmod.h, suffix suitesparse) + find_library(cholmod, suitesparseconfig)
      |      (the vcpkg toolchain puts installed/<triplet> on CMAKE_PREFIX_PATH)
      +-- found --> CHOLMOD_LIBRARIES = <cholmod>;<suitesparseconfig>
      +-- not found --> ANAFINEN_HAS_CHOLMOD OFF (Eigen solvers only)
```

`find_package(CHOLMOD CONFIG)` is not used on purpose. Fedora's `suitesparse-devel` ships `CAMDConfig.cmake` / `CCOLAMDConfig.cmake`, which include `*Targets_static.cmake` files that are not packaged. That is a hard configure error, even with `QUIET`.

When `ANAFINEN_HAS_CHOLMOD` is ON, `anaf_core` gets the `ANAFINEN_HAS_CHOLMOD` define and links CHOLMOD privately. `src/solvers/direct/solver_cholmod.cpp` compiles to a stub otherwise.

Test without CHOLMOD on a machine that has it:
```bash
cmake -S . -B build-nocholmod -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_IGNORE_PATH=/usr/include/suitesparse
```

### 5.2 Windows SDK locations (`cmake/LocateWindowsSdks.cmake`)

No drive or directory is hard-coded; every developer can keep vcpkg and the Gmsh SDK anywhere.
The module is included before `project()` and scans `%USERPROFILE%` and every existing drive `C:` .. `Z:`.

| What | Used when | Order (first valid wins) | Valid when |
|---|---|---|---|
| `CMAKE_TOOLCHAIN_FILE` (vcpkg) | not set, host is Windows, not an MSYS2 shell, `ANAFINEN_AUTO_VCPKG=ON` | `VCPKG_ROOT` env, `vcpkg.exe` on `PATH`, `<root>/{vcpkg,dev/vcpkg,tools/vcpkg,src/vcpkg,libs/vcpkg}` | `scripts/buildsystems/vcpkg.cmake` and `installed/` exist (skips the empty vcpkg bundled with Visual Studio) |
| `GMSH_SDK_DIR` | empty or without `include/gmsh.h` | `GMSH_SDK_DIR` env, `external/gmsh-sdk`, `<root>/{libs/gmsh-sdk,libs/gmsh*sdk*,gmsh*sdk*,dev/gmsh*sdk*,tools/gmsh*sdk*,sdk/gmsh*sdk*}` | `include/gmsh.h` exists |

The result is cached and printed (`vcpkg toolchain auto-detected: ...`, `Gmsh SDK auto-detected: ...`);
other Gmsh SDKs found are listed too. An explicit `-D` value, a VS Code `cmake.configureSettings` entry
or the environment variable always takes precedence. Delete the cache entry (or the build directory)
to re-run the detection.

### 5.3 HDF5 (prebuilt only)

HDF5 is never compiled as part of this project; the build only links a library that is already installed.

| Platform | Package | How CMake finds it |
|---|---|---|
| Fedora | `hdf5-devel` (runtime `hdf5`) | No CMake config is packaged: `FindHDF5` runs `h5cc -show` |
| Debian / Ubuntu | `libhdf5-dev` (runtime `libhdf5-310` etc. through `dpkg-shlibdeps`) | `h5cc` (serial flavour under `/usr/include/hdf5/serial`) |
| Arch / CachyOS | `hdf5` | `h5cc` / CMake config |
| Windows | vcpkg `hdf5:x64-windows` (default features include zlib) | `hdf5-config.cmake` of the vcpkg port; DLLs reach the ZIP through `RUNTIME_DEPENDENCIES` |

vcpkg builds the port once on the developer machine (classic mode, `installed/x64-windows`); after that only linking happens, like every other vcpkg dependency. Fedora's Gmsh links the same `libhdf5.so.310`, so only one HDF5 is loaded in the process.

Only the C API is used (`src/io/array/arrayFile.cpp`, `src/io/detail/h5Handle.hpp`): its ABI does not depend on the C++ compiler, and the separately packaged C++ API (`H5Cpp`) is not needed. Distribution builds are not thread-safe (`h5cc -showconfig`: `Threadsafety: no`), so `anaf_io` serializes every HDF5 call through one mutex.

## 6. GUI dependencies (`cmake/ExternalGui.cmake`)

| Library | Source order |
|---|---|
| GLAD | Always vendored: `external/glad` (GL 4.6 core loader) |
| GLFW | `external/glfw` subdirectory → `find_package(glfw3)` → pkg-config → `FetchContent` GLFW 3.4 |
| ImGui, ImGuizmo, ImPlot | Submodules in `external/` compiled into `imgui_suite`. If `external/imgui` is missing, falls back to vcpkg `find_package(imgui/implot/imguizmo)` |

The ImGui submodule tracks the `docking` branch (`.gitmodules`). Docking APIs (`DockBuilder*`) are required by `MainDockSpaceHost`.

## 7. Build artifacts and assets

- `POST_BUILD` copies `assets/` next to the executable. It also copies the two window icons (`anafinen.png`, `anafinen-32.png`; generated or committed) and, on Windows, the Gmsh DLL.
- Runtime asset lookup order (fonts, icon, material library), all through `anaf::DIRECTORY::findAssetPath()`:
  1. `<exe dir>/assets`
  2. `/usr/share/anafinen/assets`
  3. `./assets`
  4. `MAIN_DIR/assets`
- `assets/bridge/materialProperties.json` is the built-in material library and `assets/bridge/sectionCatalog.json` the beam section catalogue. The `POST_BUILD` copy runs only when `anafinen` relinks, so the custom target `anafinen_material_library` (ALL) copies these two files with `copy_if_different` on every build; editing them needs no relink.
- `anafinen-cli` is built into the same directory as `anafinen` and has no `POST_BUILD` asset copy of its own: it finds `<exe dir>/assets` from the GUI's copy. Built alone (`--target anafinen_cli`) in a fresh tree, it falls through to an installed package or `MAIN_DIR/assets`.
- `anafinen_run.log` is written to the current working directory (GUI and CLI alike).

## 8. Install and packaging (`cmake/Packaging.cmake`)

| Platform | Install layout | CPack generator | Package name |
|---|---|---|---|
| Linux | `bin/anafinen`, `share/anafinen/assets` (without the `.desktop` file), `share/applications/anafinen.desktop`, hicolor icons (scalable SVG, 128 px PNG, and 16 / 24 / 32 px PNGs from `anafinen-small.svg`) | `RPM;TGZ` (DEB through `package.sh`) | `anafinen-<ver>-alpha`, RPM release `1.alpha` |
| Windows | Flat: `anafinen.exe`, `assets/`, Gmsh DLL, vcpkg runtime DLLs via `RUNTIME_DEPENDENCIES`, app-local MSVC runtime (`InstallRequiredSystemLibraries`, including `vcomp140.dll` for `/openmp`, so no Visual C++ Redistributable is needed; added after the 0.1.3 release) | `ZIP` | `anafinen-<ver>-windows-<arch>-alpha` |

Package descriptions: `CPACK_PACKAGE_DESCRIPTION_SUMMARY` ("3D FEM Analysis Engine", also `pkgdesc` in `PKGBUILD`) and a long `CPACK_PACKAGE_DESCRIPTION` that also states that dynamic analysis is not available yet. DEB reads it on its own; RPM only reads `CPACK_RPM_PACKAGE_DESCRIPTION` (or `CPACK_PACKAGE_DESCRIPTION_FILE`), so that is set to the same text (before 0.2.0 the RPM carried CPack's generic "This is an installer created using CPack" template). The `.desktop` `Comment` says "3D finite element analysis of trusses and beam frames (linear static)" (it claimed dynamic and modal analysis before 0.2.0).

`anafinen-cli` is built in package builds too (`ANAFINEN_BUILD_CLI` is ON) but not installed: no package contains it yet.

Linux RPM: `CPACK_RPM_PACKAGE_AUTOREQPROV ON`, plus an explicit `Requires: hdf5` (and `suitesparse` when CHOLMOD is enabled).

### 8.1 Packaging scripts

| Script | Platform | What it does |
|---|---|---|
| `package/package.sh` | Fedora | Installs build deps with `dnf`, Release build, `cpack -G RPM` |
| `package/package.sh` | Arch / CachyOS | Stops with a hint when `gmsh` is not installed (AUR only: `gmsh` or `gmsh-bin`), then `makepkg --syncdeps --noconfirm --force` in `package/` using `package/PKGBUILD` |
| `package/package.sh` | Debian / Ubuntu | Installs deps with `apt`, Release build, `cpack -G DEB` |
| `package/package-windows.ps1` | Windows | Loads the MSVC x64 environment, Release build in `build/package-release`, `cpack -G ZIP` (also run by the VS Code task; see `package/PACKAGE_BUILD.md`) |

`package.sh` changes to the repository root first, so it works from any working directory. Tested in containers on 2026-09-28: Debian 13 (GCC 14) and Arch (GCC 16, `gmsh-bin` from the AUR) both build, package and install.

### 8.1.1 Container checks (Debian, Arch)

`package/tools/container-check.sh` repeats the build, test and packaging steps in clean Debian and Arch containers, from any host with podman (preferred) or docker. Nothing is mounted from the repository.

```
host: git ls-files (tracked + untracked, not ignored; submodules included)
  │  tar on stdin
  ▼
container anafinen-build:<distro>  (image from package/tools/containers/<distro>.Dockerfile)
  │  package/tools/containers/inside.sh <distro> <mode>, as root → build user "builder"
  │  test:    package/tools/check.sh gcc
  │  package: package/package.sh → install the package → ldd, icon, .desktop check
  │  tar of /out on stdout
  ▼
host: build-containers/<distro>/  (check-*.log or package.log + .deb / .pkg.tar.zst)
```

| What | Where |
|---|---|
| Image definitions | `package/tools/containers/debian.Dockerfile` (package list of the Debian branch of `package.sh`), `package/tools/containers/arch.Dockerfile` (`PKGBUILD` depends / makedepends + `gmsh-bin` from the AUR) |
| Images | `anafinen-build:debian`, `anafinen-build:arch`; built on first use, `--rebuild` pulls the base image again |
| ccache | named volumes `anafinen-ccache-debian`, `anafinen-ccache-arch` (`/ccache`, set in `/etc/ccache.conf`) |
| Output | `build-containers/<distro>/` (git-ignored); image build logs in `build-containers/<distro>-image.log` |

1. The working tree is sent as it is, uncommitted changes included; build directories and `.git` are left out.
2. Distros run one after the other. Source and results go through stdin / stdout, so there is no SELinux relabel (`:Z`) of the repository and no file owned by another user on the host.
3. `shell` mode opens an interactive shell in `/work` as `builder`; nothing is copied back.
4. Expected on Debian 13: `anaf_io_tests` aborts in `highOrderNodeOrderingMatchesGmshVtkWriter` (the log may end at an earlier `[ RUN ]` line: stdout is buffered, the abort is not) (the `libgmsh4.13` Eigen assertion, `ARCHITECTURE.md` section 8, item 4), so `test` reports a failure there. Every other test passes on Debian (2026-10-05, `v0.2.0-alpha`).

### 8.2 Dependencies per platform (2026-09-28)

| Platform | Build packages added for `anaf_io` | Runtime extra |
|---|---|---|
| Fedora | `zlib-devel`, `librsvg2-tools` (`rsvg-convert`, preferred icon renderer), `ImageMagick` (`package.sh`, README) | RPM `Suggests: zenity`. The RPM does not own the shared `/usr/share/applications` and `/usr/share/icons/hicolor/...` directories (`CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION`); owning them reset their mtimes to the package timestamp |
| Arch / CachyOS | `depends`: `glibc gcc-libs glfw libglvnd gmsh suitesparse libpng zlib hdf5`. `makedepends`: `cmake ninja git eigen glm nlohmann-json librsvg`. Header-only libraries are build-time only; Spectra comes from the submodule; OpenMP is GCC's `libgomp` in `gcc-libs` (the `openmp` package is LLVM's). `gmsh` is only in the AUR. | `optdepends`: `zenity` or `kdialog` |
| Debian / Ubuntu | `zlib1g-dev`, `librsvg2-bin` (`package.sh`, README) | DEB `Recommends: zenity \| kdialog`, `Section: science`; `CPACK_PACKAGE_CONTACT` is set (the DEB generator requires a maintainer) |
| All (2026-10-04) | HDF5: Fedora `hdf5-devel`, Arch `hdf5` (`depends`), Debian `libhdf5-dev`, vcpkg `hdf5` | the shared HDF5 library (RPM / DEB find it automatically, Arch `depends`, Windows ZIP ships `hdf5.dll`) |
| All (0.1.3) | `nlohmann/json`: Fedora `json-devel`, Arch `nlohmann-json` (`makedepends`), Debian `nlohmann-json3-dev`, vcpkg `nlohmann-json` | none (header-only) |
| Windows (vcpkg) | `libpng`, `glm` added to the README install list (zlib comes with libpng) | none: native dialogs are part of Windows; `ole32`, `comdlg32`, `shell32`, `uuid`, `psapi` (resource usage) on `anafinen`; `dxgi`, `advapi32` (VRAM, CPU name from the registry) on `anaf_core` |

Without zenity / kdialog on Linux, the application works; only File > Import / Export shows "no native file dialog available".

### 8.3 Licensing in packages

- `LICENSE` and `THIRD_PARTY_LICENSES.md` are installed to `/usr/share/doc/anafinen/` (RPM, DEB, Arch) and next to `anafinen.exe` (Windows ZIP). `CPACK_RESOURCE_FILE_LICENSE` points to `LICENSE`.
- The Windows ZIP redistributes GPL libraries (Gmsh, parts of SuiteSparse). `THIRD_PARTY_LICENSES.md` lists their source locations and a written source offer (GPLv3 section 6).
- The install layout is defined only in `cmake/Packaging.cmake`. The former platform-independent `install()` lines in `CMakeLists.txt` put a second executable in `/usr/anafinen` and assets in `/usr/assets`; they were removed.
- `ANAFINEN_VERSION` (`<version>-alpha`) is a compile definition of `anafinen`, shown in Help > About.

## 9. Common commands

```bash
# Build + test with a short summary (warnings, errors, test results); full logs in <dir>/check-*.log.
# Tests are always turned on (an existing tree is reconfigured with ANAFINEN_BUILD_TESTS=ON);
# a tree in which ctest finds no tests is reported as a failure, not as "ALL OK".
package/tools/check.sh            # Linux GCC in build/
package/tools/check.sh all        # + Clang (build-clang/)

# Configure + build (Linux)
git submodule update --init --recursive
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# With tests
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DANAFINEN_BUILD_TESTS=ON
cmake --build build && (cd build && ctest --output-on-failure)

# Package on the current distro
./package/package.sh

# Clean Debian / Arch containers (podman or docker), results in build-containers/<distro>/
package/tools/container-check.sh                  # both distros: build + tests
package/tools/container-check.sh arch package     # package, install, ldd / icon check
package/tools/container-check.sh debian shell     # interactive shell with the working tree in /work
package/tools/container-check.sh --rebuild all    # rebuild the images (distro updates)

# After editing assets/icons/anafinen.svg or anafinen-small.svg: regenerate the committed .ico / PNGs
package/tools/render-icons.py
```

## 10. Related files

- [CMakeLists.txt](../CMakeLists.txt)
- [cmake/CompilerOptions.cmake](../cmake/CompilerOptions.cmake)
- [cmake/Dependencies.cmake](../cmake/Dependencies.cmake)
- [cmake/ExternalGui.cmake](../cmake/ExternalGui.cmake)
- [cmake/Packaging.cmake](../cmake/Packaging.cmake)
- [package/package.sh](../package/package.sh), [package/PKGBUILD](../package/PKGBUILD), [package/PACKAGE_BUILD.md](../package/PACKAGE_BUILD.md)
- [package/tools/container-check.sh](../package/tools/container-check.sh), [package/tools/containers/](../package/tools/containers/)
- [package/tools/render-icons.py](../package/tools/render-icons.py), [assets/icons/](../assets/icons/), [src/anafinen.rc](../src/anafinen.rc)
- [.gitmodules](../.gitmodules)
- [tests/CMakeLists.txt](../tests/CMakeLists.txt)
- [src/cli/CMakeLists.txt](../src/cli/CMakeLists.txt)
