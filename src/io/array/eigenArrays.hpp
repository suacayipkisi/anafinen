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

// ArrayFile adapters for Eigen. Header-only: anaf_io itself does not depend on Eigen, the target
// that includes this header links Eigen3::Eigen (anaf_core does).
//   vectors (compile-time Rows or Cols == 1) -> rank-1 dataset
//   matrices / 2-D arrays                    -> rank-2 dataset {rows, cols}, row-major on disk
//                                               (ColMajor objects are transposed while copying)
//   Eigen::SparseMatrix                      -> csc_matrix (ColMajor) / csr_matrix (RowMajor)

#include "arrayFile.hpp"

#include <Eigen/Core>
#include <Eigen/SparseCore>

#include <format>
#include <limits>

namespace anaf::IO::ARRAY {

  template <class Derived>
    requires Scalar<typename Derived::Scalar>
  Result<void> write(ArrayFile& file, const std::string_view path, const Eigen::DenseBase<Derived>& object, const WriteOptions& options = {}) {
    using Value = typename Derived::Scalar;
    constexpr bool directAccess = (Derived::Flags & Eigen::DirectAccessBit) != 0;
    const auto& derived = object.derived();
    if constexpr (Derived::IsVectorAtCompileTime) {
      const std::uint64_t shape[] = {static_cast<std::uint64_t>(derived.size())};
      if constexpr (directAccess) {
        if (derived.innerStride() == 1) {
          return file.writeDense<Value>(path, std::span<const Value>(derived.data(), static_cast<std::size_t>(derived.size())), shape, options);
        }
      }
      const Eigen::Matrix<Value, Eigen::Dynamic, 1> values = derived.matrix().reshaped();
      return file.writeDense<Value>(path, std::span<const Value>(values.data(), static_cast<std::size_t>(values.size())), shape, options);
    }
    else {
      const std::uint64_t shape[] = {static_cast<std::uint64_t>(derived.rows()), static_cast<std::uint64_t>(derived.cols())};
      if constexpr (directAccess && Derived::IsRowMajor) {
        if (derived.innerStride() == 1 && derived.outerStride() == derived.cols()) {
          return file.writeDense<Value>(path, std::span<const Value>(derived.data(), static_cast<std::size_t>(derived.size())), shape, options);
        }
      }
      const Eigen::Matrix<Value, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> rowMajor = derived.matrix();
      return file.writeDense<Value>(path, std::span<const Value>(rowMajor.data(), static_cast<std::size_t>(rowMajor.size())), shape, options);
    }
  }

  template <class Value, int Options, class StorageIndex>
    requires Scalar<Value>
  Result<void> write(ArrayFile& file, const std::string_view path, const Eigen::SparseMatrix<Value, Options, StorageIndex>& matrix,
                     const WriteOptions& options = {}) {
    using Matrix = Eigen::SparseMatrix<Value, Options, StorageIndex>;
    // An uncompressed matrix (after insert() without makeCompressed()) has gaps between columns.
    Matrix compressedCopy;
    const Matrix* source = &matrix;
    if (!matrix.isCompressed()) {
      compressedCopy = matrix;
      compressedCopy.makeCompressed();
      source = &compressedCopy;
    }
    const auto nonZeros = static_cast<std::size_t>(source->nonZeros());
    const auto outer = static_cast<std::size_t>(source->outerSize());
    const std::vector<std::int64_t> indices(source->innerIndexPtr(), source->innerIndexPtr() + nonZeros);
    const std::vector<std::int64_t> pointers(source->outerIndexPtr(), source->outerIndexPtr() + outer + 1);
    const CompressedView<Value> view{
      .rows = static_cast<std::uint64_t>(source->rows()),
      .cols = static_cast<std::uint64_t>(source->cols()),
      .layout = Matrix::IsRowMajor ? E_SparseLayout::Csr : E_SparseLayout::Csc,
      .values = std::span<const Value>(source->valuePtr(), nonZeros),
      .indices = indices,
      .pointers = pointers,
    };
    return file.writeSparse<Value>(path, view, options);
  }

  namespace eigenDetail {

    template <class T>
    struct IsEigenSparse : std::false_type {};
    template <class Value, int Options, class StorageIndex>
    struct IsEigenSparse<Eigen::SparseMatrix<Value, Options, StorageIndex>> : std::true_type {};

    inline ArrayError eigenShapeError(const std::string_view path, const std::string& message) {
      return {ArrayError::E_Code::ShapeMismatch, std::format("'{}': {}", path, message)};
    }

