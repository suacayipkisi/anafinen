
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
`information/BUILD_SYSTEM.md` section 8.1.1.

Known issue on Debian 13: its `libgmsh4.13` aborts inside second-order 3D
meshing (an Eigen assertion in Gmsh itself), so `anaf_io_tests` aborts there.
The package builds and installs normally.

## Packaging for Windows

open in vscode: .vscode/tasks.json
```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "CPack: Create ZIP Package",
      "type": "shell",
      "command": "cpack",
      "args": [
        "--config",
        "${command:cmake.buildDirectory}/CPackConfig.cmake",
        "-G",
        "ZIP",
        "-B",
        "${command:cmake.buildDirectory}"
      ],
      "group": "build",
      "dependsOn": ["CMake: build"],
      "problemMatcher": []
    }
  ]
}
```
open vscode user settings.json: 
- press ctrl+shift+p
- Preferences: Open User Settings (JSON) then add this
```json
{
  "cmake.generator": "Ninja",
  "cmake.configureSettings": {
    "CMAKE_TOOLCHAIN_FILE": "C:/vcpkg/scripts/buildsystems/vcpkg.cmake",
    "GMSH_SDK_DIR": "C:/libs/gmsh-sdk",
    "VCPKG_TARGET_TRIPLET": "x64-windows"
  }
}
```
create package: 
- press ctrl+shift+p
- Tasks: Run Task
- CPack: Create ZIP Package  

it wil create .zip in build/ file.
