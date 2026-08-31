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

#ifndef ANAF_LOG_H
#define ANAF_LOG_H

#ifndef ANAF_INTERNAL_LOG_ACCESS
#error "Do not include 'anaf_info.h' directly! Include 'anaf_info.hpp' instead."
#endif

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#define ANAF_COLOR_RESET    "\033[0m"
#define ANAF_COLOR_BOLD     "\033[1m"
#define ANAF_COLOR_RED      "\033[31m"
#define ANAF_COLOR_GREEN   "\033[32m"
#define ANAF_COLOR_YELLOW   "\033[33m"
#define ANAF_COLOR_BLUE     "\033[34m"
#define ANAF_COLOR_CYAN     "\033[36m"

typedef enum{
    ANAF_LOG_INFO,
    ANAF_LOG_WARN,
    ANAF_LOG_ERROR,
    ANAF_LOG_SUCCESS,
    ANAF_LOG_CORE
} AnafLogLevel;

typedef void (*AnafLogCallbackFn)(AnafLogLevel level, const char* message);
void anaf_logger_set_callback(AnafLogCallbackFn cb);

bool anaf_log_init(const char* filepath);
void anaf_log_close(void);
void anaf_log_write(AnafLogLevel level, const char* fmt, ...);

#ifdef __cplusplus
}
#endif

#endif // ANAF_LOG_H
