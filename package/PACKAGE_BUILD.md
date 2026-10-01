
## Packaging for Linux (Fedora, Arch/CachyOS, and Debian)

CHOLMOD is used automatically when SuiteSparse development files are
available. The native packaging script installs them during the build:

- Fedora: `suitesparse-devel` (runtime package: `suitesparse`)
- Arch/CachyOS: `suitesparse`
- Debian/Ubuntu: `libsuitesparse-dev`

After installing the libraries listed below for your distribution, run the
same command from the repository root. The script initializes submodules,
builds a Release binary, and selects RPM, pacman, or DEB according to the
distribution.

```bash
./package/package.sh
```

The script can be started from any directory; it works from the repository root.

For Arch/CachyOS, `makepkg` uses [PKGBUILD](PKGBUILD) and installs the build
dependencies from the official repositories itself. Gmsh is only in the AUR, so
install it first (`gmsh`, built from source, or the prebuilt `gmsh-bin`); the
script stops with a hint when it is missing. For Debian, the script uses CPack's
DEB generator and derives shared-library dependencies with `dpkg-shlibdeps`.

The 128x128 PNG icon is rendered from `assets/icons/anafinen.svg` with
`rsvg-convert` (librsvg) or, as a fallback, ImageMagick. The scripts install
the converter. Without one, the package has no PNG icon (CMake warns).

To check the Debian and Arch packages without those systems, run
`package/tools/container-check.sh debian package` or `package/tools/container-check.sh arch package`
(podman or docker). The packages land in `build-containers/<distro>/`; see
`information/BUILD_SYSTEM.md` section 8.1.1. To build both and collect them for a release:

```bash
package/tools/container-check.sh all package && \
  cp build-containers/debian/*.deb build-containers/arch/anafinen-[0-9]*.pkg.tar.zst package/early-release-packages/
```

Known issue on Debian 13: its `libgmsh4.13` aborts inside second-order 3D
meshing (an Eigen assertion in Gmsh itself), so `anaf_io_tests` aborts there.
The package builds and installs normally.

## Packaging for Windows

Requirements: Visual Studio 2022 (or Build Tools) with the C++ x64 tools, CMake, Ninja,
vcpkg and the Gmsh SDK.

vcpkg and the Gmsh SDK may live on any drive: CMake finds them on its own
(`VCPKG_ROOT` / `GMSH_SDK_DIR` environment variables, `vcpkg` on `PATH`, then
`<drive>:/vcpkg`, `<drive>:/libs/gmsh-sdk`, `<drive>:/gmsh-*-Windows64-sdk`, ... on every drive;
see `information/BUILD_SYSTEM.md` section 5.2). A minimal `.vscode/settings.json` is enough:
```json
{
  "cmake.generator": "Ninja",
  "cmake.configureSettings": { "VCPKG_TARGET_TRIPLET": "x64-windows" },
  "cmake.buildDirectory": "${workspaceFolder}/build/${buildType}"
}
```
Only for an unusual location add `"CMAKE_TOOLCHAIN_FILE": "<vcpkg>/scripts/buildsystems/vcpkg.cmake"`
and/or `"GMSH_SDK_DIR": "<sdk>"` to `cmake.configureSettings` (or set the environment variables);
the packaging script passes them on as well.

`.vscode/` is not tracked; put this task into `.vscode/tasks.json`:
```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "CPack: Create ZIP Package",
      "type": "process",
      "command": "powershell.exe",
      "args": ["-NoProfile", "-ExecutionPolicy", "Bypass", "-File", "${workspaceFolder}/package/package-windows.ps1"],
      "options": { "cwd": "${workspaceFolder}" },
      "group": "build",
      "presentation": { "reveal": "always", "focus": true, "panel": "dedicated", "clear": true },
      "problemMatcher": []
    }
  ]
}
```

create package:
- press ctrl+shift+p
- Tasks: Run Task
- CPack: Create ZIP Package

`"type": "process"` starts PowerShell directly, independent of the terminal's default shell
(no quoting or path rewriting by Git Bash or cmd). A first Release build takes a few
minutes; the output appears in the task's own terminal panel.

or from a terminal:
```powershell
powershell -ExecutionPolicy Bypass -File package/package-windows.ps1
```

The script loads the MSVC environment itself, always builds **Release** in
`build/package-release` (independent of the variant selected in VS Code) and writes
`build/package-release/anafinen-<version>-windows-AMD64-alpha.zip`. A Debug build must not be
packaged: it needs the debug CRT (`ucrtbased.dll`, `VCRUNTIME140D.dll`), which exists only
where Visual Studio is installed. The ZIP carries the MSVC runtime next to `anafinen.exe`
(app-local: `vcruntime140*.dll`, `msvcp140*.dll` through `InstallRequiredSystemLibraries`, and
`libomp140.x86_64.dll` for `/openmp:llvm` from the MSVC redist directory), so the target machine
does not need the Visual C++ Redistributable. If configure warns that `libomp140.x86_64.dll` was
not found, the ZIP still needs the Redistributable (x64). To check a ZIP, open it on a clean
Windows (VM or Windows Sandbox) where the Redistributable is not installed.
