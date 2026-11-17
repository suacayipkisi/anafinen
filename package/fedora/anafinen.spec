# Fedora / Copr package of ANAFINEN, built from source by Copr (latest Fedora release only).
#
# Copr uses the "make srpm" source method: it clones the repository at the chosen tag or branch
# and runs .copr/Makefile, which calls package/fedora/make-srpm.sh. That script fetches the git
# submodules (Dear ImGui, ImPlot, ImGuizmo, Spectra, GLFW; GitHub's tag archives leave them out),
# packs Source0 from the tracked files and builds the SRPM; Copr then builds the RPM from it.
#
# Version: the upstream tag is v<version>-alpha; "~alpha" sorts before the final <version>.
# Keep upstream_version equal to project(VERSION) in CMakeLists.txt (make-srpm.sh checks it).

%global upstream_version 0.2.2
%global prerelease alpha
%global source_name anafinen-%{upstream_version}-%{prerelease}

# The project builds without LTO (CMake release flags: -O3 -ffast-math -fno-finite-math-only).
%global _lto_cflags %{nil}

Name:           anafinen
Version:        %{upstream_version}~%{prerelease}
Release:        2%{?dist}
Summary:        3D finite element analysis of trusses and beam frames
# GPL: anafinen; MIT: Dear ImGui, ImPlot, ImGuizmo, GLAD; Zlib: GLFW, nanosvg; MPL-2.0: Spectra;
# WTFPL: portable-file-dialogs; OFL-1.1: Inter, Cascadia Code (assets/fonts)
License:        GPL-3.0-or-later AND MIT AND Zlib AND MPL-2.0 AND WTFPL AND OFL-1.1
URL:            https://github.com/suacayipkisi/anafinen
# Created by package/fedora/make-srpm.sh (tracked files of the checkout, submodules included).
Source0:        %{source_name}-source-with-submodules.tar.gz

ExclusiveArch:  x86_64

BuildRequires:  gcc-c++
BuildRequires:  cmake
BuildRequires:  ninja-build
BuildRequires:  git-core
BuildRequires:  pkgconfig
BuildRequires:  desktop-file-utils
BuildRequires:  librsvg2-tools
BuildRequires:  eigen3-devel
BuildRequires:  suitesparse-devel
BuildRequires:  gmsh-devel
BuildRequires:  hdf5-devel
BuildRequires:  json-devel
BuildRequires:  glm-devel
BuildRequires:  libpng-devel
BuildRequires:  zlib-devel
BuildRequires:  freetype-devel
BuildRequires:  mesa-libGL-devel
# Built-in GLFW 3.4 (external/glfw + external/patches/glfw-3.4-anafinen.patch)
BuildRequires:  wayland-devel
BuildRequires:  libxkbcommon-devel
BuildRequires:  libX11-devel
BuildRequires:  libXrandr-devel
BuildRequires:  libXinerama-devel
BuildRequires:  libXcursor-devel
BuildRequires:  libXi-devel
BuildRequires:  libXext-devel

# GLFW opens these with dlopen, so the automatic dependencies cannot see them (the sonames GLFW 3.4 loads).
Requires:       libwayland-client.so.0()(64bit)
Requires:       libwayland-cursor.so.0()(64bit)
Requires:       libwayland-egl.so.1()(64bit)
Requires:       libxkbcommon.so.0()(64bit)
Requires:       libX11.so.6()(64bit)
Requires:       libXcursor.so.1()(64bit)
Requires:       libXrandr.so.2()(64bit)
Requires:       libXi.so.6()(64bit)
Requires:       hicolor-icon-theme
Recommends:     zenity
Suggests:       libdecor

# Patched copy built in statically (zlib license; see THIRD_PARTY_LICENSES.md).
Provides:       bundled(glfw) = 3.4
Provides:       bundled(imgui)
Provides:       bundled(implot)
Provides:       bundled(imguizmo)
Provides:       bundled(glad)
Provides:       bundled(nanosvg)
Provides:       bundled(portable-file-dialogs)

%description
ANAFINEN (Analyze Finite Element Engineering) is an open-source 3D finite
element analysis engine and visualizer: 3D trusses and Euler-Bernoulli /
Timoshenko beam frames with real cross-sections, end releases, inclined
supports, linear static analysis, stresses, beam diagrams and mesh / CAD
import and export. The package has the OpenGL GUI (anafinen) and the command
line program anafinen-cli. Alpha software: dynamic analysis is not
available yet.

%prep
%autosetup -n %{source_name}

%build
%cmake -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DANAFINEN_NATIVE_OPTIMIZATIONS=OFF \
    -DANAFINEN_BUILD_TESTS=OFF \
    -DANAFINEN_BUILD_TOOLS=OFF \
    -DANAFINEN_BUILD_CLI=ON
%cmake_build

%install
%cmake_install
# The license files are listed with %%license below.
rm -f %{buildroot}%{_docdir}/%{name}/LICENSE %{buildroot}%{_docdir}/%{name}/THIRD_PARTY_LICENSES.md

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/anafinen.desktop
# The patched GLFW must be linked in statically, never resolved from the system libglfw.so.3.
if readelf -d %{buildroot}%{_bindir}/anafinen | grep -q 'libglfw'; then
  echo "anafinen links a shared libglfw instead of the built-in patched GLFW" >&2
  exit 1
fi

%files
%license LICENSE THIRD_PARTY_LICENSES.md
%doc README.md
%{_bindir}/anafinen
%{_bindir}/anafinen-cli
%{_datadir}/anafinen/
%{_datadir}/applications/anafinen.desktop
%{_datadir}/icons/hicolor/*/apps/anafinen.*

%changelog
* Tue Nov 17 2026 suacayipkisi <konuki8523@gmail.com> - 0.2.2~alpha-2
- Link the patched GLFW statically: %%cmake's BUILD_SHARED_LIBS=ON built it as
  libglfw.so.3, which resolved to the system glfw at run time and the program
  did not start (undefined symbol glfwResizeWindowFromEdge)

* Tue Nov 17 2026 suacayipkisi <konuki8523@gmail.com> - 0.2.2~alpha-1
- Integrated title bar: menus, title and window buttons in one row; the window
  system still moves, resizes and snaps the window (Wayland, X11, Windows);
  Settings > Title Bar switches back to the system title bar
- GPU choice for hybrid-graphics laptops (Settings > Graphics): discrete
  (default), integrated or the system default
- New default theme "Professional" and six presets on its design, a ribbon,
  selection-driven Properties panels, Model Tree browser, ViewCube, SVG icons,
  FreeType text, UI font size setting, banded result colors
- First Copr build
