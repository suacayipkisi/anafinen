#!/usr/bin/env bash
#
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

# Builds the source RPM of package/fedora/anafinen.spec:
#   1. fetches the git submodules,
#   2. packs the tracked files of the checkout (submodules included, nothing untracked or ignored:
#      no build trees, no tests/ or information/) into
#      anafinen-<version>-alpha-source-with-submodules.tar.gz, the spec's Source0,
#   3. rpmbuild -bs.
# Copr runs it through .copr/Makefile; locally it gives the same SRPM (and the source archive of
# the GitHub release).
#
# Usage: package/fedora/make-srpm.sh [outdir]   (default: package/early-release-packages)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
SPEC="$SCRIPT_DIR/anafinen.spec"
OUTDIR="$(mkdir -p "${1:-$REPO_ROOT/package/early-release-packages}" && cd "${1:-$REPO_ROOT/package/early-release-packages}" && pwd)"

cd "$REPO_ROOT"

spec_version="$(sed -n 's/^%global upstream_version[[:space:]]\+//p' "$SPEC")"
prerelease="$(sed -n 's/^%global prerelease[[:space:]]\+//p' "$SPEC")"
cmake_version="$(sed -n 's/^project(anafinen VERSION \([0-9.]\+\).*/\1/p' CMakeLists.txt)"
if [[ -z "$spec_version" || "$spec_version" != "$cmake_version" ]]; then
  echo "make-srpm: spec version '$spec_version' differs from CMakeLists.txt '$cmake_version'" >&2
  exit 1
fi

name="anafinen-${spec_version}-${prerelease}"
archive="${name}-source-with-submodules.tar.gz"

git submodule update --init --recursive

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# Tracked files at the checked-out commit of this repository and of every submodule.
git ls-files -z --cached --recurse-submodules | while IFS= read -r -d '' f; do
  [[ -e "$f" || -L "$f" ]] && printf '%s\0' "$f"
done > "$work/files"

# Reproducible: sorted names, one owner, the commit time as every file's time.
commit_time="$(git log -1 --format=%ct)"
tar --create --null --no-recursion --files-from="$work/files" \
    --transform="s,^,${name}/," --sort=name --owner=0 --group=0 --numeric-owner \
    --mtime="@${commit_time}" --format=gnu \
  | gzip -n -9 > "$work/$archive"

rpmbuild -bs "$SPEC" \
  --define "_sourcedir $work" \
  --define "_srcrpmdir $OUTDIR" \
  --define "dist %{nil}"

cp "$work/$archive" "$OUTDIR/"
echo "make-srpm: $OUTDIR/$archive"
ls -1 "$OUTDIR"/anafinen-"${spec_version}"*.src.rpm
