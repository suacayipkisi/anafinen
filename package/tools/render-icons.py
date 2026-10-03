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

# Regenerates the committed icon renders from the SVG sources in assets/icons/:
#   anafinen.png     128x128 from anafinen.svg (fallback when CMake finds no converter)
#   anafinen-32.png  32x32 from anafinen-small.svg (small window icon, same fallback role)
#   anafinen.ico     16/24/32 from anafinen-small.svg, 48/64/128/256 from anafinen.svg,
#                    every entry PNG-compressed (embedded into anafinen.exe by src/anafinen.rc)
# Run it after changing either SVG. Needs rsvg-convert (librsvg) or inkscape.

import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ICON_DIR = Path(__file__).resolve().parents[2] / "assets" / "icons"
LARGE_SVG = ICON_DIR / "anafinen.svg"
SMALL_SVG = ICON_DIR / "anafinen-small.svg"
SMALL_MAX = 32  # sizes up to this use the small-size variant
ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)


def render(svg: Path, size: int, out: Path) -> None:
    if shutil.which("rsvg-convert"):
        cmd = ["rsvg-convert", "--width", str(size), "--height", str(size), "--output", str(out), str(svg)]
    elif shutil.which("inkscape"):
        cmd = ["inkscape", str(svg), "-w", str(size), "-h", str(size), "-o", str(out)]
    else:
        sys.exit("render-icons: neither rsvg-convert nor inkscape found")
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def source_for(size: int) -> Path:
    return SMALL_SVG if size <= SMALL_MAX else LARGE_SVG


def write_ico(pngs: list[tuple[int, bytes]], out: Path) -> None:
    # ICONDIR + one ICONDIRENTRY per image; PNG payloads are valid ICO images since Vista.
    header = struct.pack("<HHH", 0, 1, len(pngs))
    offset = len(header) + 16 * len(pngs)
    entries = b""
    for size, data in pngs:
        # A width / height byte of 0 means 256.
        entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    out.write_bytes(header + entries + b"".join(data for _, data in pngs))


def main() -> None:
    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        pngs = []
        for size in ICO_SIZES:
            png = tmp_dir / f"{size}.png"
            render(source_for(size), size, png)
            pngs.append((size, png.read_bytes()))
        write_ico(pngs, ICON_DIR / "anafinen.ico")
        shutil.copyfile(tmp_dir / "128.png", ICON_DIR / "anafinen.png")
        shutil.copyfile(tmp_dir / "32.png", ICON_DIR / "anafinen-32.png")
    print(f"render-icons: wrote anafinen.ico, anafinen.png, anafinen-32.png in {ICON_DIR}")


if __name__ == "__main__":
    main()
