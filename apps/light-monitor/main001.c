/*
 * AP3216C 光线采集参考程序 —— 给第一次接触嵌入式 Linux 的读者
 *
 * 这是一份用户态采集程序，还不是包含后台、网页和告警功能的完整产品。
 * 它在 Ubuntu 里交叉编译，在 I.MX6ULL 开发板上运行。
 * Mac 的 adb 负责传文件和发送运行命令；真正访问传感器的是板子上的本程序。
 *
 * 一、先把三个“地址/编号”分清
 *   /dev/i2c-0  ：Linux 的 0 号 I²C 通道，不是传感器。
 *   0x1E        ：这条总线上 AP3216C 的器件地址，选择“找谁”。
 *   0x00/0x0C…  ：AP3216C 内部寄存器地址，选择“找它里面的哪里”。
 *   它们不相加，也不需要在这里填写处理器控制器基地址 0x021A0000。
 *   Linux 的 I²C 控制器驱动负责实际产生 SCL 时钟、收发 SDA 数据。
 *
 * 二、你已经手动执行过的步骤，与本程序对应如下
 *   i2cset -y 0 0x1e 0x00 0x01 b  -> write_register() 开启光线测量。
 *   i2cget -y 0 0x1e 0x0c b       -> read_register() 读取低字节。
 *   i2cget -y 0 0x1e 0x0d b       -> read_register() 读取高字节。
 *   high * 256 + low              -> 合并为 uint16_t raw。
 *   echo / sleep / while          -> printf() / sleep_ms() / for 循环。
 *   这里通过 Linux I²C 接口完成与命令行工具等价的寄存器操作，
 *   并不是在 C 代码里调用 i2cget 命令。
 *
 * 三、建议阅读顺序，不必第一次就从头理解所有细节
 *   1. 先找 main()：打开设备 -> 选择传感器 -> 开启测量 -> 循环读取。
 *   2. 看 low/high 如何组成 raw，再看 JSON 输出。
 *   3. 回头看 read_register()、write_register()，理解 Linux I²C 接口。
 *   4. 最后再看命令行参数、信号、锁和 cleanup；这些是辅助与健壮性处理。
 *   光线偏暗判断在同目录 light_state.h 中，可在采集成功后再学习。
 *
 * 四、寄存器依据：AP3216C 芯片数据手册
 *   第 10 页：7 位器件地址 0x1E。
 *   第 11～13 页：寄存器表，以及 0x00 低三位的工作模式。
 *   第 16 页：先读 0x0C 低字节，再读 0x0D 高字节。
 *   输出的是 0～65535 的原始计数，不直接称为 lux；量程和校准另行处理。
 *
 * 五、在开发板上运行的例子
 *   ./light-monitor --count 5               采集五次后退出。
 *   ./light-monitor --count 0               持续采集，Ctrl+C 请求停止。
 *   ./light-monitor --dark-threshold 10 --hysteresis 5
 *                                          低于 10 判暗，达到 15 才恢复。
 *   ./light-monitor --help                 查看参数说明。
 *   编译方式参见 scripts/build-light-monitor.sh；初学重写时可以先只实现
 *   open -> ioctl -> 写模式 -> 读数据 -> printf -> close 这条最短路径。
 */

/* 功能测试宏：在头文件被包含前，声明要使用 POSIX.1-2008 提供的接口。
 * 让编译器能看到 nanosleep、sigaction、O_CLOEXEC 等声明；不是 CPU 配置。 */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>       /* errno 和 EINTR/EIO：查询系统调用失败原因。 */
