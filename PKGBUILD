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
source=()

build() {
  if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    git submodule update --init --recursive
  fi
  cmake -B build -S "$startdir" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr
  cmake --build build
}

package() {
  DESTDIR="$pkgdir" cmake --install build
}