#include "anaf_info.h"

#include <stdio.h>
#include <stdarg.h>

static FILE* g_log_file = NULL;

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
            tag_color = ANAF_COLOR_GREEWN;
            break;
        case ANAF_LOG_CORE:
            tag = "[AMAFINEN]";
            tag_color = ANAF_COLOR_CYAN;
            break;
    }

    // terminal output
    fprintf(stream, "%s%s%s%s", ANAF_COLOR_BOLD, tag_color, tag, ANAF_COLOR_RESET);
    va_list args_term;
    va_start(args_term, fmt);
    vfprintf(stream, fmt, args_term);
    va_end(args_term);
    fputc('\n', stream);
    
    // file output
    if (g_log_file) {
        fprintf(g_log_file, "%s", tag);
        va_list args_file;
        va_start(args_file, fmt);
        vfprintf(g_log_file, fmt, args_file);
        va_end(args_file);
        fputc('\n', g_log_file);
        fflush(g_log_file);
    }
}
