#ifndef SENSOR_COMMON_H
#define SENSOR_COMMON_H
/* 每个示例都能独立编译；这里共用参数、信号退出和 sysfs 读取。 */
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <glob.h>
#include <unistd.h>
static volatile sig_atomic_t stopped;
static void stop_demo(int sig) { (void)sig; stopped = 1; }
static inline void pause_ms(long ms) {
    struct timespec t = {ms / 1000, (ms % 1000) * 1000000};
    while (!stopped && nanosleep(&t, &t) < 0 && errno == EINTR) {}
}
static long number(const char *s, long min, long max) {
    char *end; errno = 0;
    long n = strtol(s, &end, 10);
    if (errno || !*s || *end || n < min || n > max) {
        fprintf(stderr, "Invalid number: %s (range %ld..%ld)\n", s, min, max);
        exit(2);
    }
    return n;
}
static inline void options(int argc, char **argv, long *count, long *interval, const char **sensor) {
    *count = 5; *interval = 500;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--count") && i + 1 < argc)
            *count = number(argv[++i], 0, 1000000);
        else if (!strcmp(argv[i], "--interval-ms") && i + 1 < argc)
            *interval = number(argv[++i], 20, 60000);
        else if (sensor && !strcmp(argv[i], "--sensor") && i + 1 < argc)
            *sensor = argv[++i];
        else {
            fprintf(stderr, "Usage: %s [--count N (0=continuous)] [--interval-ms 20..60000]%s\n",
                    argv[0], sensor ? " [--sensor NAME]" : "");
            exit(2);
        }
    }
    signal(SIGINT, stop_demo); signal(SIGTERM, stop_demo);
    /* Mac 终端 Ctrl+C 可能先关闭 adb/PTY，再由板端收到 HUP/PIPE。 */
    signal(SIGHUP, stop_demo); signal(SIGPIPE, stop_demo);
    setvbuf(stdout, NULL, _IOLBF, 0);
}
static inline double read_value(const char *root, const char *attribute) {
    char path[PATH_MAX]; double n;
    if (snprintf(path, sizeof(path), "%s/%s", root, attribute) >= (int)sizeof(path)) {
        fprintf(stderr, "Path too long\n"); exit(1);
    }
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(1); }
    int ok = fscanf(f, "%lf", &n);
    int closed = fclose(f);
    if (ok != 1 || closed || !isfinite(n)) {
        fprintf(stderr, "Cannot read numeric attribute: %s\n", path); exit(1);
    }
    return n;
}
static inline void find_iio(const char *name, char *result, size_t size) {
    glob_t paths = {0};
    int rc = glob("/sys/bus/iio/devices/iio:device*/name", 0, NULL, &paths);
    if (rc == 0) for (size_t i = 0; i < paths.gl_pathc; ++i) {
        char actual[128] = {0};
        FILE *f = fopen(paths.gl_pathv[i], "r");
        if (!f) continue;
        int ok = fscanf(f, "%127s", actual); fclose(f);
        if (ok == 1 && !strcmp(actual, name)) {
            snprintf(result, size, "%s", paths.gl_pathv[i]);
            char *slash = strrchr(result, '/'); if (slash) *slash = '\0';
            globfree(&paths); return;
        }
    }
    globfree(&paths);
    fprintf(stderr, "IIO device not found: %s\n", name); exit(1);
}
#endif
