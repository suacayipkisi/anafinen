
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

For Arch/CachyOS, `makepkg` uses [PKGBUILD](PKGBUILD). For Debian, the script
uses CPack's DEB generator and derives shared-library dependencies with
`dpkg-shlibdeps`.

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
