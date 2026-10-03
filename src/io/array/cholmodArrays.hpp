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

// ArrayFile adapters for SuiteSparse CHOLMOD. Header-only: anaf_io never links CHOLMOD. Include
// it only from code built with ANAFINEN_HAS_CHOLMOD (anaf_core and its tests), which has the
// CHOLMOD include directory and libraries.
//   cholmod_sparse (packed, double, real or complex, int32 / int64 indices) -> csc_matrix;
//     a symmetric matrix (stype != 0) stores one triangle only and keeps "cholmod-stype"
//   cholmod_dense (double, real or complex)                                  -> rank-2 dataset
// Reading allocates through the 32-bit index API (cholmod_*, the one Eigen's CholmodSupport
// uses with int indices); free the result with cholmod_free_sparse / cholmod_free_dense.

#include "arrayFile.hpp"

#include <cholmod.h>

#include <algorithm>
#include <complex>
#include <format>
#include <limits>
#include <suitesparse/cholmod.h>

namespace anaf::IO::ARRAY {

  namespace cholmodDetail {

    inline constexpr const char* stypeKey = "cholmod-stype";

    inline ArrayError error(const ArrayError::Code code, const std::string_view path, const std::string_view message) {
      return {code, std::format("'{}': {}", path, message)};
    }

    template <class Index>
    std::vector<std::int64_t> widen(const void* data, const std::size_t count) {
      const auto* values = static_cast<const Index*>(data);
      return std::vector<std::int64_t>(values, values + count);
    }

    template <class Value>
    Result<cholmod_sparse*> toCholmodSparse(const CompressedMatrix<Value>& stored, const int stype, const int xtype,
                                            cholmod_common& common, const std::string_view path) {
      constexpr auto intMax = static_cast<std::uint64_t>(std::numeric_limits<int>::max());
      if (stored.rows > intMax || stored.cols > intMax || stored.values.size() > intMax) {
        return std::unexpected(error(ArrayError::Code::ShapeMismatch, path, "too large for 32-bit CHOLMOD indices"));
      }
      // A CSR matrix is the CSC storage of its transpose: build that, then transpose it back.
      const bool csr = stored.layout == SparseLayout::Csr;
      const std::size_t nrow = csr ? stored.cols : stored.rows;
      const std::size_t ncol = csr ? stored.rows : stored.cols;
      cholmod_sparse* matrix = cholmod_allocate_sparse(nrow, ncol, stored.values.size(), 0, 1, csr ? 0 : stype, xtype, &common);
      if (!matrix) return std::unexpected(error(ArrayError::Code::BackendError, path, "cholmod_allocate_sparse failed"));
      std::copy(stored.pointers.begin(), stored.pointers.end(), static_cast<int*>(matrix->p));
      std::copy(stored.indices.begin(), stored.indices.end(), static_cast<int*>(matrix->i));
      std::copy(stored.values.begin(), stored.values.end(), static_cast<Value*>(matrix->x));
      if (!csr) return matrix;

      cholmod_sparse* transposed = cholmod_transpose(matrix, 1, &common); // 1: values, no conjugate
      cholmod_free_sparse(&matrix, &common);
      if (!transposed) return std::unexpected(error(ArrayError::Code::BackendError, path, "cholmod_transpose failed"));
      transposed->stype = stype;
      return transposed;
    }

  } // namespace cholmodDetail end

  inline Result<void> write(ArrayFile& file, const std::string_view path, const cholmod_sparse& matrix, const WriteOptions& options = {}) {
    using cholmodDetail::error;
    if (!matrix.packed) return std::unexpected(error(ArrayError::Code::InvalidData, path, "unpacked cholmod_sparse is not supported"));
    if (matrix.dtype != CHOLMOD_DOUBLE || (matrix.xtype != CHOLMOD_REAL && matrix.xtype != CHOLMOD_COMPLEX)) {
      return std::unexpected(error(ArrayError::Code::TypeMismatch, path, "only double real / complex (interleaved) matrices are supported"));
    }
    if (matrix.itype != CHOLMOD_INT && matrix.itype != CHOLMOD_LONG) {
      return std::unexpected(error(ArrayError::Code::TypeMismatch, path, "unsupported CHOLMOD index type"));
    }
    const bool wide = matrix.itype == CHOLMOD_LONG;
    const auto pointers = wide ? cholmodDetail::widen<std::int64_t>(matrix.p, matrix.ncol + 1) : cholmodDetail::widen<int>(matrix.p, matrix.ncol + 1);
    const auto nonZeros = static_cast<std::size_t>(pointers.back());
    const auto indices = wide ? cholmodDetail::widen<std::int64_t>(matrix.i, nonZeros) : cholmodDetail::widen<int>(matrix.i, nonZeros);

    const auto writeAs = [&]<class Value>() {
      const CompressedView<Value> view{
        .rows = matrix.nrow,
        .cols = matrix.ncol,
        .layout = SparseLayout::Csc,
        .values = std::span<const Value>(static_cast<const Value*>(matrix.x), nonZeros),
        .indices = indices,
        .pointers = pointers,
      };
      return file.writeSparse<Value>(path, view, options);
    };
    auto written = matrix.xtype == CHOLMOD_COMPLEX ? writeAs.template operator()<std::complex<double>>()
                                                   : writeAs.template operator()<double>();
    if (!written || matrix.stype == 0) return written;
    return file.setAttribute(path, cholmodDetail::stypeKey, std::int64_t{matrix.stype});
  }

