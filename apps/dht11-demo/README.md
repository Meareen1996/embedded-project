# DHT11：接线与第一个 Linux 应用

适用硬件：100ask I.MX6ULL Pro + 百问网四针 DHT11 模块 + 配套扩展板。
如果实物是其它三针模块或只有裸传感器，请按实物标识重新核对，不能套用四针排针顺序。

## 1. 断电接线

配套手册 PDF 第22页（印刷页16），2.4.1 节要求：
将 DHT11 插在扩展板 **GPIO0** 接口，模块和接口的长白线在同一侧。
这个接口是扩展板 J1 的前四针，不是 SPI/I2C 插座，也不是 STM32 板上的接口。

| 配套模块 J1 插座引脚 | 模块功能 | GPIO0 扩展接口对应信号 |
| --- | --- | --- |
| 1 | 未连接 NC | 对应接口的5V位置，模块内部未连接 |
| 2 | 3V3 | 3.3V |
| 3 | GND | GND |
| 4 | GPIO / DATA | GPIO4_IO19 |

模块原理图中 DATA 已通过 5.1kΩ 电阻上拉到3.3V，不需要再加上拉电阻。
这是模块插座编号，**不是蓝色 DHT11 元件自身的引脚编号**。
蓝色元件的4个脚为 VDD、DATA、NC、GND，排列含义与模块插座不同。

接线顺序：

1. 板子关机并断开供电，确认没有其它线继续给板子供电。
2. 配套扩展板装在底板的扩展/摄像头排针处，核对方向及1脚标记，不错位。
3. DHT11 插 GPIO0，四针完整对齐、两边长白线同侧；其余模块先不接。
4. 检查后再上电，等待系统启动。

有配套板时优先直接插接。如果没有扩展板、丝印不同或不能确认方向，
先拍全板接口和模块正反面，核对后再接，不凭照片中的左右方向数针。
使用杜邦线时只需3V3、GND和DATA三根有效线，仍需核对实物插座。

## 2. 理解三个“编号”

```text
扩展板的接口名称 GPIO0
          ↓
芯片信号 GPIO4_IO19
          ↓
当前系统的 Linux GPIO编号 (4−1)×32+19 = 115
```

程序传给驱动的是115，不是0。
对应底板复用引脚名为 CSI_VSYNC；当前镜像驱动和配套库使用这一路。

## 3. 先看 main.c 的检查部分

打开 `main.c`，从 `main()` 中的“第一步”开始。

```c
struct stat info;
stat("/dev/dht11", &info);
S_ISCHR(info.st_mode);
```

- `/dev/dht11` 是驱动在 Linux 中提供的入口。
- `stat()` 查询设备信息；`S_ISCHR()` 判断它是否为字符设备。
- 这一步不打开驱动、不初始化GPIO、不读取传感器。
- 检查通过只能证明节点存在，不能证明接线正确。

VS Code 打开本工程目录和 `apps/dht11-demo/main.c`，
执行「终端 → 运行任务 → IMX6ULL：编译并在板子运行当前 C 文件」。
默认只做检查，预期看到：

```text
Device node OK: /dev/dht11 (character device).
Check only: no GPIO initialization or sensor read performed.
```

## 4. 接好以后，再逐段学习读取

文件里已经放好带注释的后续参考实现，可按顺序阅读：

1. `open()`：打开驱动入口。
2. `ioctl(fd, DHT11_IOCINIT, &pin)`：交给驱动GPIO115，启动定时采样。
3. 等待驱动首次采样后，`ioctl(fd, DHT11_IOCGVALUE, &value)`：获取一对温湿度。
4. 打印5次、间隔2秒，然后 `close()`：停止采样并释放资源。

应用不直接生成微秒级电平，单线通信时序由内核驱动处理。
驱动返回2字节，顺序是湿度整数、温度整数；不是芯片线上传输的5字节原始帧。
命令号保留配套库的定义，使用ARM32交叉编译，不能用Mac的64位命令号替代。

先确认接线，再在 Mac 终端运行：

```sh
cd /Users/yang/Documents/ChatGPT/linux
./scripts/vscode-imx6ull.sh run /Users/yang/Documents/ChatGPT/linux/apps/dht11-demo/main.c
adb -s 100ask_IMX6ULL shell -t '/root/embedded-project/current-program --read'
```

第二行先编译、部署并检查；第三行才真正初始化和采集。
程序默认只检查，所以不会因为误点运行任务就启动硬件采样。
同一时间仅运行一个DHT11采集程序，包括原厂的readDHT11程序也应停止。

## 5. 怎样确认读到的确实是传感器数据

当前板载 `100ask_dht11` v2.0 驱动返回的是温湿度缓存，
没有给应用返回“采样成功时间”或“本次校验失败”的状态。
因此 `ioctl` 成功、出现0、或者数值不变，都不能单独证明测量成功。
程序会明确标注 `driver cache`，不把它描述为每次实时成功。

接线完成后一起检查：输出是否合理、环境变化后数值是否变化、内核是否报错。
传感器不应喷水或结露；先在室内静置观察，避免CPU散热和手指直接加热影响判断。
若输出全0或一直异常，可以查看：

```sh
adb -s 100ask_IMX6ULL shell 'dmesg | tail -40'
```

重点找 `DHT11 response error`、`DHT11 timeout error`。
如果是驱动缓存/错误反馈问题，下一阶段再针对驱动改善；不会把旧值当成成功样本。

## 资料与验证状态

- 下载资料中的模块测试手册：2.4、附录一、附录二。
- `dht11.zip` 内的 `irda_dht11_ds18b20原理图.pdf`：确认插座与上拉。
- [配套GPIO编号](https://github.com/100askTeam/arduino-imx6ull/blob/master/libraries/LED/src/gpio.h)。
- [配套DHT11驱动接口定义](https://github.com/100askTeam/arduino-imx6ull/blob/master/libraries/DHT11/src/dht11.h)。
- [配套DHT11调用方式](https://github.com/100askTeam/arduino-imx6ull/blob/master/libraries/DHT11/src/dht11.cpp)。
- 当前实板模块信息为v2.0；已比对板端模块中的命令号、2字节返回顺序和缓存行为。
- 已交叉编译并上板通过默认检查。
- 2026-11-11 用户接线后实测：初始化成功，首次5次读取均为0/0，内核报告
  `DHT11 response error`，尚未确认有效温湿度；引脚复用寄存器为ALT5（GPIO），
  GPIO4_IO19空闲输入为高电平。下一步核对实物供电、接口和方向。
- 示例已将0/0缓存改为“未确认可用测量”提示，整轮均为0/0时返回失败，避免误认成真实读数。
- 用户补充照片后，模块插在GPIO0对应四针的位置、方向看起来符合资料；照片不能验证电气接触和被遮挡的主板侧连接。
  临时切到performance（792MHz）复测5次，仍为0/0，随后恢复ondemand；未留下采集进程。
- 后续实测：模块供电和DATA空闲电压均约3.3V，模块GPIO与扩展板GPIO0导通。
  主板将GPIO115拉低时，模块端测得0.011V，证实主板到模块的数据控制通路。
  释放测试GPIO后重新采集，连续5次读取到湿度36%RH、温度25°C，应用正常退出。
  此前无响应的具体原因尚未确定，不能据此归因于CPU频率或认定传感器损坏。
- 再启动一轮5次读取时，仍显示36%RH、25°C，但内核DHT11错误计数从6增至10。
  因此目前只确认收到过合理的传感器数据，不能把每次缓存读取视为新采样成功；
  通信存在间歇失败，后续应进一步检查驱动响应等待时序与错误反馈。
