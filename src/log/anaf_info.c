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

#include "anaf_info.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>

#define ANAF_LOG_BUFFER_SIZE 2048

static AnafLogCallbackFn g_anaf_log_callback = NULL;
static FILE* g_log_file = NULL;

void anaf_logger_set_callback(AnafLogCallbackFn cb) {
    g_anaf_log_callback = cb;
}

bool anaf_log_init(const char *filepath){
    if (!filepath) {
        return false;
    }
    g_log_file = fopen(filepath, "w");
    return g_log_file != NULL;
}

void anaf_log_close(void) {
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
}
void anaf_log_write(AnafLogLevel level, const char* fmt, ...){
    const char* tag = "";
    const char* tag_color = "";
    FILE* stream = stdout;
    
    switch (level) {
        case ANAF_LOG_INFO:
            tag = "[INFO]";
            tag_color = ANAF_COLOR_BLUE;
            break;
        case ANAF_LOG_WARN:
            tag = "[WARN]";
            tag_color = ANAF_COLOR_YELLOW;
            break;
        case ANAF_LOG_ERROR:
            tag = "[ERROR]";
            tag_color = ANAF_COLOR_RED;
            break;
        case ANAF_LOG_SUCCESS:
            tag = "[SUCCESS]";
            tag_color = ANAF_COLOR_GREEN;
            break;
        case ANAF_LOG_CORE:
            tag = "[ANAFINEN]";
            tag_color = ANAF_COLOR_CYAN;
            break;
    }

    // get current time
    time_t raw_time = time(NULL);
    struct tm time_info;
    localtime_r(&raw_time, &time_info);

    char time_str[16];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &time_info);

    char message_buffer[ANAF_LOG_BUFFER_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message_buffer, sizeof(message_buffer), fmt, args);
    va_end(args);

    // terminal output
    fprintf(stream, "[%s] %s%s%s%s %s\n", time_str, ANAF_COLOR_BOLD, tag_color, tag, ANAF_COLOR_RESET, message_buffer);
    
    // file output
    if (g_log_file) {
        fprintf(g_log_file, "[%s] %s %s\n", time_str, tag, message_buffer);
        fflush(g_log_file);
    }

    // UI callback dispatch
    if(g_anaf_log_callback) {
        char ui_formatted[ANAF_LOG_BUFFER_SIZE + 64];
        snprintf(ui_formatted, sizeof(ui_formatted), "[%s] %s %s", time_str, tag, message_buffer);
        g_anaf_log_callback(level, ui_formatted);
    }
}
