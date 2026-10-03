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

// ArrayFile adapters for standard containers (all named write(); never call them with explicit
// template arguments, which would also be tried against the Eigen overloads):
//   std::vector / std::array / std::span     -> rank-1 dataset
//   std::vector<std::vector<T>> (rectangular) -> rank-2 dataset
//   std::mdspan (any rank, any layout)        -> dataset of that rank (C++23; GCC >= 15, MSVC 17.9)

#include "arrayFile.hpp"

#include <array>
#include <format>
#include <version>
#ifdef __cpp_lib_mdspan
#include <mdspan>
#endif

namespace anaf::IO::ARRAY {

  template <Scalar T>
  Result<void> write(ArrayFile& file, const std::string_view path, const std::span<const T> values, const WriteOptions& options = {}) {
    const std::uint64_t shape[] = {values.size()};
    return file.writeDense<T>(path, values, shape, options);
  }

  template <Scalar T>
  Result<void> write(ArrayFile& file, const std::string_view path, const std::vector<T>& values, const WriteOptions& options = {}) {
    const std::uint64_t shape[] = {values.size()};
    return file.writeDense<T>(path, values, shape, options);
  }

  template <Scalar T, std::size_t N>
  Result<void> write(ArrayFile& file, const std::string_view path, const std::array<T, N>& values, const WriteOptions& options = {}) {
    const std::uint64_t shape[] = {N};
    return file.writeDense<T>(path, values, shape, options);
  }

  // Rows must all have the same length.
  template <Scalar T>
  Result<void> write(ArrayFile& file, const std::string_view path, const std::vector<std::vector<T>>& rows, const WriteOptions& options = {}) {
    const std::size_t cols = rows.empty() ? 0 : rows.front().size();
    std::vector<T> values;
    values.reserve(rows.size() * cols);
    for (const auto& row : rows) {
      if (row.size() != cols) {
        return std::unexpected(ArrayError{ArrayError::Code::ShapeMismatch, std::format("'{}': rows have different lengths", path)});
      }
      values.insert(values.end(), row.begin(), row.end());
    }
    const std::uint64_t shape[] = {rows.size(), cols};
    return file.writeDense<T>(path, values, shape, options);
  }

  template <Scalar T>
  Result<std::vector<T>> readVector(const ArrayFile& file, const std::string_view path) {
    auto array = file.readDense<T>(path);
    if (!array) return std::unexpected(std::move(array.error()));
    if (array->shape.size() != 1) {
      return std::unexpected(ArrayError{ArrayError::Code::ShapeMismatch, std::format("'{}' has rank {}, expected 1", path, array->shape.size())});
    }
    return std::move(array->values);
  }

  template <Scalar T>
  Result<std::vector<std::vector<T>>> readRows(const ArrayFile& file, const std::string_view path) {
    auto array = file.readDense<T>(path);
    if (!array) return std::unexpected(std::move(array.error()));
    if (array->shape.size() != 2) {
      return std::unexpected(ArrayError{ArrayError::Code::ShapeMismatch, std::format("'{}' has rank {}, expected 2", path, array->shape.size())});
    }
    const auto cols = static_cast<std::size_t>(array->shape[1]);
    std::vector<std::vector<T>> rows(static_cast<std::size_t>(array->shape[0]));
    for (std::size_t r = 0; r < rows.size(); ++r) {
      const auto first = array->values.begin() + static_cast<std::ptrdiff_t>(r * cols);
      rows[r].assign(first, first + static_cast<std::ptrdiff_t>(cols));
    }
    return rows;
  }

#ifdef __cpp_lib_mdspan

  // Any layout and accessor: layout_right with the default accessor is written in place,
  // everything else is first gathered into row-major order.
  template <class T, class Extents, class Layout, class Accessor>
    requires Scalar<std::remove_cv_t<T>>
  Result<void> write(ArrayFile& file, const std::string_view path, const std::mdspan<T, Extents, Layout, Accessor> view,
                     const WriteOptions& options = {}) {
    using Value = std::remove_cv_t<T>;
    constexpr std::size_t rank = Extents::rank();
    std::array<std::uint64_t, rank> shape{};
    std::size_t count = 1;
    for (std::size_t d = 0; d < rank; ++d) {
      shape[d] = static_cast<std::uint64_t>(view.extent(d));
      count *= static_cast<std::size_t>(view.extent(d));
    }
    if constexpr (std::is_same_v<Layout, std::layout_right> && std::is_same_v<Accessor, std::default_accessor<T>>) {
      return file.writeDense<Value>(path, std::span<const Value>(view.data_handle(), count), shape, options);
    }
    else {
      std::vector<Value> values;
      values.reserve(count);
      std::array<typename Extents::index_type, rank> index{};
      for (std::size_t flat = 0; flat < count; ++flat) {
        std::size_t rest = flat;
        for (std::size_t d = rank; d-- > 0;) {
          const auto extent = static_cast<std::size_t>(view.extent(d));
          index[d] = static_cast<typename Extents::index_type>(rest % extent);
          rest /= extent;
        }
        values.push_back(view[index]);
      }
      return file.writeDense<Value>(path, values, shape, options);
    }
  }

  // Row-major view over a DenseArray read with ArrayFile::readDense; the array must outlive it.
  template <std::size_t Rank, Scalar T>
  Result<std::mdspan<T, std::dextents<std::size_t, Rank>>> asMdspan(DenseArray<T>& array) {
    if (array.shape.size() != Rank) {
      return std::unexpected(ArrayError{ArrayError::Code::ShapeMismatch, std::format("array has rank {}, expected {}", array.shape.size(), Rank)});
    }
    std::array<std::size_t, Rank> extents{};
    for (std::size_t d = 0; d < Rank; ++d) extents[d] = static_cast<std::size_t>(array.shape[d]);
    return std::mdspan<T, std::dextents<std::size_t, Rank>>(array.values.data(), extents);
  }

#endif

} // namespace anaf::IO::ARRAY end