#include <fcntl.h>       /* open() 以及 O_RDWR、O_CLOEXEC。 */
#include <getopt.h>      /* getopt_long()：解析 --count 等命令行选项。 */
#include <inttypes.h>    /* PRIu64：可移植地打印 64 位无符号整数。 */
#include <linux/i2c-dev.h> /* Linux 用户态 I²C 的 ioctl 命令和事务结构。 */
#include <linux/i2c.h>   /* struct i2c_msg、I2C_M_RD、I2C_FUNC_I2C。 */
#include <signal.h>      /* 接收 Ctrl+C 等信号，安排退出。 */
#include <stdbool.h>     /* bool、true、false。 */
#include <stdint.h>      /* uint8_t/uint16_t/uint64_t：确定位宽的整数。 */
#include <stdio.h>       /* printf、fprintf、perror 等输入输出函数。 */
#include <stdlib.h>      /* strtoul：把参数文字转换成整数。 */
#include <sys/file.h>    /* flock：配合使用的进程之间的文件锁。 */
#include <sys/ioctl.h>   /* ioctl：向设备驱动提交控制请求。 */
#include <time.h>        /* nanosleep、clock_gettime、struct timespec。 */
#include <unistd.h>      /* write、close 等 POSIX 接口。 */

/* 双引号表示项目自己的头文件；里面是“偏暗/正常”的判断函数。 */
#include "light_state.h"

/* 宏：给固定数值起名字。后面用名字，便于阅读和对照手册。
 * SENSOR_ADDRESS 是 7 位地址；不手动左移，不自行附加读写位。
 * 控制器驱动会根据下面的读/写操作处理总线上的地址字节。 */
#define SENSOR_ADDRESS 0x1e
#define REG_MODE 0x00       /* 工作模式：低三位 000=待机，001=仅测光。 */
#define REG_ALS_LOW 0x0c    /* 光线原始值的低 8 位，只读数据。 */
#define REG_ALS_HIGH 0x0d   /* 同一个光线原始值的高 8 位。 */
#define REG_ALS_CONFIG 0x10 /* 光线配置，位 5:4 表示量程档位。 */

/* 文件作用域变量默认初始化为 0，表示“还不需要停止”。
 * static：本变量只在这个 .c 文件内可见。
 * volatile：每次使用要实际读取，避免编译器忽略信号处理函数带来的变化。
 * sig_atomic_t：适合在信号处理函数与正常流程间传递简单标记的整数类型。
 * 这里不是硬件寄存器；volatile 也不等于通用的线程锁或线程同步机制。 */
static volatile sig_atomic_t stopping;

/* 收到信号时只设置标记，不在这里 printf、访问 I²C 或释放资源。
 * 信号可能打断程序的任何位置，这里只做简单、适合信号处理的操作。
 * main() 观察到 stopping 后，在正常执行流程中退出并清理资源。 */
static void on_signal(int signo)
{
    (void)signo; /* 明确表示本函数不使用信号编号，避免未使用参数警告。 */
    stopping = 1;
}

/* 等待若干毫秒。成功返回 0，发生非信号类错误返回 -1。
 * unsigned 表示无符号整数，本程序只允许非负等待时间。
 * 该函数让循环“等一等”，不会忙循环持续占用 CPU。 */
static int sleep_ms(unsigned ms)
{
    /* timespec 的两个字段是秒、纳秒。比如 1500ms = 1秒 + 500000000纳秒。
     * / 是整数除法，% 是取余，末尾 L 表示 long 类型常量。 */
    struct timespec remaining = {ms / 1000, (long)(ms % 1000) * 1000000L};
    /* &remaining 是“这个结构体所在的地址”。
     * 第一个参数提供计划等待时长；被信号打断时，第二个参数接收剩余时长。 */
    while (nanosleep(&remaining, &remaining) < 0) {
        /* 系统调用失败后看 errno。EINTR 表示被信号打断，并非硬件损坏。 */
        if (errno != EINTR)
            return -1;
        if (stopping)
            break; /* 正在退出就不必继续等待，否则按剩余时长继续睡眠。 */
    }
    return 0;
}

