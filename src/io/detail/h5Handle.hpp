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

// Move-only owner of one HDF5 identifier (file, group, dataset, dataspace, datatype, attribute,
// property list). Internal to anaf_io: includes hdf5.h, which callers never see.
// Must be destroyed while the HDF5 mutex is held (see array/arrayFile.cpp).

#include <hdf5.h>

#include <utility>

namespace anaf::IO::detail {

  class H5Handle {
  public:
    using Closer = herr_t (*)(hid_t);

    H5Handle() noexcept = default;
    H5Handle(const hid_t id, const Closer closer) noexcept : m_id(id), m_closer(closer) {}

    H5Handle(H5Handle&& other) noexcept
      : m_id(std::exchange(other.m_id, H5I_INVALID_HID)), m_closer(other.m_closer) {}

    H5Handle& operator=(H5Handle&& other) noexcept {
      if (this != &other) {
        reset();
        m_id = std::exchange(other.m_id, H5I_INVALID_HID);
        m_closer = other.m_closer;
      }
      return *this;
    }

    H5Handle(const H5Handle&) = delete;
    H5Handle& operator=(const H5Handle&) = delete;

    ~H5Handle() { reset(); }

    hid_t get() const noexcept { return m_id; }
    explicit operator bool() const noexcept { return m_id >= 0; }

    // Closes now and reports the result (a failed file close means unflushed data).
    herr_t reset() noexcept {
      herr_t status = 0;
      if (m_id >= 0 && m_closer) status = m_closer(m_id);
      m_id = H5I_INVALID_HID;
      return status;
    }

  private:
    hid_t m_id{H5I_INVALID_HID};
    Closer m_closer{nullptr};
  };

  inline H5Handle h5File(const hid_t id) noexcept { return {id, H5Fclose}; }
  inline H5Handle h5Group(const hid_t id) noexcept { return {id, H5Gclose}; }
  inline H5Handle h5Dataset(const hid_t id) noexcept { return {id, H5Dclose}; }
  inline H5Handle h5Space(const hid_t id) noexcept { return {id, H5Sclose}; }
  inline H5Handle h5Type(const hid_t id) noexcept { return {id, H5Tclose}; }
  inline H5Handle h5Attribute(const hid_t id) noexcept { return {id, H5Aclose}; }
  inline H5Handle h5Plist(const hid_t id) noexcept { return {id, H5Pclose}; }
  inline H5Handle h5Object(const hid_t id) noexcept { return {id, H5Oclose}; }

} // namespace anaf::IO::detail end
