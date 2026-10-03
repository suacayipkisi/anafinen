# Copyright (c) 2026 Abdurrahman Konuk (professionally known as Ufuk Deniz Konuk)
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.
#
# SPDX-License-Identifier: GPL-3.0-or-later

# Arch build image for package/tools/container-check.sh. Holds every depends / makedepends entry of
# package/PKGBUILD, plus gmsh-bin from the AUR (makepkg --syncdeps cannot install AUR packages).
FROM docker.io/library/archlinux:latest

RUN pacman -Syu --noconfirm --needed \
      base-devel git sudo ccache file \
      cmake ninja eigen glm nlohmann-json librsvg \
      glfw libglvnd suitesparse libpng zlib hdf5 \
 && pacman -Scc --noconfirm

# Unprivileged build user with passwordless sudo (makepkg refuses to run as root).
# /ccache is a named volume at run time; the system config points ccache at it.
RUN useradd -m builder \
 && echo 'builder ALL=(ALL) NOPASSWD: ALL' > /etc/sudoers.d/builder \
 && printf 'cache_dir = /ccache\nmax_size = 5G\n' > /etc/ccache.conf \
 && mkdir -p /work /out /ccache \
 && chown builder:builder /work /out /ccache

USER builder
RUN cd /tmp \
 && git clone --depth 1 https://aur.archlinux.org/gmsh-bin.git \
 && cd gmsh-bin \
 && makepkg --syncdeps --install --noconfirm \
 && rm -rf /tmp/gmsh-bin \
 && sudo pacman -Scc --noconfirm
USER root
