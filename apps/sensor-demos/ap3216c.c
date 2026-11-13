#define _POSIX_C_SOURCE 200809L
#include "common.h"
#include <fcntl.h>
#include <stdint.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
/* AP3216C: I2C0 地址 0x1e；先读低字节，锁存对应高字节。 */
static int read_reg(int fd, uint8_t reg, uint8_t *value) {
    struct i2c_msg msgs[2] = {
        {.addr=0x1e, .len=1, .buf=&reg},
        {.addr=0x1e, .flags=I2C_M_RD, .len=1, .buf=value}
    };
    struct i2c_rdwr_ioctl_data data = {.msgs=msgs, .nmsgs=2};
    int rc;
    do { rc = ioctl(fd, I2C_RDWR, &data); } while (rc < 0 && errno == EINTR);
    if (rc == 2) return 0;
    if (rc >= 0) errno = EIO;
    return -1;
}
static int write_mode(int fd, uint8_t mode) {
    uint8_t bytes[2] = {0, mode}; ssize_t n;
    do { n = write(fd, bytes, sizeof(bytes)); } while (n < 0 && errno == EINTR);
    if (n == 2) return 0;
    if (n >= 0) errno = EIO;
    return -1;
}
int main(int argc, char **argv) {
    long count, interval; const char *sensor = "all";
    options(argc, argv, &count, &interval, &sensor);
    int als = !strcmp(sensor,"all") || !strcmp(sensor,"als");
    int ir = !strcmp(sensor,"all") || !strcmp(sensor,"ir");
    int ps = !strcmp(sensor,"all") || !strcmp(sensor,"proximity");
    if (!als && !ir && !ps) { fprintf(stderr,"sensor: all/als/ir/proximity\n"); return 2; }
    int fd = open("/dev/i2c-0", O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror("/dev/i2c-0"); return 1; }
    int result = 1, saved = 0; uint8_t old_mode = 0;
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) { perror("I2C busy (stop other collectors)"); goto done; }
    unsigned long funcs;
    if (ioctl(fd, I2C_FUNCS, &funcs) < 0 || !(funcs & I2C_FUNC_I2C)) {
        fprintf(stderr,"Adapter must support I2C combined transactions\n"); goto done;
    }
    if (ioctl(fd, I2C_SLAVE, 0x1e) < 0 || read_reg(fd, 0, &old_mode) < 0) {
        perror("AP3216C setup"); goto done;
    }
    if ((old_mode & 7) > 3) { fprintf(stderr,"Chip is in reset/one-shot mode; try later\n"); goto done; }
    saved = 1;
    /* 保留其它位，开启 ALS + PS + IR；退出时恢复原模式，不重置校准。 */
    if (write_mode(fd, (old_mode & 0xf8) | 3) < 0) { perror("enable AP3216C"); goto done; }
    fprintf(stderr,"AP3216C /dev/i2c-0 @0x1e; raw counts, NOT lux or cm. Ctrl+C stops.\n");
    pause_ms(200);
    for (long n = 0; !stopped && (!count || n < count); ++n) {
        uint8_t b[6];
        for (int i = 0; i < 6; ++i) if (read_reg(fd, (uint8_t)(0x0a+i), &b[i]) < 0) {
            perror("read AP3216C"); goto done;
        }
        int ir_valid = !(b[0] & 0x80);
        int ps_valid = ir_valid && !(b[4] & 0x40) && !(b[5] & 0x40);
        printf("{\"sample\":%ld", n+1);
        if (als) printf(",\"als_raw\":%u", ((unsigned)b[3]<<8)|b[2]);
        if (ir) {
            printf(",\"ir_valid\":%s,\"ir_raw\":", ir_valid ? "true":"false");
            if (ir_valid) printf("%u", ((unsigned)b[1]<<2)|(b[0]&3)); else printf("null");
        }
        if (ps) {
            printf(",\"proximity_valid\":%s,\"proximity_raw\":", ps_valid ? "true":"false");
            if (ps_valid) printf("%u", ((unsigned)(b[5]&0x3f)<<4)|(b[4]&15)); else printf("null");
            printf(",\"object_flag\":%s", (b[5]&0x80) ? "true":"false");
        }
        puts("}");
        if (!count || n+1 < count) pause_ms(interval);
    }
    result = 0;
done:
    if (saved && write_mode(fd, old_mode) < 0) { perror("restore AP3216C mode"); result = 1; }
    if (close(fd) < 0) { perror("close I2C"); result = 1; }
    return result;
}
