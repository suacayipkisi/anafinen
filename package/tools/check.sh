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
# Usage: package/tools/check.sh [gcc|clang|all]...   (default: gcc)
#   gcc    Linux GCC, build dir "build" (the normal development tree), ctest
#   clang  Linux Clang, build dir "build-clang", ctest
# Windows is checked natively with MSVC (package/package-windows.ps1), not cross-compiled.
#
# Environment: MAX_LINES (warning / error lines shown per target, default 30).

set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
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
    cmake -S "$REPO_ROOT" -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE=Release -DANAFINEN_BUILD_TESTS=ON -DANAFINEN_BUILD_TOOLS=ON "$@" > "$dir/check-configure.log" 2>&1 \
      || { echo "[$name] configure FAILED (see ${dir#$REPO_ROOT/}/check-configure.log)"; tail -5 "$dir/check-configure.log"; FAILED=1; return 1; }
  else
    # An existing tree may have been configured without tests (e.g. by hand); turn them on.
    cmake -S "$REPO_ROOT" -B "$dir" -DANAFINEN_BUILD_TESTS=ON -DANAFINEN_BUILD_TOOLS=ON > "$dir/check-configure.log" 2>&1 \
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
  if grep -q "No tests were found" "$log"; then
    echo "[$name] no tests were found (ANAFINEN_BUILD_TESTS off, or no test repository in tests/?)"
    FAILED=1
    return
  fi
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

targets=("$@")
[[ ${#targets[@]} -eq 0 ]] && targets=(gcc)
[[ " ${targets[*]} " == *" all "* ]] && targets=(gcc clang)

for target in "${targets[@]}"; do
  case "$target" in
    gcc) check_gcc ;;
    clang) check_clang ;;
    *) echo "unknown target '$target' (gcc, clang, all)"; exit 2 ;;
  esac
done

[[ $FAILED -eq 0 ]] && echo "ALL OK" || echo "CHECK FAILED"
exit $FAILED
