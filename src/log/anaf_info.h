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
