#include <stdio.h>
#include <fcntl.h>     //open() 和打开选项
#include <unistd.h>    //close()
#include <sys/ioctl.h> //它提供ioctl()的声明和命令编码宏

// 在main()外面定义初始化命令

// 放在头文件下面

#define DHT1_IOCINIT _IOW('d', 0, int) // 它会生成一个整数命令号，驱动通过这个编号识别“初始化”请求

#define DHT_IOCGVALUE _IOWR('d', 2, void *)
// 这个当前DHT11驱动约定的命令
//  'd':命令类别
//  0:这个命令中的命令编号
//  int传递的参数类型
//  _IOW:这里表示应用向驱动传入数据

// 在main()外定义接收数据的结构体
//  这份驱动返回两个字节，湿度在前、温度在后

struct dht11_value {
    unsigned char humidity;
    unsigned char temerature;
};

// unsigned char在这里保存一个字节的非负整数

static int read_dht11(struct dht11_value *value)
{
    printf("DHT11 learning started \n");
    int pin = 115;
    printf("GPIO编号：%d\n", pin);
    printf("pin的地址：%p\n", (void *)&pin);

    int fd = open("/dev/dht11", O_RDWR);

    if (fd < 0) {
        perror("DHT11 打开失败");
        // 这里的fd会被后面的整数fd替换
        return 1;
    }
    printf("DHT11 打开成功 fd=%d\n", fd);
    if (ioctl(fd, DHT1_IOCINIT, &pin) < 0) {
        perror("DHT11 初始化失败");
        close(fd);
        return 1;
    }
    // 在初始化成功之后，‘close(fd)’之前，等待并准备变量
    sleep(3);
    //  value = {0};

    // 发出读取命令，并检查结果
    int ret = ioctl(fd, DHT_IOCGVALUE, value);

    if (ret < 0) {
        perror("读取温湿度失败");
        close(fd);
        return 1;
    }

    printf("湿度：%u %%RH,温度：%u ℃\n", (unsigned)value->humidity, (unsigned)value->temerature);

    close(fd); // 关闭设备

    return 0;
}

int main(void)
{
    struct dht11_value value = {0};
    int ret = read_dht11(&value);

    if (ret != 0) {
        return 1;
    }
    return 0;
}
