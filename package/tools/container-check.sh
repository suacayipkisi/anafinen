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

# Builds, tests or packages the current working tree (uncommitted changes included) inside
# clean Debian / Arch containers. Images are built on first use from package/tools/containers/*.Dockerfile.
# Logs and packages land in build-containers/<distro>/.
#
# Usage: package/tools/container-check.sh [--rebuild] [debian|arch|all] [test|package|shell]
#   (defaults: all test)
#   test     package/tools/check.sh gcc: build with tests, ctest, short summary
#   package  package/package.sh, then install the package and check ldd / icon / desktop entry
#   shell    interactive shell as the build user, source in /work (nothing is copied back)
#   --rebuild  rebuild the image (picks up distro updates and Dockerfile changes)
#
# Environment: CONTAINER_ENGINE (podman or docker, default: podman when installed).
# ccache persists between runs in the volumes anafinen-ccache-<distro>.

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CONTAINER_DIR="$REPO_ROOT/package/tools/containers"
[[ -f "$CONTAINER_DIR/inside.sh" ]] || { echo "$CONTAINER_DIR/inside.sh not found"; exit 2; }

if [[ -n "${CONTAINER_ENGINE:-}" ]]; then
  ENGINE="$CONTAINER_ENGINE"
elif command -v podman > /dev/null; then
  ENGINE=podman
else
  ENGINE=docker
fi
command -v "$ENGINE" > /dev/null || { echo "container engine '$ENGINE' not found"; exit 2; }

REBUILD=0
DISTROS=()
MODE=test
for arg in "$@"; do
  case "$arg" in
    --rebuild) REBUILD=1 ;;
    debian|arch) DISTROS+=("$arg") ;;
    all) DISTROS+=(debian arch) ;;
    test|package|shell) MODE="$arg" ;;
    *) echo "unknown argument '$arg'"; sed -n '/^# Usage/,/^# ccache/p' "${BASH_SOURCE[0]}"; exit 2 ;;
  esac
done
[[ ${#DISTROS[@]} -eq 0 ]] && DISTROS=(debian arch)
[[ "$MODE" == shell && ${#DISTROS[@]} -ne 1 ]] && { echo "shell needs exactly one distro"; exit 2; }

# Tracked files (submodules included) plus untracked, non-ignored files of the working tree,
# and the same for the test repository in tests/ when it is there (git-ignored in this one).
# Build directories and .git stay out, so the container always starts from a clean tree.
source_tar() {
  (cd "$REPO_ROOT" && {
    git ls-files -z --cached --recurse-submodules
    git ls-files -z --others --exclude-standard
    if [[ -e tests/.git ]]; then
      git -C tests ls-files -z --cached --others --exclude-standard | while IFS= read -r -d '' f; do
        printf 'tests/%s\0' "$f"
      done
    fi
  } | while IFS= read -r -d '' f; do
    [[ -e "$f" || -L "$f" ]] && printf '%s\0' "$f"
  done | tar -c --null --no-recursion -T -)
}

ensure_image() {
  local distro="$1" image="anafinen-build:$1"
  if [[ $REBUILD -eq 1 ]] || ! "$ENGINE" image inspect "$image" > /dev/null 2>&1; then
    echo "[$distro] building image $image"
    local args=()
    [[ $REBUILD -eq 1 ]] && args+=(--pull --no-cache)
    "$ENGINE" build "${args[@]}" -t "$image" -f "$CONTAINER_DIR/$distro.Dockerfile" "$CONTAINER_DIR" \
      > "$REPO_ROOT/build-containers/$distro-image.log" 2>&1 \
      || { echo "[$distro] image build FAILED (log: build-containers/$distro-image.log)"; tail -n 15 "$REPO_ROOT/build-containers/$distro-image.log"; return 1; }
  fi
}

run_distro() {
  local distro="$1" out="$REPO_ROOT/build-containers/$1"
  ensure_image "$distro" || return 1
  local run=("$ENGINE" run --rm -v "anafinen-ccache-$distro:/ccache" -e MAX_LINES="${MAX_LINES:-30}")

  if [[ "$MODE" == shell ]]; then
    local name="anafinen-shell-$distro-$$"
    "${run[@]}" -d --init --name "$name" "anafinen-build:$distro" sleep infinity > /dev/null || return 1
    source_tar | "$ENGINE" exec -i "$name" bash -c "$(cat "$CONTAINER_DIR/inside.sh")" inside.sh "$distro" prepare
    "$ENGINE" exec -it -u builder -w /work "$name" bash
    "$ENGINE" rm -f "$name" > /dev/null
    return 0
  fi

  rm -rf "$out" && mkdir -p "$out"
  echo "[$distro] $MODE"
  source_tar | "${run[@]}" -i "anafinen-build:$distro" \
    bash -c "$(cat "$CONTAINER_DIR/inside.sh")" inside.sh "$distro" "$MODE" | tar -x -C "$out"
  local status=${PIPESTATUS[1]} untar=${PIPESTATUS[2]}
  [[ $untar -ne 0 ]] && { echo "[$distro] no results came back from the container"; status=1; }
  echo "[$distro] output: build-containers/$distro/ ($(ls "$out" | tr '\n' ' '))"
  return "$status"
}

mkdir -p "$REPO_ROOT/build-containers"
FAILED=0
for distro in "${DISTROS[@]}"; do
  run_distro "$distro" || FAILED=1
done

[[ "$MODE" == shell ]] && exit $FAILED
[[ $FAILED -eq 0 ]] && echo "ALL OK" || echo "CONTAINER CHECK FAILED"
exit $FAILED