  inline Result<void> write(ArrayFile& file, const std::string_view path, const cholmod_dense& matrix, const WriteOptions& options = {}) {
    using cholmodDetail::error;
    if (matrix.dtype != CHOLMOD_DOUBLE || (matrix.xtype != CHOLMOD_REAL && matrix.xtype != CHOLMOD_COMPLEX)) {
      return std::unexpected(error(ArrayError::Code::TypeMismatch, path, "only double real / complex (interleaved) matrices are supported"));
    }
    const std::uint64_t shape[] = {matrix.nrow, matrix.ncol};
    // Column-major with leading dimension d -> row-major.
    const auto writeAs = [&]<class Value>() {
      const auto* source = static_cast<const Value*>(matrix.x);
      std::vector<Value> values(matrix.nrow * matrix.ncol);
      for (std::size_t r = 0; r < matrix.nrow; ++r) {
        for (std::size_t c = 0; c < matrix.ncol; ++c) values[r * matrix.ncol + c] = source[c * matrix.d + r];
      }
      return file.writeDense<Value>(path, values, shape, options);
    };
    return matrix.xtype == CHOLMOD_COMPLEX ? writeAs.template operator()<std::complex<double>>() : writeAs.template operator()<double>();
  }

  // Real files give CHOLMOD_REAL, complex files CHOLMOD_COMPLEX; csr_matrix files are transposed
  // into CSC. The "cholmod-stype" attribute, when present, restores stype.
  inline Result<cholmod_sparse*> readCholmodSparse(const ArrayFile& file, const std::string_view path, cholmod_common& common) {
    auto found = file.info(path);
    if (!found) return std::unexpected(std::move(found.error()));
    int stype = 0;
    if (auto attribute = file.attribute(path, cholmodDetail::stypeKey)) {
      if (const auto* value = std::get_if<std::int64_t>(&*attribute)) stype = static_cast<int>(*value);
    }
    const bool complex = found->scalar == ScalarType::Complex64 || found->scalar == ScalarType::Complex128;
    if (complex) {
      auto stored = file.readSparse<std::complex<double>>(path);
      if (!stored) return std::unexpected(std::move(stored.error()));
      return cholmodDetail::toCholmodSparse(*stored, stype, CHOLMOD_COMPLEX, common, path);
    }
    auto stored = file.readSparse<double>(path);
    if (!stored) return std::unexpected(std::move(stored.error()));
    return cholmodDetail::toCholmodSparse(*stored, stype, CHOLMOD_REAL, common, path);
  }

  // Rank-2 datasets keep their shape, a rank-1 dataset becomes one column.
  inline Result<cholmod_dense*> readCholmodDense(const ArrayFile& file, const std::string_view path, cholmod_common& common) {
    auto found = file.info(path);
    if (!found) return std::unexpected(std::move(found.error()));
    const bool complex = found->scalar == ScalarType::Complex64 || found->scalar == ScalarType::Complex128;

    const auto readAs = [&]<class Value>(const int xtype) -> Result<cholmod_dense*> {
      auto stored = file.readDense<Value>(path);
      if (!stored) return std::unexpected(std::move(stored.error()));
      if (stored->shape.size() != 1 && stored->shape.size() != 2) {
        return std::unexpected(cholmodDetail::error(ArrayError::Code::ShapeMismatch, path, "rank must be 1 or 2"));
      }
      const std::size_t nrow = stored->shape[0];
      const std::size_t ncol = stored->shape.size() == 2 ? stored->shape[1] : 1;
      cholmod_dense* matrix = cholmod_allocate_dense(nrow, ncol, nrow, xtype, &common);
      if (!matrix) return std::unexpected(cholmodDetail::error(ArrayError::Code::BackendError, path, "cholmod_allocate_dense failed"));
      auto* target = static_cast<Value*>(matrix->x);
      for (std::size_t r = 0; r < nrow; ++r) {
        for (std::size_t c = 0; c < ncol; ++c) target[c * nrow + r] = stored->values[r * ncol + c];
      }
      return matrix;
    };
    return complex ? readAs.template operator()<std::complex<double>>(CHOLMOD_COMPLEX) : readAs.template operator()<double>(CHOLMOD_REAL);
  }

} // namespace anaf::IO::ARRAY end
