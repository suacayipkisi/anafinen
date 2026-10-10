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

#include "arrayFile.hpp"

#include "../core/pathUtf8.hpp"
#include "../detail/h5Handle.hpp"

#include <algorithm>
#include <format>
#include <limits>
#include <mutex>
#include <system_error>
#include <type_traits>
#include <utility>

namespace anaf::IO::ARRAY {

  namespace {

    using detail::H5Handle;
    using Code = ArrayError::E_Code;

    static_assert(std::is_same_v<hid_t, std::int64_t>, "ArrayFile stores hid_t as std::int64_t (HDF5 >= 1.10)");

    constexpr const char* sparseEncodingKey = "encoding-type";
    constexpr const char* sparseVersionKey = "encoding-version";
    constexpr const char* sparseShapeKey = "shape";
    constexpr const char* sparseVersion = "0.1.0";

    // Distribution HDF5 builds are not thread-safe: every HDF5 call runs under this mutex.
    // Automatic error printing is switched off on each lock, because thread-safe builds keep
    // that setting per thread; errors are returned through ArrayError instead.
    std::unique_lock<std::mutex> lockHdf5() {
      static std::mutex s_mutex;
      std::unique_lock lock(s_mutex);
      H5Eset_auto2(H5E_DEFAULT, nullptr, nullptr);
      return lock;
    }

    herr_t collectError(unsigned, const H5E_error2_t* entry, void* data) {
      auto& text = *static_cast<std::string*>(data);
      if (!text.empty()) text += "; ";
      text += std::format("{}(): {}", entry->func_name ? entry->func_name : "?", entry->desc ? entry->desc : "");
      return 0;
    }

    ArrayError makeError(const Code code, std::string message) { return {code, std::move(message)}; }

    // Wraps the current HDF5 error stack (innermost call first) and clears it.
    ArrayError backendError(const std::string_view what) {
      std::string stack;
      H5Ewalk2(H5E_DEFAULT, H5E_WALK_DOWNWARD, collectError, &stack);
      H5Eclear2(H5E_DEFAULT);
      return makeError(Code::BackendError, stack.empty() ? std::string(what) : std::format("{} ({})", what, stack));
    }

    // "a/b/c" for an object path, "/" for the root; empty for an invalid path ("a//b", "..").
    std::string normalizePath(const std::string_view path) {
      std::string result;
      std::size_t pos = 0;
      while (pos <= path.size()) {
        const std::size_t end = std::min(path.find('/', pos), path.size());
        const std::string_view part = path.substr(pos, end - pos);
        if (part.empty()) {
          // A leading or trailing slash is fine, an empty component in between is not.
          if (pos != 0 && end != path.size()) return {};
        }
        else {
          if (part == "." || part == "..") return {};
          if (!result.empty()) result += '/';
          result += part;
        }
        pos = end + 1;
      }
      return result.empty() ? std::string("/") : result;
    }

    bool linkExists(const hid_t location, const std::string& path) {
      if (path == "/") return true;
      std::size_t pos = 0;
      while (true) {
        const std::size_t end = path.find('/', pos);
        const std::string prefix = path.substr(0, end);
        if (H5Lexists(location, prefix.c_str(), H5P_DEFAULT) <= 0) {
          H5Eclear2(H5E_DEFAULT);
          return false;
        }
        if (end == std::string::npos) return true;
        pos = end + 1;
      }
    }

    // Link creation properties: missing parent groups are created, names are UTF-8.
    H5Handle linkCreateList() {
      H5Handle lcpl = detail::h5Plist(H5Pcreate(H5P_LINK_CREATE));
      if (lcpl) {
        H5Pset_create_intermediate_group(lcpl.get(), 1);
        H5Pset_char_encoding(lcpl.get(), H5T_CSET_UTF8);
      }
      return lcpl;
    }

    H5Handle complexType(const hid_t part, const std::size_t partSize) {
      H5Handle type = detail::h5Type(H5Tcreate(H5T_COMPOUND, 2 * partSize));
      if (type) {
        H5Tinsert(type.get(), "r", 0, part);
        H5Tinsert(type.get(), "i", partSize, part);
      }
      return type;
    }

    // In-memory (native) type of a scalar; files use the same type, as h5py does.
    H5Handle memoryType(const E_ScalarType type) {
      switch (type) {
        case E_ScalarType::Float32: return detail::h5Type(H5Tcopy(H5T_NATIVE_FLOAT));
        case E_ScalarType::Float64: return detail::h5Type(H5Tcopy(H5T_NATIVE_DOUBLE));
        case E_ScalarType::Int32: return detail::h5Type(H5Tcopy(H5T_NATIVE_INT32));
        case E_ScalarType::Int64: return detail::h5Type(H5Tcopy(H5T_NATIVE_INT64));
        case E_ScalarType::UInt64: return detail::h5Type(H5Tcopy(H5T_NATIVE_UINT64));
        case E_ScalarType::Complex64: return complexType(H5T_NATIVE_FLOAT, sizeof(float));
        case E_ScalarType::Complex128: return complexType(H5T_NATIVE_DOUBLE, sizeof(double));
      }
      return {};
    }

