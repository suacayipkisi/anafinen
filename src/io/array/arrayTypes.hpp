// Copyright (c) 2026 Abdurrahman Konuk (professionally known as Ufuk Deniz Konuk)
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

// Types of the binary array store (HDF5 files with matrices, vectors and tensors). This header
// does not include hdf5.h: anaf_io links HDF5 PRIVATE, callers only see these types.
//
// Layout rules (readable by h5py / NumPy / SciPy without conversion):
//   dense      one dataset, row-major (C order), shape = dataset dimensions; rank 0 = scalar
//   sparse     one group with the datasets "data", "indices", "indptr" and the attributes
//              "encoding-type" ("csc_matrix" / "csr_matrix"), "encoding-version" ("0.1.0"),
//              "shape" (int64[2]); the anndata / scipy.sparse convention
//   complex    compound {r, i} of float / double (the h5py convention)

#include <complex>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace anaf::IO::ARRAY {

  enum class ScalarType { Float32, Float64, Int32, Int64, UInt64, Complex64, Complex128 };

  template <class T> struct ScalarTraits;
  template <> struct ScalarTraits<float> { static constexpr ScalarType type = ScalarType::Float32; };
  template <> struct ScalarTraits<double> { static constexpr ScalarType type = ScalarType::Float64; };
  template <> struct ScalarTraits<std::int32_t> { static constexpr ScalarType type = ScalarType::Int32; };
  template <> struct ScalarTraits<std::int64_t> { static constexpr ScalarType type = ScalarType::Int64; };
  template <> struct ScalarTraits<std::uint64_t> { static constexpr ScalarType type = ScalarType::UInt64; };
  template <> struct ScalarTraits<std::complex<float>> { static constexpr ScalarType type = ScalarType::Complex64; };
  template <> struct ScalarTraits<std::complex<double>> { static constexpr ScalarType type = ScalarType::Complex128; };

  template <class T>
  concept Scalar = requires { ScalarTraits<T>::type; };

  std::string_view scalarTypeName(ScalarType type) noexcept;

  // A stored type can be read into a requested type when they are equal or the conversion is a
  // lossless widening (Float32 -> Float64, Int32 -> Int64, Complex64 -> Complex128).
  bool canReadAs(ScalarType stored, ScalarType requested) noexcept;

  enum class ObjectKind { Group, Dense, Sparse };

  // Csc: pointers per column, indices are row indices (Eigen ColMajor, CHOLMOD, scipy csc_matrix).
  // Csr: pointers per row, indices are column indices (Eigen RowMajor, scipy csr_matrix).
  enum class SparseLayout { Csc, Csr };

  struct ArrayInfo {
    ObjectKind kind{ObjectKind::Group};
    std::optional<ScalarType> scalar;   // empty for groups and unsupported element types
    std::vector<std::uint64_t> shape;   // dense: dataset dimensions; sparse: {rows, cols}
    SparseLayout layout{SparseLayout::Csc};
    std::uint64_t nonZeros{0};          // sparse only
  };

  struct ArrayError {
    enum class Code {
      FileNotFound,   // the file does not exist
      InvalidFile,    // not an HDF5 file
      NotFound,       // no object at the given path
      AlreadyExists,  // write without overwrite to an existing path
      KindMismatch,   // e.g. readDense on a sparse group
      TypeMismatch,   // stored element type cannot be read as the requested one
      ShapeMismatch,  // rank / size differs from what the caller needs
      InvalidData,    // inconsistent arguments or file content (sparse pointers, sizes)
      ReadOnly,       // write through a file opened read-only
      BackendError    // HDF5 reported an error (message holds its error stack)
    };
    Code code{Code::BackendError};
    std::string message;
  };

  template <class T>
  using Result = std::expected<T, ArrayError>;

  // Row-major dense array of any rank; values.size() == product of shape (1 for rank 0).
  template <Scalar T>
  struct DenseArray {
    std::vector<std::uint64_t> shape;
    std::vector<T> values;
  };

  // Compressed sparse matrix. outer = cols (Csc) or rows (Csr):
  // pointers.size() == outer + 1, pointers[0] == 0, pointers.back() == values.size() == indices.size().
  template <Scalar T>
  struct CompressedMatrix {
    std::uint64_t rows{0};
    std::uint64_t cols{0};
    SparseLayout layout{SparseLayout::Csc};
    std::vector<T> values;
    std::vector<std::int64_t> indices;
    std::vector<std::int64_t> pointers;
  };

  // Non-owning view of a compressed matrix for writing.
  template <Scalar T>
  struct CompressedView {
    std::uint64_t rows{0};
    std::uint64_t cols{0};
    SparseLayout layout{SparseLayout::Csc};
    std::span<const T> values;
    std::span<const std::int64_t> indices;
    std::span<const std::int64_t> pointers;
  };

  // Integers are stored as int64, reals as double, strings as variable-length UTF-8.
  using AttributeValue = std::variant<std::int64_t, double, std::string, std::vector<std::int64_t>, std::vector<double>>;

  struct WriteOptions {
    bool overwrite{true};   // replace an existing object at the path (its file space is not reclaimed)
    int deflateLevel{0};    // 0 = contiguous, 1..9 = chunked + zlib (deflate filter)
  };

} // namespace anaf::IO::ARRAY end
