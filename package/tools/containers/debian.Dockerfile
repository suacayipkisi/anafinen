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

# Debian build image for package/tools/container-check.sh. The package list matches the Debian branch
# of package/package.sh, so its apt-get install step finds everything already installed.
FROM docker.io/library/debian:stable

RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y \
      sudo git ccache file ca-certificates \
      cmake ninja-build build-essential pkg-config librsvg2-bin libeigen3-dev libsuitesparse-dev \
      libpng-dev libglfw3-dev libgmsh-dev libspectra-dev libgl1-mesa-dev libglm-dev zlib1g-dev \
      nlohmann-json3-dev \
 && rm -rf /var/lib/apt/lists/*

# Unprivileged build user with passwordless sudo (package.sh calls sudo apt-get).
# /ccache is a named volume at run time; the system config points ccache at it.
RUN useradd -m builder \
 && echo 'builder ALL=(ALL) NOPASSWD: ALL' > /etc/sudoers.d/builder \
 && printf 'cache_dir = /ccache\nmax_size = 5G\n' > /etc/ccache.conf \
 && mkdir -p /work /out /ccache \
 && chown builder:builder /work /out /ccache