    std::optional<E_ScalarType> classifyComplex(const hid_t type) {
      if (H5Tget_nmembers(type) != 2) return std::nullopt;
      std::optional<E_ScalarType> result;
      const std::size_t size = H5Tget_size(type);
      bool namesMatch = true;
      for (unsigned member = 0; member < 2; ++member) {
        char* name = H5Tget_member_name(type, member);
        namesMatch = namesMatch && name && std::string_view(name) == (member == 0 ? "r" : "i");
        H5free_memory(name);
        if (H5Tget_member_class(type, member) != H5T_FLOAT) namesMatch = false;
      }
      if (!namesMatch) return std::nullopt;
      if (size == 2 * sizeof(float)) result = E_ScalarType::Complex64;
      if (size == 2 * sizeof(double)) result = E_ScalarType::Complex128;
      return result;
    }

    std::optional<E_ScalarType> classify(const hid_t type) {
      const std::size_t size = H5Tget_size(type);
      switch (H5Tget_class(type)) {
        case H5T_FLOAT:
          if (size == 4) return E_ScalarType::Float32;
          if (size == 8) return E_ScalarType::Float64;
          return std::nullopt;
        case H5T_INTEGER: {
          const bool isSigned = H5Tget_sign(type) == H5T_SGN_2;
          if (size == 4 && isSigned) return E_ScalarType::Int32;
          if (size == 8) return isSigned ? E_ScalarType::Int64 : E_ScalarType::UInt64;
          return std::nullopt;
        }
        case H5T_COMPOUND: return classifyComplex(type);
        default: return std::nullopt;
      }
    }

    std::optional<std::uint64_t> elementCount(const std::span<const std::uint64_t> shape) {
      std::uint64_t count = 1;
      for (const auto extent : shape) {
        if (extent != 0 && count > std::numeric_limits<std::uint64_t>::max() / extent) return std::nullopt;
        count *= extent;
      }
      return count;
    }

    // Halves the largest chunk dimension until a chunk holds at most 1 MiB.
    std::vector<hsize_t> chunkShape(const std::vector<hsize_t>& dims, const std::size_t elementSize) {
      std::vector<hsize_t> chunk(dims);
      for (auto& extent : chunk) extent = std::max<hsize_t>(extent, 1);
      constexpr hsize_t targetBytes = hsize_t{1} << 20;
      const auto bytes = [&] {
        hsize_t total = elementSize;
        for (const auto extent : chunk) total *= extent;
        return total;
      };
      while (bytes() > targetBytes) {
        auto largest = std::max_element(chunk.begin(), chunk.end());
        if (*largest == 1) break;
        *largest = (*largest + 1) / 2;
      }
      return chunk;
    }

    Result<void> writeDataset(const hid_t location, const std::string& name, const E_ScalarType type, const void* values,
                              const std::span<const std::uint64_t> shape, const WriteOptions& options) {
      const std::vector<hsize_t> dims(shape.begin(), shape.end());
      const std::uint64_t count = elementCount(shape).value_or(0);
      H5Handle space = detail::h5Space(dims.empty() ? H5Screate(H5S_SCALAR)
                                                    : H5Screate_simple(static_cast<int>(dims.size()), dims.data(), nullptr));
      H5Handle memType = memoryType(type);
      H5Handle lcpl = linkCreateList();
      H5Handle dcpl = detail::h5Plist(H5Pcreate(H5P_DATASET_CREATE));
      if (!space || !memType || !lcpl || !dcpl) return std::unexpected(backendError("cannot prepare dataset '" + name + "'"));

      if (options.deflateLevel < 0 || options.deflateLevel > 9) {
        return std::unexpected(makeError(Code::InvalidData, std::format("deflate level {} is outside 0..9", options.deflateLevel)));
      }
      if (options.deflateLevel > 0 && !dims.empty() && count > 0) {
        if (H5Zfilter_avail(H5Z_FILTER_DEFLATE) <= 0) {
          return std::unexpected(makeError(Code::BackendError, "this HDF5 build has no deflate (zlib) filter"));
        }
        const auto chunk = chunkShape(dims, H5Tget_size(memType.get()));
        if (H5Pset_chunk(dcpl.get(), static_cast<int>(chunk.size()), chunk.data()) < 0 ||
            H5Pset_deflate(dcpl.get(), static_cast<unsigned>(options.deflateLevel)) < 0) {
          return std::unexpected(backendError("cannot set compression for '" + name + "'"));
        }
      }

      H5Handle dataset = detail::h5Dataset(
        H5Dcreate2(location, name.c_str(), memType.get(), space.get(), lcpl.get(), dcpl.get(), H5P_DEFAULT));
      if (!dataset) return std::unexpected(backendError("cannot create dataset '" + name + "'"));
      if (count > 0 && H5Dwrite(dataset.get(), memType.get(), H5S_ALL, H5S_ALL, H5P_DEFAULT, values) < 0) {
        return std::unexpected(backendError("cannot write dataset '" + name + "'"));
      }
      return {};
    }

