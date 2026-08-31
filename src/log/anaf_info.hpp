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

#pragma once

#ifndef ANAF_INTERNAL_LOG_ACCESS
    #define ANAF_INTERNAL_LOG_ACCESS
#endif


#include "anaf_info.h"

#include <cstdarg>

namespace anaf::LOG {
    #if defined(__GNUC__) || defined(__clang__)
        #define ANAF_CXX_PRINTF_FORMAT __attribute__((format(printf, 1, 2)))
    #else
        #define ANAF_CXX_PRINTF_FORMAT
    #endif

    inline void setCallback(AnafLogCallbackFn cb) {
        anaf_logger_set_callback(cb);
    }

    inline bool init(const char* filepath) {
        return anaf_log_init(filepath);
    }

    inline void close() {
        anaf_log_close();
    }

    inline void ANAF_CXX_PRINTF_FORMAT info(const char* fmt, ...){
        va_list args;
        va_start(args, fmt);
        anaf_log_write_va(ANAF_LOG_INFO, fmt, args);
        va_end(args);
    }

    inline void ANAF_CXX_PRINTF_FORMAT warn(const char* fmt, ...){
        va_list args;
        va_start(args, fmt);
        anaf_log_write_va(ANAF_LOG_WARN, fmt, args);
        va_end(args);
    }

    inline void ANAF_CXX_PRINTF_FORMAT error(const char* fmt, ...){
        va_list args;
        va_start(args, fmt);
        anaf_log_write_va(ANAF_LOG_ERROR, fmt, args);
        va_end(args);
    }

    inline void ANAF_CXX_PRINTF_FORMAT success(const char* fmt, ...){
        va_list args;
        va_start(args, fmt);
        anaf_log_write_va(ANAF_LOG_SUCCESS, fmt, args);
        va_end(args);
    }

    inline void ANAF_CXX_PRINTF_FORMAT core(const char* fmt, ...){
        va_list args;
        va_start(args, fmt);
        anaf_log_write_va(ANAF_LOG_CORE, fmt, args);
        va_end(args);
    }

} // namespace anaf::LOG end