/* 读取传感器内部的一个字节。
 *   fd：已经打开的 /dev/i2c-N 的文件描述符。
 *   reg：传感器内部寄存器地址，如 0x0C。
 *   value：调用者提供的一个 uint8_t 变量的地址，用于接收结果。
 * 返回值：0 表示读取成功；-1 表示失败，errno 保存错误原因。
 *
 * 数据如何返回？函数返回值只表示成败，实际数据通过 value 指针写回。
 * 例如 read_register(fd, REG_ALS_LOW, &low)，把结果存入 main() 的 low。
 *
 * 总线过程：向 0x1E 写入“要读的寄存器地址”，随后 repeated START，
 * 再从同一个 0x1E 读取数据；中间不发送 STOP。这是一个组合事务。
 * 第一个“写”只是指定读取位置，不是在改变该寄存器保存的值。 */
static int read_register(int fd, uint8_t reg, uint8_t *value)
{
    /* 两条消息：一条发送寄存器地址，一条接收数据。
     * {.字段名 = 值} 是 C 的指定初始化写法，不要求死记字段顺序。
     * flags=0 表示写消息；I2C_M_RD 表示读消息；len 的单位是字节。
     * &reg 指向本函数的寄存器地址变量；value 指向调用者的数据变量。 */
    struct i2c_msg messages[2] = {
        {.addr = SENSOR_ADDRESS, .flags = 0, .len = 1, .buf = &reg},
        {.addr = SENSOR_ADDRESS, .flags = I2C_M_RD, .len = 1, .buf = value}
    };
    /* 用一个结构体把两条消息打包，交给驱动一次执行。
     * messages 在这里提供数组首元素地址；nmsgs 是消息数量，不是字节数。 */
    struct i2c_rdwr_ioctl_data transaction = {.msgs = messages, .nmsgs = 2};
    int result;
    do {
        /* ioctl(fd, 请求类型, 参数)：通过设备描述符向驱动发送请求。
         * I2C_RDWR 表示执行组合传输，&transaction 提供传输内容。
         * 驱动成功完成两条消息时返回 2；不是返回传感器测量值。 */
        result = ioctl(fd, I2C_RDWR, &transaction);
    } while (result < 0 && errno == EINTR && !stopping);
    /* && 是“并且”，!stopping 是“没有请求停止”。仅在适合重试时重试。 */
    if (result == 2)
        return 0;
    /* 非负但未完成预期全部消息，按不完整传输处理，设为输入/输出错误。
     * 若驱动已经返回 -1，则保留它设置的原始 errno。 */
    if (result >= 0)
        errno = EIO;
    return -1;
}

/* 写入传感器的一个寄存器。reg 是“写到哪里”，value 是“写什么”。
 * 对应 i2cset -y 0 0x1e 0x00 0x01 b：reg=0x00，value=0x01。
 * 此处 write() 使用 main() 通过 I2C_SLAVE 选中的器件地址。
 * 与上面的 I2C_RDWR 不同，write() 不在每条消息里单独填写器件地址。 */
static int write_register(int fd, uint8_t reg, uint8_t value)
{
    /* 发送顺序是 [寄存器地址, 寄存器值]；两者绝不相加。
     * uint8_t 是 8 位无符号整数，可以保存 0～255。 */
    uint8_t bytes[2] = {reg, value};
    /* ssize_t 能保存 write() 返回的字节数，也能表示错误值 -1。 */
    ssize_t result;
    do {
        /* sizeof(bytes) 在这里是 2：数组恰好包含两个一字节元素。
         * write() 负责传输；它不等于把程序文件保存到 eMMC。 */
        result = write(fd, bytes, sizeof(bytes));
    } while (result < 0 && errno == EINTR && !stopping);
    /* 只有两个字节都发送成功才算成功，不能把短写误当成成功。
     * (ssize_t) 是显式类型转换，使比较两边的类型保持一致。 */
    if (result == (ssize_t)sizeof(bytes))
        return 0;
    if (result >= 0)
        errno = EIO;
    return -1;
}

/* 把 --count 5 中的字符串 "5" 转成数值，同时检查范围。
 * const char *text：指向输入字符串，函数不会修改字符串。
 * unsigned *value：输出参数，成功时通过 *value 写入结果。
 * 与 read_register 的 value 用法相同：通过指针把结果带回调用者。
 * min/max 是允许的边界，返回 0/-1 表示成功/失败。 */