    Result<void> writeAttributeTo(const hid_t object, const std::string& key, const AttributeValue& value) {
      if (H5Aexists(object, key.c_str()) > 0 && H5Adelete(object, key.c_str()) < 0) {
        return std::unexpected(backendError("cannot replace attribute '" + key + "'"));
      }
      H5Eclear2(H5E_DEFAULT);

      // Numeric scalars and vectors share one path: element type, dimensions, data pointer.
      const auto writeNumeric = [&](const hid_t nativeType, const void* data, const std::size_t count, const bool isVector) -> Result<void> {
        const hsize_t dim = count;
        H5Handle space = detail::h5Space(isVector ? H5Screate_simple(1, &dim, nullptr) : H5Screate(H5S_SCALAR));
        H5Handle attribute = detail::h5Attribute(H5Acreate2(object, key.c_str(), nativeType, space.get(), H5P_DEFAULT, H5P_DEFAULT));
        if (!attribute) return std::unexpected(backendError("cannot create attribute '" + key + "'"));
        if (count > 0 && H5Awrite(attribute.get(), nativeType, data) < 0) {
          return std::unexpected(backendError("cannot write attribute '" + key + "'"));
        }
        return {};
      };

      if (const auto* number = std::get_if<std::int64_t>(&value)) return writeNumeric(H5T_NATIVE_INT64, number, 1, false);
      if (const auto* number = std::get_if<double>(&value)) return writeNumeric(H5T_NATIVE_DOUBLE, number, 1, false);
      if (const auto* list = std::get_if<std::vector<std::int64_t>>(&value)) return writeNumeric(H5T_NATIVE_INT64, list->data(), list->size(), true);
      if (const auto* list = std::get_if<std::vector<double>>(&value)) return writeNumeric(H5T_NATIVE_DOUBLE, list->data(), list->size(), true);

      const auto& text = std::get<std::string>(value);
      H5Handle type = detail::h5Type(H5Tcopy(H5T_C_S1));
      if (!type || H5Tset_size(type.get(), H5T_VARIABLE) < 0 || H5Tset_cset(type.get(), H5T_CSET_UTF8) < 0) {
        return std::unexpected(backendError("cannot create string type"));
      }
      H5Handle space = detail::h5Space(H5Screate(H5S_SCALAR));
      H5Handle attribute = detail::h5Attribute(H5Acreate2(object, key.c_str(), type.get(), space.get(), H5P_DEFAULT, H5P_DEFAULT));
      const char* data = text.c_str();
      if (!attribute || H5Awrite(attribute.get(), type.get(), static_cast<const void*>(&data)) < 0) {
        return std::unexpected(backendError("cannot write attribute '" + key + "'"));
      }
      return {};
    }

    Result<AttributeValue> readAttributeFrom(const hid_t object, const std::string& key) {
      if (H5Aexists(object, key.c_str()) <= 0) {
        H5Eclear2(H5E_DEFAULT);
        return std::unexpected(makeError(Code::NotFound, "no attribute '" + key + "'"));
      }
      H5Handle attribute = detail::h5Attribute(H5Aopen(object, key.c_str(), H5P_DEFAULT));
      H5Handle type = detail::h5Type(attribute ? H5Aget_type(attribute.get()) : H5I_INVALID_HID);
      H5Handle space = detail::h5Space(attribute ? H5Aget_space(attribute.get()) : H5I_INVALID_HID);
      if (!attribute || !type || !space) return std::unexpected(backendError("cannot open attribute '" + key + "'"));

      const int rank = H5Sget_simple_extent_ndims(space.get());
      const hssize_t points = H5Sget_simple_extent_npoints(space.get());
      if (rank < 0 || points < 0) return std::unexpected(backendError("cannot read attribute '" + key + "'"));
      const auto count = static_cast<std::size_t>(points);

      switch (H5Tget_class(type.get())) {
        case H5T_STRING: {
          if (rank != 0) return std::unexpected(makeError(Code::TypeMismatch, "attribute '" + key + "' is a string array"));
          if (H5Tis_variable_str(type.get()) > 0) {
            H5Handle memType = detail::h5Type(H5Tcopy(H5T_C_S1));
            H5Tset_size(memType.get(), H5T_VARIABLE);
            H5Tset_cset(memType.get(), H5Tget_cset(type.get()));
            char* data = nullptr;
            if (H5Aread(attribute.get(), memType.get(), static_cast<void*>(&data)) < 0) {
              return std::unexpected(backendError("cannot read attribute '" + key + "'"));
            }
            std::string text = data ? data : "";
            H5free_memory(data);
            return text;
          }
          std::string text(H5Tget_size(type.get()), '\0');
          if (H5Aread(attribute.get(), type.get(), text.data()) < 0) {
            return std::unexpected(backendError("cannot read attribute '" + key + "'"));
          }
          text.resize(std::min(text.find('\0'), text.size()));
          if (H5Tget_strpad(type.get()) == H5T_STR_SPACEPAD) text.erase(text.find_last_not_of(' ') + 1);
          return text;
        }
        case H5T_INTEGER: {
          std::vector<std::int64_t> values(count);
          if (count > 0 && H5Aread(attribute.get(), H5T_NATIVE_INT64, values.data()) < 0) {
            return std::unexpected(backendError("cannot read attribute '" + key + "'"));
          }
          if (rank == 0) return values.front();
          return values;
        }
        case H5T_FLOAT: {
          std::vector<double> values(count);
          if (count > 0 && H5Aread(attribute.get(), H5T_NATIVE_DOUBLE, values.data()) < 0) {
            return std::unexpected(backendError("cannot read attribute '" + key + "'"));
          }
          if (rank == 0) return values.front();
          return values;
        }
        default:
          return std::unexpected(makeError(Code::TypeMismatch, "attribute '" + key + "' has an unsupported type"));
      }
    }

