# ANAFINEN (Analyze Finite Element Engineering) 

<!-- Release & Downloads Badges -->
[![GitHub Release](https://img.shields.io/github/v/release/suacayipkisi/anafinen?include_prereleases&style=flat-square&color=blue)](https://github.com/suacayipkisi/anafinen/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/suacayipkisi/anafinen/total?style=flat-square&color=green)](https://github.com/suacayipkisi/anafinen/releases)

---

## 📦 Downloads (v0.1.3-alpha)

Pre-compiled binary releases for Windows and Linux are available under [GitHub Releases](https://github.com/suacayipkisi/anafinen/releases).

| Platform | File | Quick Run / Install Command |
| --- | --- | --- |
| **Windows** (10/11 x64 Portable) | [`anafinen-0.1.3-windows-AMD64-alpha.zip`](https://github.com/suacayipkisi/anafinen/releases/download/v0.1.3-alpha/anafinen-0.1.3-windows-AMD64-alpha.zip) | Extract the `.zip` into a folder you can write to (not `Program Files`) and run `anafinen.exe`. Needs the [Microsoft Visual C++ 2015-2022 Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe) |
| **Linux** (Fedora / RHEL / RPM-based) | [`anafinen-0.1.3-alpha.rpm`](https://github.com/suacayipkisi/anafinen/releases/download/v0.1.3-alpha/anafinen-0.1.3-alpha.rpm) | `sudo dnf install ./anafinen-0.1.3-alpha.rpm` |
| **Linux** (Debian / Ubuntu / DEB-based) | [`anafinen-0.1.3-alpha.deb`](https://github.com/suacayipkisi/anafinen/releases/download/v0.1.3-alpha/anafinen-0.1.3-alpha.deb) | `sudo apt install ./anafinen-0.1.3-alpha.deb` |
| **Linux** (Arch/CachyOS/Arch-based) | [`anafinen-0.1.3_alpha-1-x86_64.pkg.tar.zst`](https://github.com/suacayipkisi/anafinen/releases/download/v0.1.3-alpha/anafinen-0.1.3_alpha-1-x86_64.pkg.tar.zst) | `sudo pacman -U anafinen-0.1.3_alpha-1-x86_64.pkg.tar.zst` (needs `gmsh` or `gmsh-bin` from the AUR) |

---

**Finite Element Analysis Engine** -*under construction*- 

Look `information/` foler for documentation, project progress and so on.  

Latest release: 0.1.3-alpha (October 2026).  
Phase 1 works for 3D trusses: static displacement, axial force and stress under nodal loads and self-weight, inclined supports, built-in and imported / self-built trusses, mesh import / export (MSH, VTK, VTU, STEP, IGES, BREP).  
Phase 2 (modal analysis) is not implemented yet. Next steps: GUI design, a command-line (CLI) mode and solver updates, then beam elements and modal analysis.
- Displacement Under Applied Force (phase-1)
- Modal Analysis (phase-2)
- Heat Tranfer (phase-3)
- non-Linear Elasticity Calculations (phase 4)
- CFD(Calculated Fluid Dynamics) (phase 5)

I'm working with 4 books to build this projects.  
As you can understand I'm making this project for educational purposes  
**Here is the sources:**
- A first course in the Finite Element Method 5th edition
- Finite Element Procedures, Klaus-Jürgen Bathe 1996
- Matrix Computations 4th edition
- Mechanical Vibrations 5th edition Rao

## AI Assistance

Parts of this project were developed with the help of an AI coding assistant (Anthropic's Claude), mainly for:
- **File handling:** the `anaf_io` library (MSH / VTK / VTU / STEP / IGES / BREP import and export, async I/O)
- **GUI:** panels such as the model editor, import / export dialogs and status bar
- **Tests:** the `anaf_io` and truss test suites and the built-in truss library checks

The FEM theory, the solver design and the overall architecture are my own work, based on the books above. All AI-assisted code was reviewed(or currently under revıew), built and tested before it was committed.

## Libraries
- Calculation: Eigen, Spectra, SuiteSparse CHOLMOD
- Visualisation and GUI: OpenGL, GLAD, GLFW, ImGUI, ImGuizmo, ImPlot, glm, portable-file-dialogs
- File formats (import/export): Gmsh MSH 1/2.2/4.0/4.1, VTK legacy 2.0-5.1, VTK XML (.vtu), STEP/IGES/BREP (via Gmsh + OpenCASCADE)
- Multithreading: OpenMP and threaded SuiteSparse/BLAS backends

# Build (Linux and Windows)

I don't have money to buy a mac, sorry...  
So, there is no build for macOS

Build instructions are provided for Fedora, Arch/CachyOS, and Debian/Ubuntu.
When SuiteSparse development files are available, CMake automatically enables
Eigen's CHOLMOD backend; otherwise it falls back to Eigen's built-in sparse
LDLT solver.

If you are using another distro (not arch, fedora, debian or based on them) like gentoo, I'm sorry I will not try for them but somehow you did succeed, you can send me.

## Linux

### Libraries

#### Libraries Fedora

```bash
sudo dnf install -y \
    gcc-c++ \
    cmake \
    ninja-build \
    git \
    eigen3-devel \
    suitesparse-devel \
    mesa-libGL-devel \
    gmsh-devel \
    glfw-devel \
    spectra-devel \
    libpng-devel \
    zlib-devel \
    json-devel \
    glm-devel \
    ImageMagick \
    zenity
```

#### Libraries Arch-CachyOS
```bash
# Spectra comes from the git submodule (it is not in the official repos); OpenMP is GCC's libgomp (gcc-libs).
sudo pacman -S glibc gcc-libs eigen suitesparse glfw mesa cmake ninja git glm libpng zlib nlohmann-json librsvg zenity

# WARNING!!!!!! using paru means using AUR which is a place sometimes hackers might play around. be careful!!! 
# If you dont want to install via AUR, you may look for installing it from their websites like what we install for windows.
paru -S gmsh-bin
```

#### Libraries Debian-Ubuntu
```bash
sudo apt-get update
sudo apt-get install -y cmake ninja-build build-essential pkg-config libeigen3-dev libsuitesparse-dev libpng-dev libglfw3-dev libgmsh-dev libspectra-dev libgl1-mesa-dev libglm-dev zlib1g-dev nlohmann-json3-dev librsvg2-bin zenity
```

### Clone This Repo

```bash
# at your project folder -> cd ../(your projects location)
git clone https://github.com/suacayipkisi/anafinen.git
```

### Submodules

```bash
# at ../anafinen
git submodule update --init --recursive
```

### Start Build
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

File > Import / Export uses the desktop's own file chooser: install `zenity` (GNOME and most desktops) or `kdialog` (KDE).

### Tests (optional)
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DANAFINEN_BUILD_TESTS=ON
cmake --build build
cd build && ctest --output-on-failure
```
With the Python `vtk` package installed, the tests also cross-check every VTK/VTU variant against the official VTK library.

## Windows

- First, open cmd or powershell, be sure you have installed "git".

### Install vcpkg

```cmd
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg integrate install
```

### Install Libraries

Calculation libs:
```cmd
.\vcpkg install eigen3:x64-windows
.\vcpkg install spectra:x64-windows
.\vcpkg install suitesparse:x64-windows
```

SuiteSparse is optional at configure time. When its headers and libraries are
found, the build uses Eigen's CHOLMOD backend; otherwise it uses Eigen's
built-in sparse LDLT solver.

For native Windows builds, install SuiteSparse through vcpkg. The Linux-to-
Windows MinGW cross-build does not include CHOLMOD because Fedora does not ship
the required MinGW SuiteSparse development package; it uses the fallback solver
unless a real MinGW SuiteSparse installation is passed through `CHOLMOD_ROOT`.
The value `/path/to/suitesparse` is only an example and must not be passed
literally.

Gui and visualization:
```cmd
.\vcpkg install glfw3:x64-windows
.\vcpkg install glad:x64-windows
.\vcpkg install "imgui[core,docking-experimental,glfw-binding,opengl3-binding]:x64-windows" --recurse
.\vcpkg install implot:x64-windows
.\vcpkg install imguizmo:x64-windows
.\vcpkg install libpng:x64-windows
.\vcpkg install glm:x64-windows
.\vcpkg install nlohmann-json:x64-windows
```

Mesh engine:  
- Try to install with vcpkg, if not install(probably) download from this link:  
- https://gmsh.info/bin/Windows/?C=M;O=D  
- When I last checked(2 sep 2026), "gmsh-4.15.2-Windows64-sdk.zip" was the latest. Dont install git versions.  
- Extract the `.zip` anywhere, for example `C:\libs\gmsh-sdk` (the folder must contain `bin`, `include`, `lib` and `share`).
- CMake finds the SDK and vcpkg on its own, on any drive: `GMSH_SDK_DIR` / `VCPKG_ROOT` environment variables, `vcpkg` on `PATH`, then folders such as `<drive>:\libs\gmsh-sdk`, `<drive>:\gmsh-*-Windows64-sdk` and `<drive>:\vcpkg`. The configure log prints what it found (`Gmsh SDK auto-detected: ...`). For any other location set `GMSH_SDK_DIR` (environment variable or `-DGMSH_SDK_DIR=...`). Details: [information/BUILD_SYSTEM.md](information/BUILD_SYSTEM.md) section 5.2.

### Finally Open Visual Studio

- Open the folder as a CMake project and select an **x64** configuration (a 32-bit kit stops at configure time with a message).
- The vcpkg toolchain is detected automatically. Only for an unusual location, open Project -> CMake Settings and set the CMake toolchain file to `<your vcpkg folder>/scripts/buildsystems/vcpkg.cmake`, then press ctrl+s.
- Be sure you see "Build Succesful"

### Start to Build

- Pres ctrl+shift+b to start build.
- After build press f5 to open debug mode or ctrl+f5 to open normally.
- DONE!

## Packaging

- Linux: `./package/package.sh` picks RPM (Fedora), pacman (Arch / CachyOS) or DEB (Debian / Ubuntu) by itself.
- Debian and Arch packages can also be built in clean containers (podman or docker): `package/tools/container-check.sh all package`. The packages land in `build-containers/<distro>/`.
- Windows: `package/package-windows.ps1` builds a Release binary and the portable `.zip` (CPack).

See [package/PACKAGE_BUILD.md](package/PACKAGE_BUILD.md) for details.


## Licensing & Third Party Library and Font Licenses

This project is open-source software licensed under the **GNU General Public License v3.0 (GPLv3)**.

Copyright (c) 2026 Abdurrahman Konuk, professionally known as Ufuk Deniz Konuk. Both names refer to the same person, the sole copyright holder of this project; "Ufuk Deniz Konuk" is used in source headers, packages and releases. See the [LICENSE](LICENSE) and [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) file for details.

### Commercial & Enterprise Licensing

**Using anafinen is free under the GPLv3, including commercial use.** Companies may run it for engineering work, sell services or reports produced with it, and modify it for internal use without publishing their changes. The GPLv3 obligations (providing the source code under the GPLv3) apply only when the program, or software containing it, is **distributed** to others.

A separate commercial license is needed only to **embed anafinen in a proprietary, closed-source product and distribute that product** without the GPLv3 copyleft terms. Such licenses and custom support agreements are available from the copyright holder.

A commercial license covers anafinen's own code only. Third-party components licensed under the GPL are not the copyright holder's to relicense:
- Gmsh (used for CAD meshing)
- the GPL modules of SuiteSparse CHOLMOD (optional solver)

A commercially licensed build either excludes these components, or the licensee obtains their licenses separately. Permissively licensed components (MIT, zlib, MPL-2.0, LGPL with dynamic linking) keep their own notice requirements; see [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

For commercial inquiries: `konuki8523@gmail.com`

### Contributing
Contributions are welcome. By submitting a pull request, you agree to our [Contributor License Agreement (CLA)](.github/CLA.md), granting the maintainer the right to re-license contributions under both GPLv3 and commercial terms.
