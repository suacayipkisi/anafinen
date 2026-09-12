# ANAFINEN (Analyze Finite Element Engineering) 

<!-- Release & Downloads Badges -->
[![GitHub Release](https://img.shields.io/github/v/release/suacayipkisi/anafinen?include_prereleases&style=flat-square&color=blue)](https://github.com/suacayipkisi/anafinen/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/suacayipkisi/anafinen/total?style=flat-square&color=green)](https://github.com/suacayipkisi/anafinen/releases)

---

## 📦 Downloads (v0.1.1-alpha)

Pre-compiled binary releases for Windows and Linux are available under [GitHub Releases](https://github.com/suacayipkisi/anafinen/releases/tag/v0.1.0-alpha).

| Platform | File | Quick Run / Install Command |
| --- | --- | --- |
| **Windows** (via Winget) | *Package Manager* (moderator approve waiting) | `winget install suacayipkisi.anafinen` |
| **Windows** (10/11 x64 Portable) | [`anafinen-0.1.0-windows-AMD64-alpha.zip`](https://github.com/suacayipkisi/anafinen/releases/download/v0.1.0-alpha/anafinen-0.1.0-windows-AMD64-alpha.zip) | Extract `.zip` and **run as administrator** `bin/anafinen.exe` |
| **Linux** (Fedora / RHEL / RPM-based) | [`anafinen-0.1.1-alpha.rpm`](https://github.com/suacayipkisi/anafinen/releases/download/v0.1.0-alpha/anafinen-0.1.1-alpha.rpm) | `sudo dnf install ./anafinen-0.1.1-alpha.rpm` |
| **Linux** (Arch/CachyOS/Arch-based) | [`anafinen-0.1.1_alpha-1-x86_64.pkg.tar.zst`](https://github.com/suacayipkisi/anafinen/releases/download/v0.1.0-alpha/anafinen-0.1.1_alpha-1-x86_64.pkg.tar.zst) | `sudo pacman -U anafinen-0.1.1_alpha-1-x86_64.pkg.tar.zst` |

---

**Finite Element Analysis Engine** -*under construction*- 

Currently project is at phase 1
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

## Libraries
- Calculation: Eigen, Spectra, SuiteSparse CHOLMOD
- Visualisation and GUI: OpenGL, GLAD, GLFW, ImGUI, ImGuizmo, ImPlot, glm
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
    glm-devel
```

#### Libraries Arch-CachyOS
```bash
sudo pacman -S glibc gcc-libs eigen suitesparse spectra glfw mesa openmp cmake ninja git glm

# WARNING!!!!!! using paru means using AUR which is a place sometimes hackers might play around. be careful!!! 
# If you dont want to install via AUR, you may look for installing it from their websites like what we install for windows.
paru -S gmsh-bin
```

#### Libraries Debian-Ubuntu
```bash
sudo apt-get update
sudo apt-get install -y cmake ninja-build build-essential pkg-config libeigen3-dev libsuitesparse-dev libpng-dev libglfw3-dev libgmsh-dev libspectra-dev libgl1-mesa-dev libglm-dev
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

Gui and visualization:
```cmd
.\vcpkg install glfw3:x64-windows
.\vcpkg install glad:x64-windows
.\vcpkg install "imgui[core,docking-experimental,glfw-binding,opengl3-binding]:x64-windows" --recurse
.\vcpkg install implot:x64-windows
.\vcpkg install imguizmo:x64-windows
```

Mesh engine:  
- Try to install with vcpkg, if not install(probably) download from this link:  
- https://gmsh.info/bin/Windows/?C=M;O=D  
- When I last checked(2 sep 2026), "gmsh-4.15.2-Windows64-sdk.zip" was the latest. Dont install git versions.  
- Extract .zip into C:\libs\gmsh-sdk
- be sure you can find files at:
- - "C:\libs\gmsh-sdk\bin" 
- - "C:\libs\gmsh-sdk\include" 
- - "C:\libs\gmsh-sdk\lib" 
- - "C:\libs\gmsh-sdk\share"
- - "C:\libs\gmsh-sdk\README.md" 
- If you set location different, you might have some problems.
- I can see that pushing you to an exact location might be wrong, also if you extracted gmsh in another location you can change [CMakeLists.txt](CMakeLists.txt) for setting your own location instead of rearranging file locations.

### Finally Open Visual Studio

- Open from top menu Project -> CMake Settings  
- Paste this into CMake toolchain file: "C:/Users/<your username>/vcpkg/scripts/buildsystems/vcpkg.cmake" then press ctrl+s
- Be sure you see "Build Succesful"

### Start to Build

- Pres ctrl+shift+b to start build.
- After build press f5 to open debug mode or ctrl+f5 to open normally.
- DONE!


## Licensing & Third Party Library and Font Licenses

This project is open-source software licensed under the **GNU General Public License v3.0 (GPLv3)**. See the [LICENSE](LICENSE) and [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) file for details.

### Commercial & Enterprise Licensing
If you wish to integrate this project into proprietary, closed-source software without being bound by the copyleft terms of the GPLv3, commercial licenses and custom support agreements are available directly from the copyright holder.

For commercial inquiries: `konuki8523@gmail.com`

### Contributing
Contributions are welcome. By submitting a pull request, you agree to our [Contributor License Agreement (CLA)](CLA.md), granting the maintainer the right to re-license contributions under both GPLv3 and commercial terms.