    // Opens an existing object; NotFound when the path does not exist.
    Result<H5Handle> openObject(const hid_t file, const std::string& path) {
      if (!linkExists(file, path)) return std::unexpected(makeError(Code::NotFound, "no object at '" + path + "'"));
      H5Handle object = detail::h5Object(H5Oopen(file, path.c_str(), H5P_DEFAULT));
      if (!object) return std::unexpected(backendError("cannot open '" + path + "'"));
      return object;
    }

    Result<std::vector<std::uint64_t>> datasetShape(const hid_t dataset) {
      H5Handle space = detail::h5Space(H5Dget_space(dataset));
      const int rank = space ? H5Sget_simple_extent_ndims(space.get()) : -1;
      if (rank < 0) return std::unexpected(backendError("cannot read dataset shape"));
      std::vector<hsize_t> dims(static_cast<std::size_t>(rank));
      if (rank > 0 && H5Sget_simple_extent_dims(space.get(), dims.data(), nullptr) < 0) {
        return std::unexpected(backendError("cannot read dataset shape"));
      }
      return std::vector<std::uint64_t>(dims.begin(), dims.end());
    }

    std::optional<E_ScalarType> datasetScalar(const hid_t dataset) {
      H5Handle type = detail::h5Type(H5Dget_type(dataset));
      return type ? classify(type.get()) : std::nullopt;
    }

    Result<void> checkReadable(const std::optional<E_ScalarType> stored, const E_ScalarType requested, const std::string& path) {
      if (!stored) return std::unexpected(makeError(Code::TypeMismatch, "'" + path + "' has an unsupported element type"));
      if (!canReadAs(*stored, requested)) {
        return std::unexpected(makeError(Code::TypeMismatch, std::format("'{}' holds {}, cannot read it as {}", path,
                                                                         scalarTypeName(*stored), scalarTypeName(requested))));
      }
      return {};
    }

    // Reads a whole dataset with the given memory type; its element count must equal `count`.
    Result<void> readDataset(const hid_t dataset, const hid_t memType, void* values, const std::uint64_t count, const std::string& path) {
      H5Handle space = detail::h5Space(H5Dget_space(dataset));
      const hssize_t points = space ? H5Sget_simple_extent_npoints(space.get()) : -1;
      if (points < 0) return std::unexpected(backendError("cannot read '" + path + "'"));
      if (static_cast<std::uint64_t>(points) != count) {
        return std::unexpected(makeError(Code::ShapeMismatch, std::format("'{}' holds {} values, expected {}", path, points, count)));
      }
      if (count > 0 && H5Dread(dataset, memType, H5S_ALL, H5S_ALL, H5P_DEFAULT, values) < 0) {
        return std::unexpected(backendError("cannot read '" + path + "'"));
      }
      return {};
    }

