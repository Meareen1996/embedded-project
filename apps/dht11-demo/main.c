#define _POSIX_C_SOURCE 200809L
/*
 * DHT11 入门：先检查设备，再初始化，最后读取温湿度。
 * 默认只检查设备，不操作 GPIO。接线确认后用 --read 读取5次。
 * 接口依据：100askTeam/arduino-imx6ull 的 DHT11 库和当前板载 v2.0 驱动。
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* 驱动的既有 ABI 使用 void* 编码命令号，必须按板子的32位 ABI 编译。 */
#define DHT11_IOCINIT   _IOW('d', 0, int)
#define DHT11_IOCGVALUE _IOWR('d', 2, void *)
_Static_assert(sizeof(void *) == 4, "Build this demo with the ARM32 toolchain");

/* 扩展板上 GPIO0 接口 → GPIO4_IO19 → Linux GPIO 编号115。
 * 这里的接口0不是 Linux GPIO 编号0！ */
enum { DHT11_GPIO = (4 - 1) * 32 + 19 };

/* 字段顺序是驱动约定：湿度在前、温度在后；不是传感器原始5字节。 */
struct dht11_value {
    uint8_t humidity;
    uint8_t temperature;
};
_Static_assert(sizeof(struct dht11_value) == 2, "Driver returns exactly two bytes");
static volatile sig_atomic_t stopped;
static void stop_demo(int sig) { (void)sig; stopped = 1; }
static void wait_ms(long ms) {
    struct timespec left = { ms / 1000, (ms % 1000) * 1000000 };
    while (!stopped && nanosleep(&left, &left) < 0 && errno == EINTR) {}
}

int main(int argc, char **argv) {
    int measure = 0;
    if (argc == 2 && !strcmp(argv[1], "--read")) measure = 1;
    else if (argc > 1 && !(argc == 2 && !strcmp(argv[1], "--check"))) {
        fprintf(stderr, "Usage: %s [--check | --read]\n", argv[0]); return 2;
    }

    /* 第一步：Linux 用设备文件表示驱动入口。stat只检查类型，不启动采样。 */
    struct stat info;
    if (stat("/dev/dht11", &info) < 0) { perror("/dev/dht11"); return 1; }
    if (!S_ISCHR(info.st_mode)) {
        fprintf(stderr, "/dev/dht11 is not a character device\n"); return 1;
    }
    puts("Device node OK: /dev/dht11 (character device).");
    if (!measure) {
        puts("Check only: no GPIO initialization or sensor read performed.");
        puts("After confirming wiring, run this program with --read.");
        return 0;
    }

    signal(SIGINT, stop_demo); signal(SIGTERM, stop_demo);
    signal(SIGHUP, stop_demo); signal(SIGPIPE, stop_demo);
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* 驱动使用全局状态；先锁独立文件，再打开设备。
     * 第二个进程不能先打开再关设备，否则可能停止第一个进程的采样。 */
    int lock = open("/tmp/dht11-demo.lock", O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (lock < 0) { perror("lock file"); return 1; }
    if (flock(lock, LOCK_EX | LOCK_NB) < 0) {
        perror("Another DHT11 demo is running"); close(lock); return 1;
    }
    int fd = open("/dev/dht11", O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("open DHT11"); close(lock); return 1; }

    /* 第二步：把GPIO编号交给驱动，由内核负责单线时序及定时采样。 */
    int result = 1;
    int pin = DHT11_GPIO;
    if (ioctl(fd, DHT11_IOCINIT, &pin) < 0) { perror("DHT11_IOCINIT"); goto done; }
    puts("Initialized GPIO4_IO19 (Linux GPIO115); waiting for driver sampling...");
    wait_ms(2500);

    /* 第三步：一次ioctl获取一对温湿度。当前v2.0驱动返回缓存，
     * 不提供采样时间或校验失败状态；ioctl成功不代表传感器本次响应成功。 */
    fprintf(stderr, "NOTE: driver cache only; freshness/validity is NOT reported.\n");
    int nonzero_samples = 0;
    for (int sample = 1; sample <= 5 && !stopped; ++sample) {
        struct dht11_value value = {0};
        if (ioctl(fd, DHT11_IOCGVALUE, &value) < 0) { perror("DHT11_IOCGVALUE"); goto done; }
        if (value.humidity == 0 && value.temperature == 0) {
            fprintf(stderr, "sample=%d: cache is 0/0; no usable measurement confirmed. Check wiring and dmesg.\n", sample);
        } else {
            ++nonzero_samples;
            printf("sample=%d humidity=%u %%RH temperature=%u C (driver cache)\n",
                   sample, (unsigned)value.humidity, (unsigned)value.temperature);
        }
        if (sample < 5) wait_ms(2000);
    }
    result = (stopped || nonzero_samples > 0) ? 0 : 1;
done:
    /* 第四步：关闭设备，驱动停止采样并释放GPIO。协作锁最后释放。 */
    if (close(fd) < 0) { perror("close DHT11"); result = 1; }
    close(lock);
    return result;
}
