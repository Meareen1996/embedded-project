#include <stdio.h>
#include "led.h"

int led_set_level(int level)
{
    FILE *fp = fopen("/sys/class/gpio/gpio131/value", "w");
    if (fp == NULL) {
        perror("open LED value");
        return -1;
    }
    /**
     * 文件的写入、关闭和错误检查，都由这个函数负责
     */
    int written = fprintf(fp, "%d", level);
    int closed = fclose(fp);
    if (written < 0 || closed != 0) {
        fprintf(stderr, "Failed to write LED level\n");
        return -1;
    }
    return 0;
}