    std::optional<std::string> validateCompressed(const std::uint64_t rows, const std::uint64_t cols, const E_SparseLayout layout,
                                                  const std::uint64_t nonZeros, const std::span<const std::int64_t> indices,
                                                  const std::span<const std::int64_t> pointers) {
      const std::uint64_t outer = layout == E_SparseLayout::Csc ? cols : rows;
      const std::uint64_t inner = layout == E_SparseLayout::Csc ? rows : cols;
      if (pointers.size() != outer + 1) return std::format("pointer array has {} entries, expected {}", pointers.size(), outer + 1);
      if (indices.size() != nonZeros) return std::format("index array has {} entries, expected {}", indices.size(), nonZeros);
      if (pointers.front() != 0) return std::format("first pointer is {}, expected 0", pointers.front());
      if (static_cast<std::uint64_t>(pointers.back()) != nonZeros) {
        return std::format("last pointer is {}, expected {}", pointers.back(), nonZeros);
      }
      for (std::size_t i = 0; i + 1 < pointers.size(); ++i) {
        if (pointers[i + 1] < pointers[i]) return std::format("pointers decrease at {}", i);
      }
      for (std::size_t i = 0; i < indices.size(); ++i) {
        if (indices[i] < 0 || static_cast<std::uint64_t>(indices[i]) >= inner) {
          return std::format("index {} at {} is outside 0..{}", indices[i], i, inner);
        }
      }
      return std::nullopt;
    }

  } // namespace end

  std::string_view scalarTypeName(const E_ScalarType type) noexcept {
    switch (type) {
      case E_ScalarType::Float32: return "float32";
      case E_ScalarType::Float64: return "float64";
      case E_ScalarType::Int32: return "int32";
      case E_ScalarType::Int64: return "int64";
      case E_ScalarType::UInt64: return "uint64";
      case E_ScalarType::Complex64: return "complex64";
      case E_ScalarType::Complex128: return "complex128";
    }
    return "unknown";
  }

  bool canReadAs(const E_ScalarType stored, const E_ScalarType requested) noexcept {
    if (stored == requested) return true;
    return (stored == E_ScalarType::Float32 && requested == E_ScalarType::Float64) ||
           (stored == E_ScalarType::Int32 && requested == E_ScalarType::Int64) ||
           (stored == E_ScalarType::Complex64 && requested == E_ScalarType::Complex128);
  }

  // ---------------------------------------------------------------- lifetime

  ArrayFile::ArrayFile(const std::int64_t file, std::filesystem::path path, const bool writable) noexcept
    : m_file(file), m_path(std::move(path)), m_writable(writable) {}

  ArrayFile::ArrayFile(ArrayFile&& other) noexcept
    : m_file(std::exchange(other.m_file, -1)), m_path(std::move(other.m_path)), m_writable(other.m_writable) {}

  ArrayFile& ArrayFile::operator=(ArrayFile&& other) noexcept {
    if (this != &other) {
      (void)close();
      m_file = std::exchange(other.m_file, -1);
      m_path = std::move(other.m_path);
      m_writable = other.m_writable;
    }
    return *this;
  }

  ArrayFile::~ArrayFile() { (void)close(); }

  Result<void> ArrayFile::close() {
    if (m_file < 0) return {};
    auto lock = lockHdf5();
    const herr_t status = H5Fclose(std::exchange(m_file, -1));
    if (status < 0) return std::unexpected(backendError("cannot close '" + pathToUtf8(m_path) + "'"));
    return {};
  }

  Result<ArrayFile> ArrayFile::create(const std::filesystem::path& path) {
    const std::string name = pathToUtf8(path);
    auto lock = lockHdf5();
    const hid_t file = H5Fcreate(name.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (file < 0) return std::unexpected(backendError("cannot create '" + name + "'"));
    return ArrayFile(file, path, true);
  }

  Result<ArrayFile> ArrayFile::open(const std::filesystem::path& path, const E_Access access) {
    const std::string name = pathToUtf8(path);
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
      return std::unexpected(makeError(Code::FileNotFound, "no file '" + name + "'"));
    }
    auto lock = lockHdf5();
    if (H5Fis_accessible(name.c_str(), H5P_DEFAULT) <= 0) {
      H5Eclear2(H5E_DEFAULT);
      return std::unexpected(makeError(Code::InvalidFile, "'" + name + "' is not an HDF5 file"));
    }
    const bool writable = access == E_Access::ReadWrite;
    const hid_t file = H5Fopen(name.c_str(), writable ? H5F_ACC_RDWR : H5F_ACC_RDONLY, H5P_DEFAULT);
    if (file < 0) return std::unexpected(backendError("cannot open '" + name + "'"));
    return ArrayFile(file, path, writable);
  }

  // ---------------------------------------------------------------- structure

  namespace {

    // Common checks of every call: an open file, a valid path and, for writes, write access.
    Result<std::string> checkCall(const std::int64_t file, const bool writable, const std::string_view path,
                                  const bool forWrite, const bool rootAllowed) {
      if (file < 0) return std::unexpected(makeError(Code::InvalidData, "the file is closed"));
      if (forWrite && !writable) return std::unexpected(makeError(Code::ReadOnly, "the file is open read-only"));
      std::string name = normalizePath(path);
      if (name.empty() || (!rootAllowed && name == "/")) {
        return std::unexpected(makeError(Code::InvalidData, std::format("invalid object path '{}'", path)));
      }
      return name;
    }

