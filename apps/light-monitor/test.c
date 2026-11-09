
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "light_state.h"

#define SENSOR_ADDRESS 0x1e
#define REG_MODE 0x00
#define REG_ALS_LOW 0x0c
#define REG_ALS_HIGH 0x0d
#define REG_ALS_CONFIG 0x10

static volatile sig_atomic_t stopping;

static void on_signal(int signo)
{
    (void)signo;
    stopping = 1;
}

static int sleep_ms(unsigned ms)
{
    struct timespec remaining = {ms / 1000, (long)(ms % 1000) * 1000000L};
    while (nanosleep(&remaining, &remaining) < 0) {
        if (errno != EINTR)
            return -1;
        if (stopping)
            break;
    }
    return 0;
}

static int read_register(int fd, uint8_t reg, uint8_t *value)
{

    struct i2c_msg message[2] = {
        {.addr = SENSOR_ADDRESS, .flags = 0, .len = 1, .buf = &reg},
        {.addr = SENSOR_ADDRESS, .flags = I2C_M_RD, .len = 1, .buf = value}};
    struct i2c_rdwr_ioctl_data transaction = {.msgs = message, .nmsgs = 2};
    int result;

    do {
        result = ioctl(fd, I2C_RDWR, &transaction);
    } while (result < 0 && errno == EINTR && !stopping);
    if (result == 2)
        return 0;
    if (result >= 0)
        errno = EIO;
    return -1;
}

static int write_register(int fd, uint8_t reg, uint8_t value)
{
    uint8_t bytes[2] = {reg, value};
    ssize_t result;

    do {
        result = write(fd, bytes, sizeof(bytes));
    } while (result < 0 && errno == EINTR && !stopping);
    if (result == (ssize_t)sizeof(bytes))
        return 0;
    if (result >= 0)
        errno = EIO;
    return -1;
}

static int number(const char *text, unsigned min, unsigned max, unsigned *value)
{
    char *end;
    errno = 0;
    unsigned long n = strtoul(text, &end, 10);

    if (!text[0] || text[0] == '-' || errno || *end || n < min || n > max)
        return -1;
    *value = (unsigned)n;
    return 0;
}

static void usage(FILE *out, const char *program)
{
    fprintf(out,
            "Usage: %s [--device /dev/i2c-0] [--count N] "
            "[--interval-ms 1000] [--dark-threshold 10] [--bright-threshold 100] [--hysteresis 5]\n"
            "N=0: continuous; raw counts only, NOT calibrated lux.\n",
            program);
}

int main(int argc, char **argv)
{
    const char *device = "/dev/i2c-0";

    unsigned count = 0, interval = 1000, threshold = 10, bright_threshold = 100, hysteresis = 5;
    const struct option options[] = {{"device", required_argument, NULL, 'd'},
                                     {"count", required_argument, NULL, 'c'},
                                     {"interval-ms", required_argument, NULL, 'i'},
                                     {"dark-threshold", required_argument, NULL, 't'},
                                     {"bright-threshold", required_argument, NULL, 'b'},
                                     {"hysteresis", required_argument, NULL, 'y'},
                                     {"help", no_argument, NULL, 'h'},
                                     {NULL, 0, NULL, 0}};

    int option;

    while ((option = getopt_long(argc, argv, "", options, NULL)) != -1) {
        int invalid = 0;

        switch (option) {
        case 'd':
            device = optarg;
            break;
        case 'c':
            invalid = number(optarg, 0, 1000000, &count);
            break;
        case 'i':
            invalid = number(optarg, 100, 60000, &interval);
            break;
        case 't':
            invalid = number(optarg, 0, 65535, &threshold);
            break;
        case 'b':
            invalid = number(optarg, 0, 65535, &bright_threshold);
            break;
        case 'y':
            invalid = number(optarg, 0, 65535, &hysteresis);
            break;
        case 'h':
            usage(stdout, argv[0]);
            return 0;
        default:
            usage(stderr, argv[0]);
            return 2;
        }
        if (invalid) {
            fprintf(stderr, "Invalid value: %s\n", optarg);
            return 2;
        }
    }

    if (optind != argc || bright_threshold < hysteresis ||
        threshold + hysteresis >= bright_threshold - hysteresis) {
        fprintf(stderr, "Require dark_threshold + hysteresis < bright_threshold - hysteresis\n");
        usage(stderr, argv[0]);
        return 2;
    }

    struct sigaction action = {0};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL)) {
        perror("sigaction");
        return 1;
    }

    if (sigaction(SIGHUP, &action, NULL)) {
        perror("sigaction");
        return 1;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);

    int fd = open(device, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror(device);
        return 1;
    }

    int status = 1;
    bool mode_saved = false;
    enum light_state state = LIGHT_NORMAL;
    uint8_t saved_mode = 0, config = 0;
    unsigned long funcs;

    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        perror("I2C device is already locked by another collector");
        goto cleanup;
    }

    if (ioctl(fd, I2C_SLAVE, SENSOR_ADDRESS) < 0 || ioctl(fd, I2C_FUNCS, &funcs) < 0) {
        perror("select AP3216C");
        goto cleanup;
    }

    if (!(funcs & I2C_FUNC_I2C)) {
        fprintf(stderr, "Adapter does not support combined I2C transfers\n");
        goto cleanup;
    }

    if (read_register(fd, REG_MODE, &saved_mode) || read_register(fd, REG_ALS_CONFIG, &config)) {
        perror("read AP3216C configuration");
        goto cleanup;
    }

    if ((saved_mode & 7) != 0 && (saved_mode & 7) != 1) {
        fprintf(stderr, "Sensor is in use (mode=0x%02x); stop its other user first\n", saved_mode);
        goto cleanup;
    }

    mode_saved = true;

    uint8_t als_mode = (saved_mode & 0xf8) | 1;
    if (write_register(fd, REG_MODE, als_mode)) {
        perror("enable ambient-light sensing");
        goto cleanup;
    }

    if (sleep_ms(200)) {
        perror("wait for first conversion");
        goto cleanup;
    }

    for (unsigned sample = 0; !stopping && (!count || sample < count); ++sample) {
        uint8_t low, high;

        if (read_register(fd, REG_ALS_LOW, &low) || read_register(fd, REG_ALS_HIGH, &high)) {
            perror("read light sample");
            goto cleanup;
        }

        uint16_t raw = (uint16_t)(((uint16_t)high << 8) | low);

        state = light_classify(raw, threshold, bright_threshold, hysteresis, state, sample == 0);

        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now)) {
            perror("clock_gettime");
            goto cleanup;
        }

        uint64_t uptime_ms = (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;

        if (printf("{\"sensor\":\"ap3216c\",\"sequence\":%u,"
                   "\"uptime_ms\":%" PRIu64 ",\"als_raw\":%u,"
                   "\"range_code\":%u,\"dark_threshold_raw\":%u,"
                   "\"bright_threshold_raw\":%u,\"hysteresis_raw\":%u,\"saturated\":%s,\"state\":"
                   "\"%s\"}\n",
                   sample + 1, uptime_ms, raw, (config >> 4) & 3, threshold, bright_threshold,
                   hysteresis, raw == 65535 ? "true" : "false", light_state_name(state)) < 0) {
            perror("write JSON output");
            goto cleanup;
        }

        if ((!count || sample + 1 < count) && sleep_ms(interval)) {
            perror("sample interval");
            goto cleanup;
        }
    }
    status = 0;

cleanup:

    if (mode_saved && write_register(fd, REG_MODE, saved_mode)) {
        perror("restore AP3216C mode");
        status = 1;
    }
    close(fd);
    return status;
}
