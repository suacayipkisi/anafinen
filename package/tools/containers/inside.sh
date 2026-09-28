#!/usr/bin/env bash
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

# Runs as root inside an anafinen-build container, started by package/tools/container-check.sh.
# stdin: tar of the source tree. stdout: tar of /out (logs and packages), read back by the host.
# All progress output goes to stderr.
#
# Usage: inside.sh <distro> <test|package|prepare>
#   test     package/tools/check.sh gcc (build + ctest) as the build user
#   package  package/package.sh, then installs the package and checks its libraries and icon
#   prepare  only unpacks the source (used by the interactive shell mode, no tar on stdout)

set -uo pipefail

DISTRO="$1"
MODE="$2"
MAX_LINES="${MAX_LINES:-30}"

exec 3>&1 1>&2

tar -x -C /work || { echo "[$DISTRO] could not unpack the source tree"; exit 1; }
chown -R builder:builder /work /out /ccache

as_builder() {
  sudo -u builder -H bash -c "cd /work && $1"
}

run_package() {
  local log=/out/package.log
  as_builder "package/package.sh" > "$log" 2>&1
  local status=$?
  grep -E "(warning|error):" "$log" | grep -vE "/(_deps|external)/" | sort -u | head -n "$MAX_LINES"
  if [[ $status -ne 0 ]]; then
    echo "[$DISTRO] package.sh FAILED (log: package.log)"
    tail -n 15 "$log"
    return 1
  fi
  echo "[$DISTRO] package.sh OK"

  local pkg
  case "$DISTRO" in
    debian)
      cp /work/build/*.deb /out/
      pkg="$(ls /out/*.deb | head -1)"
      apt-get install -y "$pkg" >> "$log" 2>&1 ;;
    arch)
      cp /work/package/*.pkg.tar.zst /out/
      # makepkg may also emit an anafinen-debug package; install the main one only.
      pkg="$(ls /out/anafinen-[0-9]*.pkg.tar.zst | head -1)"
      pacman -U --noconfirm "$pkg" >> "$log" 2>&1 ;;
  esac
  [[ $? -ne 0 ]] && { echo "[$DISTRO] installing ${pkg#/out/} FAILED"; return 1; }
  echo "[$DISTRO] installed ${pkg#/out/}"

  local missing
  missing="$(ldd /usr/bin/anafinen | grep "not found")"
  if [[ -n "$missing" ]]; then
    echo "[$DISTRO] missing libraries:"; echo "$missing"
    return 1
  fi
  echo "[$DISTRO] ldd: all libraries found"
  echo "[$DISTRO] icon: $(file -b /usr/share/icons/hicolor/128x128/apps/anafinen.png 2>&1)"
  echo "[$DISTRO] desktop entries: $(find /usr/share/applications -name 'anafinen*.desktop' | wc -l)"
}

status=0
case "$MODE" in
  test)
    as_builder "package/tools/check.sh gcc"
    status=$?
    cp /work/build/check-*.log /out/ 2>/dev/null ;;
  package)
    run_package
    status=$? ;;
  prepare)
    exit 0 ;;
  *)
    echo "unknown mode '$MODE'"
    exit 2 ;;
esac

chown -R builder:builder /out
tar -c -C /out . >&3
exit $status