    // Write target: removed when it exists and overwriting is allowed.
    Result<void> prepareTarget(const hid_t file, const std::string& name, const WriteOptions& options) {
      if (!linkExists(file, name)) return {};
      if (!options.overwrite) return std::unexpected(makeError(Code::AlreadyExists, "'" + name + "' already exists"));
      if (H5Ldelete(file, name.c_str(), H5P_DEFAULT) < 0) return std::unexpected(backendError("cannot replace '" + name + "'"));
      return {};
    }

  } // namespace end

  bool ArrayFile::exists(const std::string_view path) const {
    auto lock = lockHdf5();
    const auto name = checkCall(m_file, m_writable, path, false, true);
    return name && linkExists(m_file, *name);
  }

  Result<ArrayInfo> ArrayFile::info(const std::string_view path) const {
    auto lock = lockHdf5();
    const auto name = checkCall(m_file, m_writable, path, false, true);
    if (!name) return std::unexpected(name.error());
    auto object = openObject(m_file, *name);
    if (!object) return std::unexpected(std::move(object.error()));

    ArrayInfo result;
    if (H5Iget_type(object->get()) == H5I_DATASET) {
      result.kind = E_ObjectKind::Dense;
      result.scalar = datasetScalar(object->get());
      auto shape = datasetShape(object->get());
      if (!shape) return std::unexpected(std::move(shape.error()));
      result.shape = std::move(*shape);
      return result;
    }

    result.kind = E_ObjectKind::Group;
    auto encoding = readAttributeFrom(object->get(), sparseEncodingKey);
    const auto* type = encoding ? std::get_if<std::string>(&*encoding) : nullptr;
    if (!type || (*type != "csc_matrix" && *type != "csr_matrix")) return result;

    result.kind = E_ObjectKind::Sparse;
    result.layout = *type == "csc_matrix" ? E_SparseLayout::Csc : E_SparseLayout::Csr;
    auto shape = readAttributeFrom(object->get(), sparseShapeKey);
    const auto* dims = shape ? std::get_if<std::vector<std::int64_t>>(&*shape) : nullptr;
    if (!dims || dims->size() != 2 || (*dims)[0] < 0 || (*dims)[1] < 0) {
      return std::unexpected(makeError(Code::InvalidData, "sparse matrix '" + *name + "' has no valid 'shape' attribute"));
    }
    result.shape = {static_cast<std::uint64_t>((*dims)[0]), static_cast<std::uint64_t>((*dims)[1])};
    if (H5Lexists(object->get(), "data", H5P_DEFAULT) <= 0) {
      H5Eclear2(H5E_DEFAULT);
      return std::unexpected(makeError(Code::InvalidData, "sparse matrix '" + *name + "' has no 'data' dataset"));
    }
    H5Handle data = detail::h5Dataset(H5Dopen2(object->get(), "data", H5P_DEFAULT));
    H5Handle space = detail::h5Space(data ? H5Dget_space(data.get()) : H5I_INVALID_HID);
    const hssize_t points = space ? H5Sget_simple_extent_npoints(space.get()) : -1;
    if (points < 0) return std::unexpected(backendError("cannot open '" + *name + "/data'"));
    result.scalar = datasetScalar(data.get());
    result.nonZeros = static_cast<std::uint64_t>(points);
    return result;
  }

  Result<std::vector<std::string>> ArrayFile::list(const std::string_view group) const {
    auto lock = lockHdf5();
    const auto name = checkCall(m_file, m_writable, group, false, true);
    if (!name) return std::unexpected(name.error());
    auto object = openObject(m_file, *name);
    if (!object) return std::unexpected(std::move(object.error()));
    if (H5Iget_type(object->get()) != H5I_GROUP) return std::unexpected(kindError(*name, "a group"));

    H5G_info_t groupInfo{};
    if (H5Gget_info(object->get(), &groupInfo) < 0) return std::unexpected(backendError("cannot list '" + *name + "'"));
    std::vector<std::string> names;
    names.reserve(groupInfo.nlinks);
    for (hsize_t i = 0; i < groupInfo.nlinks; ++i) {
      const auto length = H5Lget_name_by_idx(object->get(), ".", H5_INDEX_NAME, H5_ITER_INC, i, nullptr, 0, H5P_DEFAULT);
      if (length < 0) return std::unexpected(backendError("cannot list '" + *name + "'"));
      std::string child(static_cast<std::size_t>(length) + 1, '\0');
      H5Lget_name_by_idx(object->get(), ".", H5_INDEX_NAME, H5_ITER_INC, i, child.data(), child.size(), H5P_DEFAULT);
      child.resize(static_cast<std::size_t>(length));
      names.push_back(std::move(child));
    }
    return names;
  }

