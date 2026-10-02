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

# Builds and tests the project and prints only a short summary: warnings, errors and
# failed tests. Full logs stay in <build dir>/check-*.log.
#
# Usage: package/tools/check.sh [gcc|clang|mingw|all]...   (default: gcc)
#   gcc    Linux GCC, build dir "build" (the normal development tree), ctest
#   clang  Linux Clang, build dir "build-clang", ctest
#   mingw  Windows cross-build (MinGW), build dir "build-mingw", tests under Wine
#
# Environment: GMSH_SDK_DIR (Windows Gmsh SDK, default ~/Projects/gmsh-sdk),
#              MAX_LINES (warning / error lines shown per target, default 30).

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
GMSH_SDK_DIR="${GMSH_SDK_DIR:-$HOME/Projects/gmsh-sdk}"
MINGW_SYSROOT_BIN="/usr/x86_64-w64-mingw32/sys-root/mingw/bin"
MAX_LINES="${MAX_LINES:-30}"
FAILED=0

# Prints unique compiler diagnostics from our sources (third-party trees filtered out).
diagnostics() {
  grep -E "(warning|error):" "$1" | grep -vE "/(_deps|external)/" | sed "s|$REPO_ROOT/||" | sort -u
}

# $1 name, $2 build dir, remaining: extra configure arguments.
build() {
  local name="$1" dir="$REPO_ROOT/$2"; shift 2
  local log="$dir/check-build.log"
  mkdir -p "$dir"
  if [[ ! -f "$dir/CMakeCache.txt" ]]; then
    cmake -S "$REPO_ROOT" -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DANAFINEN_BUILD_TESTS=ON "$@" > "$dir/check-configure.log" 2>&1 \
      || { echo "[$name] configure FAILED (see ${dir#$REPO_ROOT/}/check-configure.log)"; tail -5 "$dir/check-configure.log"; FAILED=1; return 1; }
  else
    cmake -S "$REPO_ROOT" -B "$dir" > "$dir/check-configure.log" 2>&1 \
      || { echo "[$name] configure FAILED"; tail -5 "$dir/check-configure.log"; FAILED=1; return 1; }
  fi

  cmake --build "$dir" -- -k 0 > "$log" 2>&1
  local status=$?
  local found count
  found="$(diagnostics "$log")"
  count=$(grep -c . <<< "$found")
  if [[ $status -ne 0 ]]; then
    echo "[$name] build FAILED, $count diagnostics (log: ${log#$REPO_ROOT/})"
    FAILED=1
  else
    echo "[$name] build OK, $count warnings"
  fi
  [[ -n "$found" ]] && head -n "$MAX_LINES" <<< "$found"
  return $status
}

# $1 name, $2 build dir: ctest summary plus the output of failed tests.
run_ctest() {
  local name="$1" dir="$REPO_ROOT/$2"
  local log="$dir/check-test.log"
  (cd "$dir" && ctest --output-on-failure > "$log" 2>&1)
  local status=$?
  echo "[$name] $(grep -E "tests passed|tests failed" "$log" | tail -1)"
  if [[ $status -ne 0 ]]; then
    grep -E "FAIL|Failed|\*\*\*" "$log" | head -n "$MAX_LINES"
    FAILED=1
  fi
}

check_gcc() {
  build gcc build && run_ctest gcc build
}

check_clang() {
  CC=clang CXX=clang++ build clang build-clang && run_ctest clang build-clang
}

check_mingw() {
  local dll
  dll="$(ls "$GMSH_SDK_DIR"/lib/gmsh-*.dll 2>/dev/null | head -1)"
  [[ -z "$dll" ]] && { echo "[mingw] Gmsh SDK not found in $GMSH_SDK_DIR (set GMSH_SDK_DIR)"; FAILED=1; return 1; }
  build mingw build-mingw \
    -DCMAKE_TOOLCHAIN_FILE=/usr/share/mingw/toolchain-mingw64.cmake \
    -DGMSH_SDK_DIR="$GMSH_SDK_DIR" \
    -DGMSH_INCLUDE_DIR="$GMSH_SDK_DIR/include" \
    -DGMSH_LIBRARY="$GMSH_SDK_DIR/lib/gmsh.dll.lib" \
    -DGMSH_DLL="$dll" || return 1

  if ! command -v wine > /dev/null; then
    echo "[mingw] wine not installed, tests skipped"
    return 0
  fi
  local test
  for test in anaf_core_tests anaf_io_tests anaf_truss_io_tests; do
    local log="$REPO_ROOT/build-mingw/check-$test.log"
    (cd "$REPO_ROOT/build-mingw/tests" && WINEDEBUG=-all WINEPATH="$MINGW_SYSROOT_BIN;$GMSH_SDK_DIR/lib" \
      timeout 600 wine "$test.exe" > "$log" 2>&1)
    local status=$?
    echo "[mingw] $test (Wine): $(grep -E "test cases" "$log" | tail -1)"
    if [[ $status -ne 0 ]]; then
      grep -E "FAIL" "$log" | head -n "$MAX_LINES"
      FAILED=1
    fi
  done
}

targets=("$@")
[[ ${#targets[@]} -eq 0 ]] && targets=(gcc)
[[ " ${targets[*]} " == *" all "* ]] && targets=(gcc clang mingw)

for target in "${targets[@]}"; do
  case "$target" in
    gcc) check_gcc ;;
    clang) check_clang ;;
    mingw) check_mingw ;;
    *) echo "unknown target '$target' (gcc, clang, mingw, all)"; exit 2 ;;
  esac
done

[[ $FAILED -eq 0 ]] && echo "ALL OK" || echo "CHECK FAILED"
exit $FAILED
