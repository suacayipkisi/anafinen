# Maintainer: Ufuk Deniz Konuk <konuki8523@gmail.com>
pkgname=anafinen
pkgver=0.1.1_alpha
pkgrel=1
pkgdesc="3D FEM Analysis Engine"
arch=('x86_64')
url="https://github.com/suacayipkisi/anafinen"
license=('GPL-3.0-or-later')
depends=('glibc' 'gcc-libs' 'eigen' 'spectra' 'glfw' 'mesa' 'gmsh' 'openmp')
makedepends=('cmake' 'ninja' 'git' 'eigen' 'spectra' 'glfw' 'mesa' 'glm' 'gmsh' 'openmp')
options=('!lto')
source=()

build() {
  cd "$startdir"
  cmake -B build-pkg -S . -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=OFF \
    -DCMAKE_EXE_LINKER_FLAGS="-fuse-ld=bfd -fno-lto"
  cmake --build build-pkg
}

package() {
  DESTDIR="$pkgdir" cmake --install "$startdir/build-pkg"
}