  Result<void> ArrayFile::createGroup(const std::string_view path) {
    auto lock = lockHdf5();
    const auto name = checkCall(m_file, m_writable, path, true, true);
    if (!name) return std::unexpected(name.error());
    if (linkExists(m_file, *name)) {
      auto object = openObject(m_file, *name);
      if (!object) return std::unexpected(std::move(object.error()));
      if (H5Iget_type(object->get()) != H5I_GROUP) return std::unexpected(makeError(Code::AlreadyExists, "'" + *name + "' is not a group"));
      return {};
    }
    H5Handle lcpl = linkCreateList();
    H5Handle created = detail::h5Group(H5Gcreate2(m_file, name->c_str(), lcpl.get(), H5P_DEFAULT, H5P_DEFAULT));
    if (!created) return std::unexpected(backendError("cannot create group '" + *name + "'"));
    return {};
  }

  Result<void> ArrayFile::remove(const std::string_view path) {
    auto lock = lockHdf5();
    const auto name = checkCall(m_file, m_writable, path, true, false);
    if (!name) return std::unexpected(name.error());
    if (!linkExists(m_file, *name)) return std::unexpected(makeError(Code::NotFound, "no object at '" + *name + "'"));
    if (H5Ldelete(m_file, name->c_str(), H5P_DEFAULT) < 0) return std::unexpected(backendError("cannot remove '" + *name + "'"));
    return {};
  }

  ArrayError ArrayFile::kindError(const std::string_view path, const std::string_view expected) {
    return makeError(Code::KindMismatch, std::format("'{}' is not {}", path, expected));
  }

  // ---------------------------------------------------------------- dense

  Result<void> ArrayFile::writeDenseRaw(const std::string_view path, const E_ScalarType type, const void* values,
                                        const std::uint64_t count, const std::span<const std::uint64_t> shape,
                                        const WriteOptions& options) {
    auto lock = lockHdf5();
    const auto name = checkCall(m_file, m_writable, path, true, false);
    if (!name) return std::unexpected(name.error());
    const auto expected = elementCount(shape);
    if (!expected) return std::unexpected(makeError(Code::InvalidData, "shape of '" + *name + "' overflows"));
    if (*expected != count) {
      return std::unexpected(makeError(Code::ShapeMismatch, std::format("'{}': {} values for a shape of {} elements", *name, count, *expected)));
    }
    if (auto prepared = prepareTarget(m_file, *name, options); !prepared) return prepared;
    return writeDataset(m_file, *name, type, values, shape, options);
  }

  Result<std::vector<std::uint64_t>> ArrayFile::denseShape(const std::string_view path, const E_ScalarType requested) const {
    auto lock = lockHdf5();
    const auto name = checkCall(m_file, m_writable, path, false, false);
    if (!name) return std::unexpected(name.error());
    auto object = openObject(m_file, *name);
    if (!object) return std::unexpected(std::move(object.error()));
    if (H5Iget_type(object->get()) != H5I_DATASET) return std::unexpected(kindError(*name, "a dense array"));
    if (auto readable = checkReadable(datasetScalar(object->get()), requested, *name); !readable) {
      return std::unexpected(std::move(readable.error()));
    }
    return datasetShape(object->get());
  }

  Result<void> ArrayFile::readDenseRaw(const std::string_view path, const E_ScalarType requested, void* values,
                                       const std::uint64_t count) const {
    auto lock = lockHdf5();
    const auto name = checkCall(m_file, m_writable, path, false, false);
    if (!name) return std::unexpected(name.error());
    H5Handle dataset = detail::h5Dataset(H5Dopen2(m_file, name->c_str(), H5P_DEFAULT));
    H5Handle memType = memoryType(requested);
    if (!dataset || !memType) return std::unexpected(backendError("cannot open '" + *name + "'"));
    return readDataset(dataset.get(), memType.get(), values, count, *name);
  }

  // ---------------------------------------------------------------- sparse

  Result<void> ArrayFile::writeSparseRaw(const std::string_view path, const E_ScalarType type, const std::uint64_t rows,
                                         const std::uint64_t cols, const E_SparseLayout layout, const void* values,
                                         const std::uint64_t count, const std::span<const std::int64_t> indices,
                                         const std::span<const std::int64_t> pointers, const WriteOptions& options) {
    auto lock = lockHdf5();
    const auto name = checkCall(m_file, m_writable, path, true, false);
    if (!name) return std::unexpected(name.error());
    if (rows > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        cols > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      return std::unexpected(makeError(Code::InvalidData, "sparse matrix '" + *name + "' is too large"));
    }
    if (const auto problem = validateCompressed(rows, cols, layout, count, indices, pointers)) {
      return std::unexpected(makeError(Code::InvalidData, "sparse matrix '" + *name + "': " + *problem));
    }
    if (auto prepared = prepareTarget(m_file, *name, options); !prepared) return prepared;

    H5Handle lcpl = linkCreateList();
    H5Handle group = detail::h5Group(H5Gcreate2(m_file, name->c_str(), lcpl.get(), H5P_DEFAULT, H5P_DEFAULT));
    if (!group) return std::unexpected(backendError("cannot create group '" + *name + "'"));

    const std::uint64_t nonZeroShape[] = {count};
    const std::uint64_t pointerShape[] = {pointers.size()};
    if (auto written = writeDataset(group.get(), "data", type, values, nonZeroShape, options); !written) return written;
    if (auto written = writeDataset(group.get(), "indices", E_ScalarType::Int64, indices.data(), nonZeroShape, options); !written) return written;
    if (auto written = writeDataset(group.get(), "indptr", E_ScalarType::Int64, pointers.data(), pointerShape, options); !written) return written;

    const std::string encoding = layout == E_SparseLayout::Csc ? "csc_matrix" : "csr_matrix";
    const std::vector<std::int64_t> shape = {static_cast<std::int64_t>(rows), static_cast<std::int64_t>(cols)};
    if (auto written = writeAttributeTo(group.get(), sparseEncodingKey, encoding); !written) return written;
    if (auto written = writeAttributeTo(group.get(), sparseVersionKey, std::string(sparseVersion)); !written) return written;
    return writeAttributeTo(group.get(), sparseShapeKey, shape);
  }

