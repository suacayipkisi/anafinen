#!/usr/bin/env python3
# Copyright (c) 2026 Abdurrahman Konuk (professionally known as Ufuk Deniz Konuk)
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Cross-checks anaf_io against the reference VTK implementation (Python bindings):
#   1. files written by VTK (every legacy / XML variant) must be read by anaf_io exactly as VTK reads them;
#   2. files written by anaf_io must be read by VTK exactly as anaf_io describes them.
# Usage: vtkReferenceCheck.py <path to anaf_io_tool> <work directory>

import math
import os
import subprocess
import sys

import vtk
from vtk.util import numpy_support  # noqa: F401  (ensures numpy-backed arrays are available)

TOOL = sys.argv[1]
WORK = sys.argv[2]
os.makedirs(WORK, exist_ok=True)
failures = []


def fail(message):
    failures.append(message)
    print("  FAIL", message)


# ------------------------------------------------------------------ reference dataset

def awkward(i, salt):
    # Values must stay finite in float32 arrays (VTK cannot read "inf" back from ASCII files).
    pool = [0.1 + 0.2, 1.0 / 3.0, -123456.789012345, 6.02214076e23, 1e-17, 2.5, 1e30, -7.0 / 9.0, math.pi, -0.5]
    return pool[(i * 7 + salt * 3) % 10] * (1.0 + i * 1e-9)


STANDARD_CELLS = [
    (vtk.VTK_VERTEX, 1), (vtk.VTK_LINE, 2), (vtk.VTK_QUADRATIC_EDGE, 3), (vtk.VTK_TRIANGLE, 3),
    (vtk.VTK_QUADRATIC_TRIANGLE, 6), (vtk.VTK_QUAD, 4), (vtk.VTK_QUADRATIC_QUAD, 8), (vtk.VTK_BIQUADRATIC_QUAD, 9),
    (vtk.VTK_TETRA, 4), (vtk.VTK_QUADRATIC_TETRA, 10), (vtk.VTK_HEXAHEDRON, 8), (vtk.VTK_QUADRATIC_HEXAHEDRON, 20),
    (vtk.VTK_TRIQUADRATIC_HEXAHEDRON, 27), (vtk.VTK_WEDGE, 6), (vtk.VTK_QUADRATIC_WEDGE, 15), (vtk.VTK_PYRAMID, 5),
    (vtk.VTK_QUADRATIC_PYRAMID, 13),
]
COMPOSITE_CELLS = [(vtk.VTK_POLY_LINE, 4), (vtk.VTK_POLYGON, 5), (vtk.VTK_TRIANGLE_STRIP, 5), (vtk.VTK_PIXEL, 4),
                   (vtk.VTK_VOXEL, 8), (vtk.VTK_POLY_VERTEX, 3), (vtk.VTK_POLYGON, 4), (vtk.VTK_POLYGON, 3)]

NODES = 40


