# ANAFINEN (Analyze Finite Element Engineering) 

<!-- Release & Downloads Badges -->
[![GitHub Release](https://img.shields.io/github/v/release/suacayipkisi/anafinen?include_prereleases&style=flat-square&color=blue)](https://github.com/suacayipkisi/anafinen/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/suacayipkisi/anafinen/total?style=flat-square&color=green)](https://github.com/suacayipkisi/anafinen/releases)

---

## 📦 Downloads (v0.1.0-alpha)

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
- Calculation: Eigen, Spectra
- Visualisation and GUI: OpenGL, GLAD, GLFW, ImGUI, ImGuizmo, ImPlot
- Multithreading: OpenMP

# Build and package (Linux and Windows)

I don't have money to buy a mac, sorry...  
So, there is no build for macOS

I tried to build in cachyOS but I couldn't installed gmsh lib from aur, some problems occoured.  
If I would be succeed somehow, then I will add build instruction for arch too.  

Also I will try in debian and add build instruction for it too.

If you are using another distro (not arch, fedora, debian or based on them) like gentoo, I'm sorry I will not try for them but somehow you did succeed, you can send me.

## Fedora, Arch/CachyOS, and Debian

After installing the libraries listed below for your distribution, run the
same command from the repository root. The script initializes submodules,
builds a Release binary, and selects RPM, pacman, or DEB according to the
distribution.

```bash
./package.sh
```

For Arch/CachyOS, `makepkg` uses [PKGBUILD](PKGBUILD). For Debian, the script
uses CPack's DEB generator and derives shared-library dependencies with
`dpkg-shlibdeps`.

## Fedora

### Libraries

```bash
sudo dnf install -y \
    gcc-c++ \
    cmake \
    ninja-build \
    git \
    eigen3-devel \
    mesa-libGL-devel \
    gmsh-devel \
    glfw-devel \
    spectra-devel
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
```

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

### Package from Windows

Run PowerShell from the repository root. The Gmsh SDK must contain `include`,
`lib`, and `bin/gmsh.dll`.

```powershell
.\package.ps1 -VcpkgRoot "C:/Users/<your username>/vcpkg" -GmshSdkDir "C:/libs/gmsh-sdk"
```

The result is a ZIP containing the executable, `gmsh.dll`, and the required
assets beside the executable.


## Licensing & Third Party Library and Font Licenses

This project is open-source software licensed under the **GNU General Public License v3.0 (GPLv3)**. See the [LICENSE](LICENSE) and [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) file for details.

### Commercial & Enterprise Licensing
If you wish to integrate this project into proprietary, closed-source software without being bound by the copyleft terms of the GPLv3, commercial licenses and custom support agreements are available directly from the copyright holder.

For commercial inquiries: `konuki8523@gmail.com`

### Contributing
Contributions are welcome. By submitting a pull request, you agree to our [Contributor License Agreement (CLA)](CLA.md), granting the maintainer the right to re-license contributions under both GPLv3 and commercial terms.
