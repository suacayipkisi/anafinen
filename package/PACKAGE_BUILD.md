
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

Run PowerShell from the repository root. The Gmsh SDK must contain `include`,
`lib`, and `bin/gmsh.dll`.

```powershell
.\package\package.ps1 -VcpkgRoot "C:/Users/<your username>/vcpkg" -GmshSdkDir "C:/libs/gmsh-sdk"
```

When using vcpkg, the script installs `suitesparse:x64-windows` and enables
CHOLMOD automatically. For a manual SuiteSparse installation, pass its root:

```powershell
.\package\package.ps1 -CholmodRoot "C:/libs/SuiteSparse" -GmshSdkDir "C:/libs/gmsh-sdk"
```

The result is a ZIP containing the executable, `gmsh.dll`, and the required
assets beside the executable.

## Packaging for Windows via Linux

### in Fedora for Windows

```bash
sudo dnf install -y \
  mingw64-gcc \
  mingw64-gcc-c++ \
  mingw64-binutils \
  mingw64-crt \
  mingw64-winpthreads \
  mingw64-libgomp \
  mingw64-eigen3 \
  mingw64-libpng \
  mingw64-zlib \
  glm-devel \
  ninja-build \
  ImageMagick \
  zip
```

```bash
sudo ln -sf /usr/include/glm /usr/x86_64-w64-mingw32/sys-root/mingw/include/glm
```

```bash
rm -rf build-win64
cmake -B build-win64 -S . -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=package/mingw64-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DGMSH_SDK_DIR="$HOME/Projects/gmsh-sdk" \
  -DGMSH_INCLUDE_DIR="$HOME/Projects/gmsh-sdk/include" \
  -DGMSH_LIBRARY="$HOME/Projects/gmsh-sdk/lib/gmsh-4.15.dll" \
  -DGMSH_DLL="$HOME/Projects/gmsh-sdk/lib/gmsh-4.15.dll"
cmake --build build-win64
cpack --config build-win64/CPackConfig.cmake -B build-win64 -G ZIP
```

### in Arch-CachyOS for Windows

```bash
sudo pacman -S --needed --noconfirm \
  mingw-w64-gcc \
  mingw-w64-binutils \
  mingw-w64-crt \
  mingw-w64-winpthreads \
  glm \
  eigen \
  ninja \
  imagemagick \
  zip

# WARING !!!! AUR !!!!
paru -S --needed --noconfirm \
  mingw-w64-zlib \
  mingw-w64-libpng \
  mingw-w64-eigen
```

```bash
sudo ln -sf /usr/include/glm /usr/x86_64-w64-mingw32/include/glm
```

```bash
rm -rf build-win64
cmake -B build-win64 -S . -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=package/mingw64-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DGMSH_SDK_DIR="$HOME/Projects/gmsh-sdk" \
  -DGMSH_INCLUDE_DIR="$HOME/Projects/gmsh-sdk/include" \
  -DGMSH_LIBRARY="$HOME/Projects/gmsh-sdk/lib/gmsh-4.15.dll" \
  -DGMSH_DLL="$HOME/Projects/gmsh-sdk/lib/gmsh-4.15.dll"
cmake --build build-win64
cpack --config build-win64/CPackConfig.cmake -B build-win64 -G ZIP
```

### in Debian-Ubuntu for Windows

```bash
sudo apt update && sudo apt install -y \
  gcc-mingw-w64-x86-64 \
  g++-mingw-w64-x86-64 \
  binutils-mingw-w64-x86-64 \
  mingw-w64-x86-64-dev \
  libeigen3-dev \
  libglm-dev \
  ninja-build \
  imagemagick \
  zip \
  cmake \
  git \
  build-essential
```

```bash
sudo mkdir -p /usr/x86_64-w64-mingw32/include
sudo ln -sf /usr/include/eigen3 /usr/x86_64-w64-mingw32/include/eigen3
sudo ln -sf /usr/include/glm /usr/x86_64-w64-mingw32/include/glm
```

```bash
mkdir -p /tmp/mingw_deps && cd /tmp/mingw_deps

# zlib
git clone --depth 1 https://github.com/madler/zlib.git
cd zlib
make -f win32/Makefile.gcc PREFIX=x86_64-w64-mingw32- -j$(nproc)
sudo cp zlib1.dll /usr/x86_64-w64-mingw32/lib/
sudo cp libz.a /usr/x86_64-w64-mingw32/lib/
sudo cp libzdll.a /usr/x86_64-w64-mingw32/lib/libz.dll.a
sudo cp zlib.h zconf.h /usr/x86_64-w64-mingw32/include/
cd ..

# libpng
git clone --depth 1 https://github.com/pnggroup/libpng.git
cd libpng
cmake -B build -G Ninja \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc \
  -DCMAKE_INSTALL_PREFIX=/usr/x86_64-w64-mingw32 \
  -DZLIB_INCLUDE_DIR=/usr/x86_64-w64-mingw32/include \
  -DZLIB_LIBRARY=/usr/x86_64-w64-mingw32/lib/libz.dll.a \
  -DPNG_SHARED=ON -DPNG_STATIC=OFF -DPNG_TESTS=OFF
ninja -C build
sudo ninja -C build install
cd "$HOME/Projects/anafinen"
rm -rf /tmp/mingw_deps
```

```bash
rm -rf build-win64
cmake -B build-win64 -S . -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=package/mingw64-toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DGMSH_SDK_DIR="$HOME/Projects/gmsh-sdk" \
  -DGMSH_INCLUDE_DIR="$HOME/Projects/gmsh-sdk/include" \
  -DGMSH_LIBRARY="$HOME/Projects/gmsh-sdk/lib/gmsh-4.15.dll" \
  -DGMSH_DLL="$HOME/Projects/gmsh-sdk/lib/gmsh-4.15.dll"
cmake --build build-win64
cpack --config build-win64/CPackConfig.cmake -B build-win64 -G ZIP
```
