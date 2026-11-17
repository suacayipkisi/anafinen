
## Packaging for Linux (Fedora, Arch/CachyOS, and Debian)

CHOLMOD is used automatically when SuiteSparse development files are
available. The native packaging script installs them during the build:

- Fedora: `suitesparse-devel` (runtime package: `suitesparse`)
- Arch/CachyOS: `suitesparse`
- Debian/Ubuntu: `libsuitesparse-dev`

HDF5 (binary matrix / vector files) is required and always comes prebuilt from the
distribution: Fedora `hdf5-devel`, Arch/CachyOS `hdf5`, Debian/Ubuntu `libhdf5-dev`.
The scripts and the container images install it.

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

The PNG icons are rendered at configure time with `rsvg-convert` (librsvg) or,
as a fallback, ImageMagick: 128x128 from `assets/icons/anafinen.svg`, and
16 / 24 / 32 px from `assets/icons/anafinen-small.svg`. The scripts install the
converter. Without one, CMake uses the committed `anafinen.png` /
`anafinen-32.png` and skips the 16 / 24 px hicolor icons. After editing an SVG,
run `package/tools/render-icons.py` to regenerate the committed `.ico` and PNGs.

To check the Debian and Arch packages without those systems, run
`package/tools/container-check.sh debian package` or `package/tools/container-check.sh arch package`
(podman or docker). The packages land in `build-containers/<distro>/`. To build both and
collect them for a release:

```bash
package/tools/container-check.sh all package && \
  cp build-containers/debian/*.deb build-containers/arch/anafinen-[0-9]*.pkg.tar.zst package/early-release-packages/
```

Known issue on Debian 13: its `libgmsh4.13` aborts inside second-order 3D
meshing (an Eigen assertion in Gmsh itself), so the mesh I/O tests abort there.
The package builds and installs normally.

## Fedora Copr (built by Copr from the repository)

Copr builds the RPM itself, for the latest Fedora release only (the same policy as the RPM on
GitHub Releases). Files:

| File | Role |
|---|---|
| `package/fedora/anafinen.spec` | The spec. Version `0.2.2~alpha` (`%global upstream_version` must match `project(VERSION)` in `CMakeLists.txt`); `%changelog` gets one entry per release |
| `package/fedora/make-srpm.sh` | Fetches the submodules, packs the tracked files (submodules included, nothing untracked: no build trees, `tests/`, `information/`) into `anafinen-<version>-alpha-source-with-submodules.tar.gz` (reproducible: sorted, commit time) and runs `rpmbuild -bs` |
| `.copr/Makefile` | Copr's "make srpm" entry point: installs `git-core rpm-build`, runs `make-srpm.sh` into Copr's `outdir` |

GitHub's tag archives leave out the submodules (Dear ImGui, ImPlot, ImGuizmo, Spectra, GLFW), so the
spec cannot point at them the way a project without submodules can; the "make srpm" method clones
the repository with network access and builds the SRPM, then the RPM is built from it offline.

One-time setup on <https://copr.fedorainfracloud.org>:

1. New Project: name `anafinen`; Chroots: only the latest `fedora-NN-x86_64`; leave "Follow Fedora
   branching" off (a new Fedora release is added by hand, with a rebuild).
2. Packages > New package > SCM: Clone URL `https://github.com/suacayipkisi/anafinen.git`,
   Committish = the release tag (e.g. `v0.2.2-alpha`), Subdirectory empty, Spec File
   `package/fedora/anafinen.spec`, Type `git`, SRPM build method **make srpm**.
3. Rebuild the package. Optionally: Settings > Integrations, GitHub webhook, to build on every push
   of a tag.

Per release: bump the spec (`upstream_version`, `Release: 1`, `%changelog`), commit, push the tag,
change the package's Committish to the new tag and rebuild. Users install with:

```bash
sudo dnf copr enable suacayipkisi/anafinen
sudo dnf install anafinen
```

Local check of the spec (same SRPM as Copr's): `package/fedora/make-srpm.sh /tmp/srpm` and
`rpmbuild --rebuild /tmp/srpm/anafinen-*.src.rpm`, or `mock -r fedora-NN-x86_64 --rebuild` for a
clean, offline chroot like Copr's.

## Packaging for Windows

Requirements: Visual Studio 2022 (or Build Tools) with the C++ x64 tools, CMake, Ninja,
vcpkg (with the packages listed in the README, including `hdf5:x64-windows`) and the Gmsh SDK.
vcpkg compiles each port once; the anafinen build only links them, and `RUNTIME_DEPENDENCIES`
copies the DLLs the executables import (`gmsh-4.15.dll`, `cholmod.dll`, `z.dll`, ...; GLFW is linked statically from the
`external/glfw` submodule)
into the ZIP. `hdf5.dll` is copied only once a program calls the HDF5 array store; the GUI and the
CLI do not yet, so the MSVC linker drops the import and the v0.2.1 ZIP has no `hdf5.dll`.

vcpkg and the Gmsh SDK may live on any drive: CMake finds them on its own
(`VCPKG_ROOT` / `GMSH_SDK_DIR` environment variables, `vcpkg` on `PATH`, then
`<drive>:/vcpkg`, `<drive>:/libs/gmsh-sdk`, `<drive>:/gmsh-*-Windows64-sdk`, ... on every
drive). A minimal `.vscode/settings.json` is enough:
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
(app-local: `vcruntime140*.dll`, `msvcp140*.dll` and the `/openmp` runtime `vcomp140.dll`, all
through `InstallRequiredSystemLibraries`), so the target machine does not need the Visual C++
Redistributable. MSVC builds use `/openmp`, not `/openmp:llvm`: the LLVM runtime
`libomp140.x86_64.dll` ships only under `debug_nonredist` and may not be redistributed. To check a ZIP, open it on a clean
Windows (VM or Windows Sandbox) where the Redistributable is not installed.