static int number(const char *text, unsigned min, unsigned max, unsigned *value)
{
    char *end; /* strtoul 会把“未成功解析的剩余文字”位置存到 end。 */
    errno = 0; /* 先清零，才能判断这次转换是否产生范围等错误。 */
    /* 10 表示按十进制解析命令行参数，这里没有按十六进制解析参数。
     * &end 是指针变量自身的地址，让 strtoul 可以修改 end 的指向。 */
    unsigned long n = strtoul(text, &end, 10);
    /* || 是“或者”。依次拒绝空串、以负号开头、转换出错、尾部残留字符、
     * 超出范围。例如 "abc"、"12abc"、"-1" 不应被接受为采样次数。
     * C 字符串以 '\0' 结尾；*end 非零表示还有未解析的字符。 */
    if (!text[0] || text[0] == '-' || errno || *end || n < min || n > max)
        return -1;
    *value = (unsigned)n; /* *value 是解引用：写入调用者提供的变量。 */
    return 0;
}

/* 帮助信息：out 可以是 stdout（正常输出）或 stderr（错误输出）。
 * program 通常来自 argv[0]，也就是启动本程序时使用的程序名/路径。
 * 多行相邻字符串会在编译时自动拼接，不需要手动使用 +。 */
static void usage(FILE *out, const char *program)
{
    fprintf(out, "Usage: %s [--device /dev/i2c-0] [--count N] "
            "[--interval-ms 1000] [--dark-threshold 10] [--hysteresis 5]\n"
            "N=0: continuous; raw counts only, NOT calibrated lux.\n", program);
}

/* 程序入口。前面的函数只是定义，main() 中调用它们才会执行。
 * argc 是命令行参数个数，包含程序名；argv 保存各个参数字符串。
 * 例如 ./light-monitor --count 5：argc=3，argv[1]="--count"。
 * main 返回值是进程退出码：本程序约定 0=成功，1=运行失败，2=参数错误。 */