def make_grid(cells, float_points=False):
    grid = vtk.vtkUnstructuredGrid()
    points = vtk.vtkPoints()
    points.SetDataTypeToFloat() if float_points else points.SetDataTypeToDouble()
    for i in range(NODES):
        points.InsertNextPoint(awkward(i, 1), awkward(i, 2), i * 0.1)
    grid.SetPoints(points)
    for index, (cell_type, count) in enumerate(cells):
        ids = vtk.vtkIdList()
        for k in range(count):
            ids.InsertNextId((index * 3 + k) % NODES)
        grid.InsertNextCell(cell_type, ids)

    def add(data, name, array, components, size, salt):
        array.SetName(name)
        array.SetNumberOfComponents(components)
        array.SetNumberOfTuples(size)
        for t in range(size):
            for c in range(components):
                value = awkward(t * components + c, salt)
                if isinstance(array, (vtk.vtkIntArray, vtk.vtkUnsignedCharArray)):
                    value = (t * 7 + c) % 200
                array.SetComponent(t, c, value)
        data.AddArray(array)

    add(grid.GetPointData(), "Temperature", vtk.vtkDoubleArray(), 1, NODES, 1)
    add(grid.GetPointData(), "Velocity", vtk.vtkFloatArray(), 3, NODES, 2)
    add(grid.GetPointData(), "Index", vtk.vtkIntArray(), 1, NODES, 3)
    add(grid.GetPointData(), "Tensor9", vtk.vtkDoubleArray(), 9, NODES, 4)
    add(grid.GetCellData(), "Pressure", vtk.vtkDoubleArray(), 1, len(cells), 5)
    add(grid.GetCellData(), "Flags", vtk.vtkUnsignedCharArray(), 1, len(cells), 6)
    add(grid.GetCellData(), "Strain6", vtk.vtkDoubleArray(), 6, len(cells), 7)
    # Two eigenmodes in the anaf_io single-file convention, dataset-level field data and TimeValue.
    add(grid.GetPointData(), "Shape_Mode_001", vtk.vtkDoubleArray(), 3, NODES, 8)
    add(grid.GetPointData(), "Shape_Mode_002", vtk.vtkDoubleArray(), 3, NODES, 9)
    add(grid.GetFieldData(), "Shape_Mode_Values", vtk.vtkDoubleArray(), 1, 2, 10)
    add(grid.GetFieldData(), "NaturalFrequency", vtk.vtkDoubleArray(), 1, 2, 10)
    add(grid.GetFieldData(), "Matrix2", vtk.vtkDoubleArray(), 2, 3, 11)
    add(grid.GetFieldData(), "TimeValue", vtk.vtkDoubleArray(), 1, 1, 12)
    return grid


# ------------------------------------------------------------------ canonical descriptions

def describe_vtk(dataset):
    """points, {cell key: cell index}, arrays {(loc, name): (components, values)} as read by VTK."""
    points = [tuple(dataset.GetPoint(i)) for i in range(dataset.GetNumberOfPoints())]
    cells = []
    for c in range(dataset.GetNumberOfCells()):
        ids = vtk.vtkIdList()
        dataset.GetCellPoints(c, ids)
        cells.append((dataset.GetCellType(c), tuple(ids.GetId(k) for k in range(ids.GetNumberOfIds()))))
    arrays = {}
    for loc, data in (("N", dataset.GetPointData()), ("E", dataset.GetCellData()), ("G", dataset.GetFieldData())):
        for a in range(data.GetNumberOfArrays()):
            array = data.GetArray(a)
            if array is None:
                continue
            values = [array.GetComponent(t, c) for t in range(array.GetNumberOfTuples())
                      for c in range(array.GetNumberOfComponents())]
            arrays[(loc, array.GetName())] = (array.GetNumberOfComponents(), values)
    return points, cells, arrays


def describe_tool(path):
    result = subprocess.run([TOOL, "dump", path], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"anaf_io_tool dump failed for {path}: {result.stderr.strip()}")
    lines = result.stdout.splitlines()
    pos = 0
    count = int(lines[pos].split()[1]); pos += 1
    points = [tuple(float(v) for v in lines[pos + i].split()) for i in range(count)]
    pos += count
    count = int(lines[pos].split()[1]); pos += 1
    cells = []
    for i in range(count):
        parts = [int(v) for v in lines[pos + i].split()]
        cells.append((parts[0], tuple(parts[2:])))
    pos += count
    arrays = {}
    while pos < len(lines):
        if lines[pos].startswith("WARNING"):
            pos += 1
            continue
        _, loc, comps, size, name = lines[pos].split(" ", 4)
        values = [float(v) for v in lines[pos + 1].split()] if int(size) else []
        arrays[(loc, name)] = (int(comps), values)
        pos += 2
    return points, cells, arrays


def same_float(a, b):
    return a == b or (math.isnan(a) and math.isnan(b))


