# Build System and Packaging

This document describes how CMake configures, builds, and packages ANAFINEN, and how each dependency is detected.

> **Document status**
> Verified against: `v0.1.2-alpha` + working tree, 2026-09-28.

## 1. Overall flow

```text
CMakeLists.txt
   |
   +-- project(anafinen VERSION 0.1.2), C++23, compile_commands.json
   |
   +-- include(CompilerOptions)  -> project_warnings_and_optimizations (INTERFACE)
   +-- include(Dependencies)     -> Eigen, OpenMP, OpenGL, PNG, CHOLMOD?, Gmsh, Spectra, glm
   +-- include(ExternalGui)      -> glad_local, GLFW target, imgui_suite
   |
   +-- add_library(anaf_io STATIC ...)     mesh I/O (Gmsh, zlib PRIVATE)
   +-- add_library(anaf_core STATIC ...)   FEM + truss adapter (links anaf_io)
   +-- add_executable(anafinen ...)        GUI + bridge + log + main (+ portable-file-dialogs)
   +-- tests/ (ANAFINEN_BUILD_TESTS=ON)    anaf_io_tests, anaf_io_tool, anaf_truss_io_tests, vtk_reference_check
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
| `ANAFINEN_NATIVE_OPTIMIZATIONS` | option, OFF | Adds `/arch:AVX2` (MSVC) or `-march=x86-64` (GCC/Clang) |
| `ANAFINEN_BUILD_TESTS` | option, OFF | Builds the `tests/` directory and enables `ctest` |

## 3. Targets

| Target | Type | Contents | Links |
|---|---|---|---|
| `project_warnings_and_optimizations` | INTERFACE | Release flags and defines | - |
| `anaf_io` | STATIC | `src/io/*` (model, formats, service) | Gmsh, ZLIB (both PRIVATE) |
| `anaf_core` | STATIC | `src/objectCalcs/truss_1D/*` (incl. `trussIO/trussMeshAdapter.cpp`) | anaf_io, Eigen3, Spectra, OpenMP, CHOLMOD (optional, PRIVATE) |
| `glad_local` | STATIC | `external/glad/src/gl.c` | - |
| `imgui_suite` | STATIC | ImGui core + GLFW/OpenGL3 backends + ImGuizmo + ImPlot | glad, GLFW, OpenGL |
| `anafinen` | EXECUTABLE | `main.cpp`, bridge, log, directory, GUI, test | `anaf_core`, `imgui_suite`, glad, GLFW, OpenGL, glm, PNG; Windows: ole32, comdlg32, shell32, uuid (file dialogs) |

Compile definitions on `anafinen`:
- `ANAF_GUI`
- `GLFW_INCLUDE_NONE`: GLAD provides the GL headers.
- `MAIN_DIR="<source dir>"`: used as an asset search fallback in development builds.

**Adding a source file:** append it to `ANAF_IO_SOURCES` (file formats), `ANAF_CORE_SOURCES` (FEM, adapters) or `ANAFINEN_SOURCES` (GUI) in `CMakeLists.txt`. There is no globbing.

**Front ends and macros:** shared libraries never change their types with `ANAF_GUI` or similar macros. A static library is compiled once and linked into several executables (GUI now, CLI later), and a type that differs between them would violate the ODR. GUI-only code lives in `anafinen` sources; a CLI will be a separate executable linking `anaf_core` + `anaf_io`.

## 4. Compiler options (`cmake/CompilerOptions.cmake`)

| Compiler | Release flags | Extra |
|---|---|---|
| GCC / Clang | `-O3 -ffast-math -fno-finite-math-only` | Linker: `mold` if found, else `lld` (Linux only) |
| MSVC | `/O2` | `NOMINMAX`, `_CRT_SECURE_NO_WARNINGS`, `WIN32_LEAN_AND_MEAN` |
| All | `NDEBUG`, `EIGEN_NO_DEBUG` in Release | `ccache` as compiler launcher if found |

`-ffast-math` allows reassociation, so floating-point results can differ slightly between Debug and Release. `-fno-finite-math-only` keeps `std::isfinite` checks working, which the solver referee relies on.

## 5. Dependency detection (`cmake/Dependencies.cmake`)

| Dependency | Method | Required | Notes |
|---|---|---|---|
| Eigen3 | `find_package(Eigen3 CONFIG REQUIRED)` | yes | - |
| OpenMP | `find_package(OpenMP REQUIRED)` | yes | MSVC uses `/openmp:llvm` instead of the imported target |
| OpenGL, ZLIB, PNG | `find_package(... REQUIRED)` | yes | Windows fallback creates `OpenGL::GL` -> `opengl32` |
| SuiteSparse CHOLMOD | see section 5.1 | no | Sets `ANAFINEN_HAS_CHOLMOD` |
| Gmsh SDK | `find_path` / `find_library` | yes | Windows: `GMSH_SDK_DIR` (default `C:/libs/gmsh-sdk`), also resolves `GMSH_DLL` |
| Spectra | submodule `external/spectra`, else `find_package(Spectra)` | no | Header-only; imported as `spectra_local` |
| glm | `find_package(glm CONFIG)`, else header search | yes | - |
| ImageMagick | `find_program(magick convert)` | no | Converts `assets/icons/anafinen.svg` to a 128x128 PNG at configure time |
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
      |      (also searches C:/vcpkg/installed/x64-windows)
      +-- found --> CHOLMOD_LIBRARIES = <cholmod>;<suitesparseconfig>
      +-- not found --> ANAFINEN_HAS_CHOLMOD OFF (Eigen solvers only)
```

`find_package(CHOLMOD CONFIG)` is not used on purpose. Fedora's `suitesparse-devel` ships `CAMDConfig.cmake` / `CCOLAMDConfig.cmake`, which include `*Targets_static.cmake` files that are not packaged. That is a hard configure error, even with `QUIET`.

When `ANAFINEN_HAS_CHOLMOD` is ON, `anaf_core` gets the `ANAFINEN_HAS_CHOLMOD` define and links CHOLMOD privately. `solver_cholmod.cpp` compiles to a stub otherwise.

Test without CHOLMOD on a machine that has it:
```bash
cmake -S . -B build-nocholmod -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_IGNORE_PATH=/usr/include/suitesparse
```

## 6. GUI dependencies (`cmake/ExternalGui.cmake`)

| Library | Source order |
|---|---|
| GLAD | Always vendored: `external/glad` (GL 4.6 core loader) |
| GLFW | `external/glfw` subdirectory → MinGW sysroot (cross build) → `find_package(glfw3)` → pkg-config → `FetchContent` GLFW 3.4 |
| ImGui, ImGuizmo, ImPlot | Submodules in `external/` compiled into `imgui_suite`. If `external/imgui` is missing, falls back to vcpkg `find_package(imgui/implot/imguizmo)` |

The ImGui submodule tracks the `docking` branch (`.gitmodules`). Docking APIs (`DockBuilder*`) are required by `MainDockSpaceHost`.

## 7. Build artifacts and assets

- `POST_BUILD` copies `assets/` next to the executable. It also copies the generated PNG icon and, on Windows, the Gmsh DLL.
- Runtime asset lookup order (fonts, icon):
  1. `<exe dir>/assets`
  2. `./assets`
  3. `MAIN_DIR/assets`
  4. `/usr/share/anafinen/assets`
- `anafinen_run.log` is written to the current working directory.

## 8. Install and packaging (`cmake/Packaging.cmake`)

| Platform | Install layout | CPack generator | Package name |
|---|---|---|---|
| Linux | `bin/anafinen`, `share/anafinen/assets`, `.desktop`, hicolor icons (SVG + 128px PNG) | `RPM;TGZ` (DEB through `package.sh`) | `anafinen-<ver>-alpha`, RPM release `1.alpha` |
| Windows | Flat: `anafinen.exe`, `assets/`, Gmsh DLL, vcpkg runtime DLLs via `RUNTIME_DEPENDENCIES` | `ZIP` | `anafinen-<ver>-windows-<arch>-alpha` |
| Windows (MinGW cross) | Same + MinGW runtime DLLs from the Fedora sysroot | `ZIP` | same |

Linux RPM: `CPACK_RPM_PACKAGE_AUTOREQPROV ON`, plus an explicit `Requires: suitesparse` when CHOLMOD is enabled.

### 8.1 Packaging scripts

| Script | Platform | What it does |
|---|---|---|
| `package/package.sh` | Fedora | Installs build deps with `dnf`, Release build, `cpack -G RPM` |
| `package/package.sh` | Arch / CachyOS | `makepkg -f` using `package/PKGBUILD` |
| `package/package.sh` | Debian / Ubuntu | Installs deps with `apt`, Release build, `cpack -G DEB` |
| VS Code task | Windows | `cpack -G ZIP` from the configured build dir (see `package/PACKAGE_BUILD.md`) |

Winget ID `suacayipkisi.anafinen` is waiting for moderator approval.

### 8.2 Dependencies per platform (2026-09-28)

| Platform | Build packages added for `anaf_io` | Runtime extra |
|---|---|---|
| Fedora | `zlib-devel` (`package.sh`, README) | RPM `Suggests: zenity` |
| Arch / CachyOS | `libpng`, `zlib` in `depends` / `makedepends`, `glm` in `makedepends` (`PKGBUILD`) | `optdepends`: `zenity` or `kdialog` |
| Debian / Ubuntu | `zlib1g-dev` (`package.sh`, README) | DEB `Recommends: zenity \| kdialog`; `CPACK_PACKAGE_CONTACT` is set (the DEB generator requires a maintainer) |
| Windows (vcpkg) | `libpng`, `glm` added to the README install list (zlib comes with libpng) | none: native dialogs are part of Windows; `ole32`, `comdlg32`, `shell32`, `uuid` are linked |

Without zenity / kdialog on Linux, the application works; only File > Import / Export shows "no native file dialog available".

### 8.3 Licensing in packages

- `LICENSE` and `THIRD_PARTY_LICENSES.md` are installed to `/usr/share/doc/anafinen/` (RPM, DEB, Arch) and next to `anafinen.exe` (Windows ZIP). `CPACK_RESOURCE_FILE_LICENSE` points to `LICENSE`.
- The Windows ZIP redistributes GPL libraries (Gmsh, parts of SuiteSparse). `THIRD_PARTY_LICENSES.md` lists their source locations and a written source offer (GPLv3 section 6).
- The install layout is defined only in `cmake/Packaging.cmake`. The former platform-independent `install()` lines in `CMakeLists.txt` put a second executable in `/usr/anafinen` and assets in `/usr/assets`; they were removed.
- `ANAFINEN_VERSION` (`<version>-alpha`) is a compile definition of `anafinen`, shown in Help > About.

## 9. Common commands

```bash
# Configure + build (Linux)
git submodule update --init --recursive
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

# With tests
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DANAFINEN_BUILD_TESTS=ON
cmake --build build && (cd build && ctest --output-on-failure)

# Package on the current distro
./package/package.sh
```

## 10. Related files

- [CMakeLists.txt](../CMakeLists.txt)
- [cmake/CompilerOptions.cmake](../cmake/CompilerOptions.cmake)
- [cmake/Dependencies.cmake](../cmake/Dependencies.cmake)
- [cmake/ExternalGui.cmake](../cmake/ExternalGui.cmake)
- [cmake/Packaging.cmake](../cmake/Packaging.cmake)
- [package/package.sh](../package/package.sh), [package/PKGBUILD](../package/PKGBUILD), [package/PACKAGE_BUILD.md](../package/PACKAGE_BUILD.md)
- [.gitmodules](../.gitmodules)
- [tests/CMakeLists.txt](../tests/CMakeLists.txt)
