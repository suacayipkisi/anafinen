#!/usr/bin/env python3
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

# Generates the light UI icon set from the hand-written dark set:
#   assets/icons/ui/dark/*.svg  ->  assets/icons/ui/light/*.svg
# Every palette color of the dark set is swapped for its light counterpart (exact hex match).
# White, the result colormap and the dark mark on amber are shared by both sets and stay as they are.
# A color that is in neither table is an error, so a new color cannot slip into one set only.
# Never edit light/ by hand: change dark/, run this script and commit both.
#
# Usage: package/tools/icon-variants.py [--check]
#   --check  only verify that light/ matches what would be generated (exit 1 otherwise)

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DARK = ROOT / "assets" / "icons" / "ui" / "dark"
LIGHT = ROOT / "assets" / "icons" / "ui" / "light"

# dark -> light (token names as in the design notes)
PALETTE = {
  "#dce1e8": "#2f3742",  # ink
  "#8f9aa8": "#7a8594",  # inkSoft
  "#4a5462": "#ffffff",  # paper
  "#a9bdd4": "#d3deeb",  # faceTop
  "#7890ae": "#a7b9cf",  # faceLeft
  "#546a86": "#7f95b1",  # faceRight
  "#4da3ff": "#1c74d9",  # blue
  "#2d5f96": "#cfe3fa",  # blueSoft
  "#36c5d6": "#0e9bad",  # cyan
  "#48c78e": "#1c9a5f",  # green
  "#f5b83d": "#cc8500",  # amber
  "#f2645c": "#d23f37",  # red
  "#a98bfa": "#7452db",  # violet
  "#1f4f86": "#0f4c99",  # blueDeep
}

SHARED = {
  "#ffffff",  # white highlights and marks
  "#1e2228",  # dark mark on amber
  "#2f6bff", "#22c3e6", "#3dd68c", "#f5d33d", "#f25c54",  # result colormap
}

COLOR = re.compile(r"#[0-9a-fA-F]{6}\b")


def convert(text: str, name: str) -> str:
  def swap(match: re.Match) -> str:
    color = match.group(0).lower()
    if color in PALETTE:
      return PALETTE[color]
    if color in SHARED:
      return color
    raise SystemExit(f"{name}: color {color} is not in the icon palette")

  body = COLOR.sub(swap, text)
  return body.replace("(dark set; light/ is generated", "(light set, GENERATED from dark/", 1)


def main() -> int:
  check = "--check" in sys.argv[1:]
  sources = sorted(DARK.glob("*.svg"))
  if not sources:
    print(f"no icons in {DARK}", file=sys.stderr)
    return 1

  LIGHT.mkdir(parents=True, exist_ok=True)
  expected = {src.name: convert(src.read_text(encoding="utf-8"), src.name) for src in sources}
  stale = [p for p in LIGHT.glob("*.svg") if p.name not in expected]

  if check:
    bad = [n for n, t in expected.items() if not (LIGHT / n).exists() or (LIGHT / n).read_text(encoding="utf-8") != t]
    bad += [p.name for p in stale]
    for n in bad:
      print(f"out of date: light/{n}", file=sys.stderr)
    return 1 if bad else 0

  for n, t in expected.items():
    (LIGHT / n).write_text(t, encoding="utf-8")
  for p in stale:
    p.unlink()
  print(f"{len(expected)} light icons written, {len(stale)} stale removed")
  return 0


if __name__ == "__main__":
  sys.exit(main())