    template <class Type>
    Result<Type> readEigenDense(const ArrayFile& file, const std::string_view path) {
      using Value = typename Type::Scalar;
      auto array = file.readDense<Value>(path);
      if (!array) return std::unexpected(std::move(array.error()));
      const auto& shape = array->shape;

      Eigen::Index rows = 0;
      Eigen::Index cols = 0;
      if (shape.size() == 2) {
        rows = static_cast<Eigen::Index>(shape[0]);
        cols = static_cast<Eigen::Index>(shape[1]);
      }
      else if (shape.size() == 1 && Type::IsVectorAtCompileTime) {
        // A rank-1 dataset fills a column vector, or a row vector when the type is one.
        const auto size = static_cast<Eigen::Index>(shape[0]);
        rows = Type::RowsAtCompileTime == 1 ? 1 : size;
        cols = Type::RowsAtCompileTime == 1 ? size : 1;
      }
      else {
        return std::unexpected(eigenShapeError(path, std::format("rank {} does not fit the requested type", shape.size())));
      }
      if ((Type::RowsAtCompileTime != Eigen::Dynamic && rows != Type::RowsAtCompileTime) ||
          (Type::ColsAtCompileTime != Eigen::Dynamic && cols != Type::ColsAtCompileTime)) {
        return std::unexpected(eigenShapeError(path, std::format("size {}x{} does not fit the fixed-size type", rows, cols)));
      }
      using RowMajorMap = Eigen::Map<const Eigen::Matrix<Value, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>;
      Type result = RowMajorMap(array->values.data(), rows, cols);
      return result;
    }

    template <class Type>
    Result<Type> readEigenSparse(const ArrayFile& file, const std::string_view path) {
      using Value = typename Type::Scalar;
      using StorageIndex = typename Type::StorageIndex;
      auto stored = file.readSparse<Value>(path);
      if (!stored) return std::unexpected(std::move(stored.error()));

      constexpr auto indexMax = static_cast<std::uint64_t>(std::numeric_limits<StorageIndex>::max());
      if (stored->rows > indexMax || stored->cols > indexMax || stored->values.size() > indexMax) {
        return std::unexpected(eigenShapeError(path, "too large for the StorageIndex of the requested type"));
      }
      const auto rows = static_cast<Eigen::Index>(stored->rows);
      const auto cols = static_cast<Eigen::Index>(stored->cols);
      const std::vector<StorageIndex> indices(stored->indices.begin(), stored->indices.end());
      const std::vector<StorageIndex> pointers(stored->pointers.begin(), stored->pointers.end());

      // Eigen needs strictly increasing inner indices per outer entry; other files (e.g. CHOLMOD
      // unsorted output) are rebuilt through triplets, which also sums duplicates.
      bool sorted = true;
      for (std::size_t outer = 0; outer + 1 < pointers.size() && sorted; ++outer) {
        for (auto k = pointers[outer] + 1; k < pointers[outer + 1]; ++k) {
          if (indices[static_cast<std::size_t>(k)] <= indices[static_cast<std::size_t>(k) - 1]) {
            sorted = false;
            break;
          }
        }
      }

      const auto build = [&]<int StoredOptions>() -> Type {
        using Stored = Eigen::SparseMatrix<Value, StoredOptions, StorageIndex>;
        if (sorted) {
          const Eigen::Map<const Stored> map(rows, cols, static_cast<Eigen::Index>(stored->values.size()), pointers.data(),
                                             indices.data(), stored->values.data());
          return Type(map);
        }
        std::vector<Eigen::Triplet<Value, StorageIndex>> triplets;
        triplets.reserve(stored->values.size());
        for (std::size_t outer = 0; outer + 1 < pointers.size(); ++outer) {
          for (auto k = static_cast<std::size_t>(pointers[outer]); k < static_cast<std::size_t>(pointers[outer + 1]); ++k) {
            const auto o = static_cast<StorageIndex>(outer);
            triplets.emplace_back(StoredOptions == Eigen::RowMajor ? o : indices[k], StoredOptions == Eigen::RowMajor ? indices[k] : o,
                                  stored->values[k]);
          }
        }
        Type result(rows, cols);
        result.setFromTriplets(triplets.begin(), triplets.end());
        return result;
      };
      if (stored->layout == E_SparseLayout::Csr) return build.template operator()<Eigen::RowMajor>();
      return build.template operator()<Eigen::ColMajor>();
    }

  } // namespace eigenDetail end

  // Reads a dense Eigen type (Matrix / Array, fixed or dynamic size) or an Eigen::SparseMatrix.
  // A ColMajor sparse type reads csr_matrix files too (and vice versa); Eigen transposes the storage.
  template <class Type>
  Result<Type> readEigen(const ArrayFile& file, const std::string_view path) {
    if constexpr (eigenDetail::IsEigenSparse<Type>::value) return eigenDetail::readEigenSparse<Type>(file, path);
    else return eigenDetail::readEigenDense<Type>(file, path);
  }

} // namespace anaf::IO::ARRAY end