int main(int argc, char **argv)
{
    /* 第 1 步：准备配置。下面都是本进程内存中的变量，不会自动持久化。
     * 路径属于“运行程序的板子”，不是 Mac 或 Ubuntu 的 /dev。
     * 0 号通道是针对当前开发板系统确认的默认值，其他系统需重新核对。 */
    const char *device = "/dev/i2c-0";
    /* 默认持续采集，每轮输出后等待 1000ms；不是硬件定时器精确定时。
     * threshold/hysteresis 是演示用的原始计数阈值，并非 lux 或通用标准。
     * 低于 10 -> 偏暗；已经偏暗时，要达到 10+5=15 才恢复正常。 */
    unsigned count = 0, interval = 1000, threshold = 10, hysteresis = 5;
    /* 长选项表：选项名、是否需要参数、flag、内部返回的标记字符。
     * required_argument：后面必须有值；no_argument：后面不需要值。
     * 最后一行全零是表尾标记。
     * 'd'/'c' 等是内部 switch 使用的标记；本程序没有启用 -d/-c 短选项。 */
    const struct option options[] = {
        {"device", required_argument, NULL, 'd'},
        {"count", required_argument, NULL, 'c'},
        {"interval-ms", required_argument, NULL, 'i'},
        {"dark-threshold", required_argument, NULL, 't'},
        {"hysteresis", required_argument, NULL, 'y'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };
    int option;
    /* 第 2 步：逐个解析命令行选项，处理完成返回 -1，结束循环。
     * 这里括号中的 = 是赋值；!= 才是判断“不相等”。 */
    while ((option = getopt_long(argc, argv, "", options, NULL)) != -1) {
        int invalid = 0;
        /* optarg 由 getopt_long 提供，指向当前选项的值，例如 "5"。
         * &count 等把变量地址传给 number()，让函数可以更新该变量。 */
        switch (option) {
        case 'd': device = optarg; break;
        case 'c': invalid = number(optarg, 0, 1000000, &count); break;
        case 'i': invalid = number(optarg, 100, 60000, &interval); break;
        case 't': invalid = number(optarg, 0, 65535, &threshold); break;
        case 'y': invalid = number(optarg, 0, 65535, &hysteresis); break;
        case 'h': usage(stdout, argv[0]); return 0;
        default: usage(stderr, argv[0]); return 2;
        }
        if (invalid) {
            fprintf(stderr, "Invalid value: %s\n", optarg);
            return 2;
        }
    }
    /* optind 是下一个未处理参数的位置。这里拒绝多余的位置参数。
     * 光线原始值最大 65535，恢复阈值也必须在这个可表示范围内。 */
    if (optind != argc || threshold + hysteresis > 65535) {
        usage(stderr, argv[0]);
        return 2;
    }

    /* 第 3 步：注册退出信号。
     * {0} 将结构体初始化为零；sa_handler 指定收到信号后调用哪个函数。
     * SIGINT 通常来自 Ctrl+C；SIGTERM 通常来自 kill 的默认终止请求。 */
    struct sigaction action = {0};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask); /* 不额外屏蔽其他信号。 */
    if (sigaction(SIGINT, &action, NULL) || sigaction(SIGTERM, &action, NULL)) {
        perror("sigaction");
        return 1;
    }
    /* SIGHUP 可能在终端会话断开时送达，也让程序尝试走正常清理路径。
     * 这不是保证：断电、SIGKILL(kill -9)、某些断连情况无法靠本处理函数恢复。
     * 必须区分“请求退出”和“无条件立刻杀死”，这里处理的是前者。 */
    if (sigaction(SIGHUP, &action, NULL)) {
        perror("sigaction");
        return 1;
    }
    /* 按行缓冲：每输出换行就及时显示，便于 adb 里观察实时 JSON。
     * 正常数据走 stdout；perror/fprintf(stderr, ...) 的诊断走 stderr。 */
    setvbuf(stdout, NULL, _IOLBF, 0);
    /* 第 4 步：打开 I²C 设备文件。open 成功返回非负整数 fd，失败返回 -1。
     * fd 是 Linux 为这次打开分配的“访问凭证”，不是总线地址/传感器数据。
     * O_RDWR：允许读写；O_CLOEXEC：未来若执行另一个程序，不泄漏此描述符。
     * | 在这里合并互不重叠的选项位；不是 Shell 的管道符。
     * 只有打开入口还不代表已找到 AP3216C；通信是否成功要看后面的读写。 */
    int fd = open(device, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror(device); /* 输出路径和 errno 对应的原因，例如权限不足。 */
        return 1;
    }
    /* 先假设失败，完整采集结束才设成成功；这些变量也供 cleanup 使用。 */
    int status = 1;
    bool mode_saved = false, dark = false; /* 是否需恢复模式，以及前次明暗状态。 */
    uint8_t saved_mode = 0, config = 0; /* 保存进入时的模式和量程配置。 */
    unsigned long funcs; /* 位掩码：适配器支持哪些 I²C 操作。 */
    /* 第 5 步：尝试对设备文件加“排他、非阻塞”锁。
     * LOCK_EX：同样遵守此锁的其他进程不能同时持有锁。
     * LOCK_NB：拿不到就报错，不一直等待。
     * 这是建议锁，只协调也使用 flock 的程序；不能阻止 i2cget 等不加锁工具，
     * 也不能代替驱动管理。范围是整个设备文件，不仅仅是地址 0x1E。
     * 因而运行采集时不要同时用其他程序操作这个传感器。 */
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        perror("I2C device is already locked by another collector");
        goto cleanup;
    }
    /* 第 6 步：选择器件，并查询适配器能力。
     * I2C_SLAVE 设置后，write() 将发往 7 位地址 0x1E。
     * I2C_FUNCS 把支持能力写入 funcs（所以传 &funcs）。
     * 不使用 I2C_SLAVE_FORCE：若内核驱动已占用该地址，普通选择会拒绝访问。
     * 注意，选中地址本身并不验证器件存在；后面的真正传输才有应答结果。
     * read_register 中 I2C_RDWR 的每条消息仍需显式填写自己的 .addr。 */
    if (ioctl(fd, I2C_SLAVE, SENSOR_ADDRESS) < 0 ||
        ioctl(fd, I2C_FUNCS, &funcs) < 0) {
        perror("select AP3216C");
        goto cleanup;
    }
    /* & 在两个整数之间是“按位与”，用于检查是否存在某个能力位。
     * 与 &funcs 的“取地址”是同一个符号的不同语法用法。
     * 不支持组合传输就退出，不静默采用可能不兼容的方式。 */
    if (!(funcs & I2C_FUNC_I2C)) {
        fprintf(stderr, "Adapter does not support combined I2C transfers\n");
        goto cleanup;
    }
    /* 第 7 步：先读原设置，再改工作模式。|| 会短路：前一次失败就不继续。
     * 把原设置保存在内存中，结束时尽量还原；config 这里只读，不改变量程。 */
    if (read_register(fd, REG_MODE, &saved_mode) ||
        read_register(fd, REG_ALS_CONFIG, &config)) {
        perror("read AP3216C configuration");
        goto cleanup;
    }
    /* 7 = 二进制 00000111，按位与只保留工作模式的低三位。
     * 仅接受原模式为待机(0)或仅 ALS(1)，其他模式保守拒绝，避免打断接近测量。
     * 这只是模式检查，不能完整判断是否有别的进程正使用传感器。 */
    if ((saved_mode & 7) != 0 && (saved_mode & 7) != 1) {
        fprintf(stderr, "Sensor is in use (mode=0x%02x); stop its other user first\n",
                saved_mode);
        goto cleanup;
    }
    /* 从这里开始，清理时需要尝试还原，写入失败也可能需要恢复。
     * bool 为 true 并不代表数据保存到文件，只是内存中的一个标记。 */
    mode_saved = true;
    /* 0xF8 = 11111000，保留高五位、清除低三位，再 |1 把模式设为 001。
     * 这叫“保留无关位，只修改目标位”，不要为了配置低三位乱改其他位。 */
    uint8_t als_mode = (saved_mode & 0xf8) | 1;
    if (write_register(fd, REG_MODE, als_mode)) {
        perror("enable ambient-light sensing");
        goto cleanup;
    }
    /* 第 8 步：先等第一次转换。开启功能不表示测量结果立刻有效。
     * 手册给出 ALS 典型转换时间 100ms，这里初次等待 200ms。 */
    if (sleep_ms(200)) {
        perror("wait for first conversion");
        goto cleanup;
    }
    /* 第 9 步：循环采样。
     * sample 从 0 开始，++sample 每轮加 1。
     * !stopping：没有收到退出请求。
     * !count：count 为 0 时成立，代表不限次数；否则要求 sample < count。
     * 这是 while true + sleep 的 C 版本，只是额外支持有限次数和退出处理。 */
    for (unsigned sample = 0; !stopping && (!count || sample < count); ++sample) {
        uint8_t low, high; /* 各一字节；成功读完两者后才会使用。 */
        /* 必须先低后高：读低字节时，芯片把当时的高字节暂存，之后再读高字节。
         * 即使传感器期间更新测量结果，两次取出的字节仍对应同一个样本。
         * “锁存”是传感器内部功能，与上面的 Linux 文件锁不同。 */
        if (read_register(fd, REG_ALS_LOW, &low) ||
            read_register(fd, REG_ALS_HIGH, &high)) {
            perror("read light sample");
            goto cleanup;
        }
        /* 合并数据，等价于你在 Shell 中写的 high * 256 + low。
         * uint16_t：16 位无符号整数，范围 0～65535。
         * 先把 high 转成 16 位再左移 8 位，把它放在结果的高八位；
         * | low 把低八位填进去。两部分不重叠，所以此处按位或等价于相加。
         * 示例 high=0x01、low=0x22 -> raw=0x0122=290。
         * 这是数据组合，不是在加器件地址或寄存器地址。 */
        uint16_t raw = (uint16_t)(((uint16_t)high << 8) | low);
        /* 使用前一次状态做“回差/滞回”判断，函数在 light_state.h。
         * sample==0 标记第一次采样，第一次没有可参考的前一次状态。
         * dark 是软件根据阈值判断的业务状态，传感器不会直接告诉我们“房间暗”。 */
        dark = light_is_dark(raw, threshold, hysteresis, dark, sample == 0);
        /* 单调时钟用于记录样本时间顺序，不是年月日形式的现实时间。
         * Linux 的 CLOCK_MONOTONIC 不受手动校时影响，通常也不累计挂起时长。 */
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now)) {
            perror("clock_gettime");
            goto cleanup;
        }
        /* 秒转毫秒，再把纳秒转毫秒；先提升到 64 位，避免较小整数溢出。 */
        uint64_t uptime_ms = (uint64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
        /* 第 10 步：每行一个 JSON，未来后端或网页可以按行解析。
         * \" 是 C 字符串里表示双引号的转义；实际输出没有这些反斜杠。
         * %u 对应无符号数；PRIu64 为当前平台提供正确的 64 位整数格式。
         * 条件 ? A : B 是三元运算符，例如 dark ? "dark" : "normal"。
         *
         * 字段说明：
         *   sequence          从 1 开始的本次进程采样序号。
         *   uptime_ms         单调时钟毫秒值，便于计算两次采样间隔。
         *   als_raw           传感器原始计数，不是 lux。
         *   range_code        (config >> 4) & 3，取配置寄存器的位 5:4。
         *   dark_threshold_raw / hysteresis_raw  当前软件阈值与回差。
         *   saturated         是否恰好达到数字满量程 65535，作为提示；
         *                     并不能据此保证未满量程时的物理照度一定准确。
         *   state             软件判断的 dark/normal。
         * printf 失败时可能是输出通道出错，本函数尝试转去 cleanup。
         * 本版本没有为 SIGPIPE 单独注册处理函数，所以某些管道断开情况
         * 仍可能直接终止进程；不要把下面的恢复逻辑视为绝对保证。
         */
        if (printf("{\"sensor\":\"ap3216c\",\"sequence\":%u,"
                   "\"uptime_ms\":%" PRIu64 ",\"als_raw\":%u,"
                   "\"range_code\":%u,\"dark_threshold_raw\":%u,"
                   "\"hysteresis_raw\":%u,\"saturated\":%s,\"state\":\"%s\"}\n",
                   sample + 1, uptime_ms, raw, (config >> 4) & 3,
                   threshold, hysteresis, raw == 65535 ? "true" : "false",
                   dark ? "dark" : "normal") < 0) {
            perror("write JSON output");
            goto cleanup;
        }
        /* 最后一个样本之后不再等待。其他情况等 interval 毫秒再采样。
         * 实际周期包含 I²C 读取和输出时间，所以并非严格每 1000ms 一次。 */
        if ((!count || sample + 1 < count) && sleep_ms(interval)) {
            perror("sample interval");
            goto cleanup;
        }
    }
    status = 0; /* 正常完成，或正常观察到停止标记后离开循环。 */

/* 统一清理出口。goto cleanup 只跳转到这里，不是重新执行整个 main。
 * 这样发生错误时不必在每个分支重复写“恢复模式 + 关闭 fd”。
 * 能否恢复取决于进程仍能执行、I²C 链路正常等条件，断电无法在此处理。 */
cleanup:
    /* 若原模式是 0x00，就恢复待机；原来是 0x01，就恢复为测光。
     * 本程序不把配置写入文件，不烧写系统，也不改变 eMMC 启动分区。
     * 这里只写回传感器的运行时寄存器。 */
    if (mode_saved && write_register(fd, REG_MODE, saved_mode)) {
        perror("restore AP3216C mode");
        status = 1;
    }
    close(fd); /* 释放设备描述符；本进程持有的 flock 锁也随之释放。 */
    return status;
}
