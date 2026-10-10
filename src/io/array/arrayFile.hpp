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

// HDF5 file holding matrices, vectors and tensors (format-neutral core). Paths inside the file
// look like "stiffness" or "results/step_001/u"; missing parent groups are created on write.
//
// Thread safety: every call holds one process-wide mutex, because distribution HDF5 builds are
// not thread-safe. Different ArrayFile objects can be used from different threads; a single
// object must not be shared between threads without external locking.
//
// Container adapters on top of this class: stdArrays.hpp (std::vector, std::array, std::mdspan),
// eigenArrays.hpp (Eigen dense / sparse), cholmodArrays.hpp (cholmod_sparse / cholmod_dense).

#include "arrayTypes.hpp"

#include <filesystem>

namespace anaf::IO::ARRAY {

  class ArrayFile {
  public:
    enum class E_Access { ReadOnly, ReadWrite };

    // Creates the file, replacing an existing one.
    static Result<ArrayFile> create(const std::filesystem::path& path);
    static Result<ArrayFile> open(const std::filesystem::path& path, E_Access access = E_Access::ReadOnly);

    ArrayFile(ArrayFile&& other) noexcept;
    ArrayFile& operator=(ArrayFile&& other) noexcept;
    ArrayFile(const ArrayFile&) = delete;
    ArrayFile& operator=(const ArrayFile&) = delete;
    ~ArrayFile(); // closes silently; call close() to see a flush error

    Result<void> close();

    const std::filesystem::path& path() const noexcept { return m_path; }
    bool isOpen() const noexcept { return m_file >= 0; }
    bool isWritable() const noexcept { return m_writable; }

    // Structure
    bool exists(std::string_view path) const;
    Result<ArrayInfo> info(std::string_view path) const;
    Result<std::vector<std::string>> list(std::string_view group = "/") const; // child names, sorted
    Result<void> createGroup(std::string_view path);
    Result<void> remove(std::string_view path);

    // Dense arrays: values are row-major, values.size() == product of shape (1 for an empty shape).
    template <Scalar T>
    Result<void> writeDense(const std::string_view path, const std::span<const T> values,
                            const std::span<const std::uint64_t> shape, const WriteOptions& options = {}) {
      return writeDenseRaw(path, ScalarTraits<T>::type, values.data(), values.size(), shape, options);
    }

    template <Scalar T>
    Result<DenseArray<T>> readDense(const std::string_view path) const {
      DenseArray<T> array;
      auto shape = denseShape(path, ScalarTraits<T>::type);
      if (!shape) return std::unexpected(std::move(shape.error()));
      array.shape = std::move(*shape);
      std::uint64_t count = 1;
      for (const auto extent : array.shape) count *= extent;
      array.values.resize(count);
      if (auto read = readDenseRaw(path, ScalarTraits<T>::type, array.values.data(), count); !read) {
        return std::unexpected(std::move(read.error()));
      }
      return array;
    }

    // Sparse matrices (validated on write and on read).
    template <Scalar T>
    Result<void> writeSparse(const std::string_view path, const CompressedView<T>& matrix, const WriteOptions& options = {}) {
      return writeSparseRaw(path, ScalarTraits<T>::type, matrix.rows, matrix.cols, matrix.layout, matrix.values.data(),
                            matrix.values.size(), matrix.indices, matrix.pointers, options);
    }

    template <Scalar T>
    Result<CompressedMatrix<T>> readSparse(const std::string_view path) const {
      auto found = info(path);
      if (!found) return std::unexpected(std::move(found.error()));
      if (found->kind != E_ObjectKind::Sparse) return std::unexpected(kindError(path, "a sparse matrix"));
      CompressedMatrix<T> matrix;
      matrix.rows = found->shape[0];
      matrix.cols = found->shape[1];
      matrix.layout = found->layout;
      matrix.values.resize(found->nonZeros);
      if (auto read = readSparseRaw(path, ScalarTraits<T>::type, *found, matrix.values.data(), matrix.indices, matrix.pointers); !read) {
        return std::unexpected(std::move(read.error()));
      }
      return matrix;
    }

    // Attributes on any object ("/" = the file itself).
    Result<void> setAttribute(std::string_view path, std::string_view key, const AttributeValue& value);
    Result<AttributeValue> attribute(std::string_view path, std::string_view key) const;

  private:
    ArrayFile(std::int64_t file, std::filesystem::path path, bool writable) noexcept;

    Result<void> writeDenseRaw(std::string_view path, E_ScalarType type, const void* values, std::uint64_t count,
                               std::span<const std::uint64_t> shape, const WriteOptions& options);
    Result<std::vector<std::uint64_t>> denseShape(std::string_view path, E_ScalarType requested) const;
    Result<void> readDenseRaw(std::string_view path, E_ScalarType requested, void* values, std::uint64_t count) const;

    Result<void> writeSparseRaw(std::string_view path, E_ScalarType type, std::uint64_t rows, std::uint64_t cols,
                                E_SparseLayout layout, const void* values, std::uint64_t count,
                                std::span<const std::int64_t> indices, std::span<const std::int64_t> pointers,
                                const WriteOptions& options);
    Result<void> readSparseRaw(std::string_view path, E_ScalarType requested, const ArrayInfo& found, void* values,
                               std::vector<std::int64_t>& indices, std::vector<std::int64_t>& pointers) const;

    static ArrayError kindError(std::string_view path, std::string_view expected);

    std::int64_t m_file{-1}; // hid_t
    std::filesystem::path m_path;
    bool m_writable{false};
  };

} // namespace anaf::IO::ARRAY end