def compare(label, reference, actual, expected_cells=None, ignore_arrays=()):
    ref_points, ref_cells, ref_arrays = reference
    act_points, act_cells, act_arrays = actual
    if ref_points != act_points:
        fail(f"{label}: points differ")
        return
    wanted = expected_cells if expected_cells is not None else [(c, i) for i, c in enumerate(ref_cells)]
    # Cell order may differ (anaf_io groups cells by type): compare as multisets keyed by (type, ids).
    act_index = {}
    for i, cell in enumerate(act_cells):
        act_index.setdefault(cell, []).append(i)
    if sorted(c for c, _ in wanted) != sorted(act_cells):
        fail(f"{label}: cells differ ({len(wanted)} expected, {len(act_cells)} read)")
        missing = [c for c, _ in wanted if c not in act_index][:3]
        print("    first missing cells:", missing)
        return
    for (loc, name), (comps, values) in ref_arrays.items():
        if name in ignore_arrays:
            continue
        if (loc, name) not in act_arrays:
            fail(f"{label}: array {loc}:{name} missing")
            continue
        act_comps, act_values = act_arrays[(loc, name)]
        if act_comps != comps:
            fail(f"{label}: array {name} components {comps} vs {act_comps}")
            continue
        if loc in ("N", "G"):
            ok = len(values) == len(act_values) and all(same_float(x, y) for x, y in zip(values, act_values))
        else:
            ok = True
            used = {}
            for cell, source in wanted:
                slot = act_index[cell][used.get(cell, 0)]
                used[cell] = used.get(cell, 0) + 1
                expected = values[source * comps:(source + 1) * comps]
                got = act_values[slot * comps:(slot + 1) * comps]
                if not all(same_float(x, y) for x, y in zip(expected, got)):
                    ok = False
                    break
        if not ok:
            fail(f"{label}: array {loc}:{name} values differ")


def split_composites(cells):
    """Expected anaf_io decomposition of composite VTK cells, as (cell, source index)."""
    out = []
    for i, (cell_type, ids) in enumerate(cells):
        if cell_type == vtk.VTK_POLY_LINE:
            out += [((vtk.VTK_LINE, (ids[k], ids[k + 1])), i) for k in range(len(ids) - 1)]
        elif cell_type == vtk.VTK_POLY_VERTEX:
            out += [((vtk.VTK_VERTEX, (p,)), i) for p in ids]
        elif cell_type == vtk.VTK_POLYGON and len(ids) == 3:
            out.append(((vtk.VTK_TRIANGLE, ids), i))
        elif cell_type == vtk.VTK_POLYGON and len(ids) == 4:
            out.append(((vtk.VTK_QUAD, ids), i))
        elif cell_type == vtk.VTK_POLYGON:
            out += [((vtk.VTK_TRIANGLE, (ids[0], ids[k], ids[k + 1])), i) for k in range(1, len(ids) - 1)]
        elif cell_type == vtk.VTK_TRIANGLE_STRIP:
            for k in range(len(ids) - 2):
                tri = (ids[k], ids[k + 1], ids[k + 2]) if k % 2 == 0 else (ids[k + 1], ids[k], ids[k + 2])
                out.append(((vtk.VTK_TRIANGLE, tri), i))
        elif cell_type == vtk.VTK_PIXEL:
            out.append(((vtk.VTK_QUAD, (ids[0], ids[1], ids[3], ids[2])), i))
        elif cell_type == vtk.VTK_VOXEL:
            v = ids
            out.append(((vtk.VTK_HEXAHEDRON, (v[0], v[1], v[3], v[2], v[4], v[5], v[7], v[6])), i))
        else:
            out.append(((cell_type, ids), i))
    return out


def read_with_vtk(path):
    errors = []
    if path.endswith(".vtu"):
        reader = vtk.vtkXMLUnstructuredGridReader()
    else:
        reader = vtk.vtkUnstructuredGridReader()
        reader.ReadAllScalarsOn()
        reader.ReadAllVectorsOn()
        reader.ReadAllFieldsOn()
        reader.ReadAllTensorsOn()
    reader.AddObserver("ErrorEvent", lambda obj, event: errors.append(event))
    reader.SetFileName(path)
    reader.Update()
    if errors:
        raise RuntimeError(f"VTK reported errors reading {path}")
    return reader.GetOutput()


# ------------------------------------------------------------------ 1. files written by VTK

def write_legacy(grid, path, binary, version):
    writer = vtk.vtkUnstructuredGridWriter()
    writer.SetFileName(path)
    writer.SetInputData(grid)
    writer.SetFileTypeToBinary() if binary else writer.SetFileTypeToASCII()
    writer.SetFileVersion(version)
    writer.Write()