  Result<void> ArrayFile::readSparseRaw(const std::string_view path, const E_ScalarType requested, const ArrayInfo& found,
                                        void* values, std::vector<std::int64_t>& indices, std::vector<std::int64_t>& pointers) const {
    auto lock = lockHdf5();
    const auto name = checkCall(m_file, m_writable, path, false, false);
    if (!name) return std::unexpected(name.error());
    if (auto readable = checkReadable(found.scalar, requested, *name); !readable) return readable;

    H5Handle group = detail::h5Group(H5Gopen2(m_file, name->c_str(), H5P_DEFAULT));
    if (!group) return std::unexpected(backendError("cannot open '" + *name + "'"));
    const auto readPart = [&](const char* part, const hid_t memType, void* buffer, const std::uint64_t count) -> Result<void> {
      const std::string partPath = *name + "/" + part;
      if (H5Lexists(group.get(), part, H5P_DEFAULT) <= 0) {
        H5Eclear2(H5E_DEFAULT);
        return std::unexpected(makeError(Code::InvalidData, "sparse matrix '" + *name + "' has no '" + part + "' dataset"));
      }
      H5Handle dataset = detail::h5Dataset(H5Dopen2(group.get(), part, H5P_DEFAULT));
      if (!dataset) return std::unexpected(backendError("cannot open '" + partPath + "'"));
      if (std::string_view(part) != "data") {
        H5Handle type = detail::h5Type(H5Dget_type(dataset.get()));
        if (!type || H5Tget_class(type.get()) != H5T_INTEGER) {
          return std::unexpected(makeError(Code::InvalidData, "'" + partPath + "' is not an integer array"));
        }
      }
      if (auto read = readDataset(dataset.get(), memType, buffer, count, partPath); !read) {
        auto error = std::move(read.error());
        if (error.code == Code::ShapeMismatch) error.code = Code::InvalidData;
        return std::unexpected(std::move(error));
      }
      return {};
    };

    const std::uint64_t outer = found.layout == E_SparseLayout::Csc ? found.shape[1] : found.shape[0];
    indices.resize(found.nonZeros);
    pointers.resize(outer + 1);
    H5Handle memType = memoryType(requested);
    if (auto read = readPart("data", memType.get(), values, found.nonZeros); !read) return read;
    if (auto read = readPart("indices", H5T_NATIVE_INT64, indices.data(), indices.size()); !read) return read;
    if (auto read = readPart("indptr", H5T_NATIVE_INT64, pointers.data(), pointers.size()); !read) return read;

    if (const auto problem = validateCompressed(found.shape[0], found.shape[1], found.layout, found.nonZeros, indices, pointers)) {
      return std::unexpected(makeError(Code::InvalidData, "sparse matrix '" + *name + "': " + *problem));
    }
    return {};
  }

  // ---------------------------------------------------------------- attributes

  Result<void> ArrayFile::setAttribute(const std::string_view path, const std::string_view key, const AttributeValue& value) {
    auto lock = lockHdf5();
    const auto name = checkCall(m_file, m_writable, path, true, true);
    if (!name) return std::unexpected(name.error());
    if (key.empty()) return std::unexpected(makeError(Code::InvalidData, "empty attribute name"));
    auto object = openObject(m_file, *name);
    if (!object) return std::unexpected(std::move(object.error()));
    return writeAttributeTo(object->get(), std::string(key), value);
  }

  Result<AttributeValue> ArrayFile::attribute(const std::string_view path, const std::string_view key) const {
    auto lock = lockHdf5();
    const auto name = checkCall(m_file, m_writable, path, false, true);
    if (!name) return std::unexpected(name.error());
    auto object = openObject(m_file, *name);
    if (!object) return std::unexpected(std::move(object.error()));
    return readAttributeFrom(object->get(), std::string(key));
  }

} // namespace anaf::IO::ARRAY end
