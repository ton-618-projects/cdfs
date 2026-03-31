#include "log.h"
#include <stdarg.h>
#include <time.h>

void cdfs_log_msg(log_level_t level, const uint8_t *component, const char *fmt, ...) {
    time_t rawtime;
    struct tm *timeinfo;
    uint8_t time_str[32];

    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime((char *)time_str, sizeof(time_str), "%Y-%m-%dT%H:%M:%S", timeinfo);

    const uint8_t *lvl_str;
    switch (level) {
        case LOG_LEVEL_INFO: lvl_str = (const uint8_t *)"INFO "; break;
        case LOG_LEVEL_WARN: lvl_str = (const uint8_t *)"WARN "; break;
        case LOG_LEVEL_ERR:  lvl_str = (const uint8_t *)"ERROR"; break;
        default:             lvl_str = (const uint8_t *)"DEBUG"; break;
    }

    fprintf(stdout, "[%s] [%s] [%s] ", (char *)time_str, (char *)lvl_str, (char *)component);

    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);

    // Add newline implicitly to match printf behavior mostly, but only if the user didn't already
    // Actually, to match existing printf calls which likely end in \n, we might get double newlines.
    // We'll leave it as is; we just replace `printf("[MD] foo\n")` to `LOG_INFO("MD", "foo\n")`.
}