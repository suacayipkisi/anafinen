// Copyright (c) 2026 Ufuk Deniz Konuk
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

#include "getExecutableDirectory.hpp"

#include <filesystem>

#ifdef __linux__
#include <unistd.h>
#include <linux/limits.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

namespace anaf::DIRECTORY {

    std::filesystem::path getExecutableDirectory(){
#ifdef __linux__
            char result[PATH_MAX];
            const ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
            if (count != -1) {
                return std::filesystem::path(std::string(result, count)).parent_path();
            }
#elif defined(_WIN32)
            char result[MAX_PATH];
            const DWORD count = GetModuleFileNameA(nullptr, result, MAX_PATH);
            if (count != 0) {
                return std::filesystem::path(std::string(result, count)).parent_path();
            }
#endif
            return std::filesystem::current_path();
    }

} // namespace anaf::DIRECTORY end