def write_xml(grid, path, mode, encode, compressor, header64, big_endian, pieces):
    writer = vtk.vtkXMLUnstructuredGridWriter()
    writer.SetFileName(path)
    writer.SetInputData(grid)
    {"ascii": writer.SetDataModeToAscii, "binary": writer.SetDataModeToBinary, "appended": writer.SetDataModeToAppended}[mode]()
    writer.SetEncodeAppendedData(encode)
    writer.SetCompressorTypeToZLib() if compressor else writer.SetCompressorTypeToNone()
    writer.SetHeaderTypeToUInt64() if header64 else writer.SetHeaderTypeToUInt32()
    writer.SetByteOrderToBigEndian() if big_endian else writer.SetByteOrderToLittleEndian()
    writer.SetNumberOfPieces(pieces)
    writer.Write()


print("1. reading files written by VTK", vtk.vtkVersion.GetVTKVersion())
cases = 0
for composite in (False, True):
    for float_points in (False, True):
        cells = STANDARD_CELLS + (COMPOSITE_CELLS if composite else [])
        grid = make_grid(cells, float_points)
        tag = f"{'mixed' if composite else 'std'}_{'f32' if float_points else 'f64'}"
        files = []
        for binary in (False, True):
            for version in (42, 51):
                path = os.path.join(WORK, f"ref_{tag}_legacy{version}_{'bin' if binary else 'asc'}.vtk")
                write_legacy(grid, path, binary, version)
                files.append(path)
        for mode in ("ascii", "binary", "appended"):
            for encode in ((False, True) if mode == "appended" else (True,)):
                for compressor in ((False, True) if mode != "ascii" else (False,)):
                    for header64 in (False, True):
                        for big_endian in ((False, True) if mode != "ascii" else (False,)):
                            name = f"ref_{tag}_{mode}{'_b64' if encode and mode == 'appended' else ''}" \
                                   f"{'_zlib' if compressor else ''}_{'h64' if header64 else 'h32'}{'_be' if big_endian else ''}.vtu"
                            path = os.path.join(WORK, name)
                            write_xml(grid, path, mode, encode, compressor, header64, big_endian, 1)
                            files.append(path)
        pieces_path = os.path.join(WORK, f"ref_{tag}_3pieces.vtu")
        write_xml(grid, pieces_path, "appended", False, True, True, False, 3)
        files.append(pieces_path)

        for path in files:
            cases += 1
            reference = describe_vtk(read_with_vtk(path))
            try:
                actual = describe_tool(path)
            except RuntimeError as error:
                fail(str(error))
                continue
            expected = split_composites(reference[1]) if composite else None
            if "3pieces" in path and not composite:
                expected = None
            compare(os.path.basename(path), reference, actual, expected)
print(f"   {cases} VTK-written files checked")

# ------------------------------------------------------------------ 2. files written by anaf_io, read by VTK

print("2. VTK reading files written by anaf_io")
source = os.path.join(WORK, "ref_std_f64_legacy51_asc.vtk")
variants = [("out_42_ascii.vtk", ["--vtk42"]), ("out_42_binary.vtk", ["--vtk42", "--binary"]),
            ("out_51_ascii.vtk", []), ("out_51_binary.vtk", ["--binary"]),
            ("out_ascii.vtu", []), ("out_binary.vtu", ["--binary"]), ("out_zlib.vtu", ["--binary", "--compress"])]
ours = describe_tool(source)
for name, flags in variants:
    path = os.path.join(WORK, name)
    result = subprocess.run([TOOL, "convert", source, path] + flags, capture_output=True, text=True)
    if result.returncode != 0:
        fail(f"{name}: convert failed: {result.stderr.strip()}")
        continue
    try:
        seen_by_vtk = describe_vtk(read_with_vtk(path))
    except RuntimeError as error:
        fail(str(error))
        continue
    # anaf_io adds NodeTag / ElementTag arrays for lossless round trips; they are extra, not a mismatch.
    compare(name, ours, seen_by_vtk, ignore_arrays=("NodeTag", "ElementTag"))
print(f"   {len(variants)} anaf_io-written files checked")

print(f"\n{len(failures)} failures")
sys.exit(1 if failures else 0)
