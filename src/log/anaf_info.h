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

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ANAF_COLOR_RESET    "\033[0m"
#define ANAF_COLOR_BOLD     "\033[1m"
#define ANAF_COLOR_RED      "\033[31m"
#define ANAF_COLOR_GREEWN   "\033[32m"
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

bool anaf_log_init(const char* filepath);
void anaf_log_close(void);
void anaf_log_write(AnafLogLevel level, const char* fmt, ...);

#define anaf_info(...)    anaf_log_write(ANAF_LOG_INFO, ##__VA_ARGS__)
#define anaf_warn(...)    anaf_log_write(ANAF_LOG_WARN, ##__VA_ARGS__)
#define anaf_error(...)   anaf_log_write(ANAF_LOG_ERROR, ##__VA_ARGS__)
#define anaf_success(...) anaf_log_write(ANAF_LOG_SUCCESS, ##__VA_ARGS__)
#define anaf_core(...)    anaf_log_write(ANAF_LOG_CORE, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif

#endif // ANAF_LOG_H
