# Linux 板卡传感器独立实验

本组代码用于当前通过 ADB 连接的 **100ask I.MX6ULL Pro**，不是 STM32 工程。
先逐项观察输入，再考虑灯、电机、线程、串口和 MQTT 的联动。
每项只有读取循环，暂时不引入多线程，便于从 `main()` 顺着看。

## 在 VS Code 中运行

1. 用 VS Code 打开 `/Users/yang/Documents/ChatGPT/linux` 文件夹。
2. 顶部菜单选择「终端 → 运行任务」。
3. 选择 **传感器：选择一个实验并上板运行**。
4. 下拉选择 `als` 等项目。程序会自动编译、通过 ADB 部署、显示数据。
5. 也可以选择 **传感器：所有采集通道各读5次**，依次检查八个采集项。

默认采集 5 次，自动退出；按键和触摸实验默认观察 5 秒。
查看代码时打开本目录相应的 `.c` 文件，从 `main()` 开始读。
原有光线控制灯的学习代码保留，本组实验不会控制 LED。

## 在 Mac 终端运行

先进入学习工程：

```sh
cd /Users/yang/Documents/ChatGPT/linux
./scripts/sensor-demos.sh list
./scripts/sensor-demos.sh run all
```

单项持续观察，例如光线：

```sh
./scripts/sensor-demos.sh run als --count 0
```

按 Ctrl+C 停止。单项指定次数/间隔：

```sh
./scripts/sensor-demos.sh run accel --count 20 --interval-ms 500
```

如果提示已有程序运行，先到原终端停止它。脚本默认连接 `100ask_IMX6ULL`；
以后更换 ADB 序列号时可设置 `ANDROID_SERIAL`。板端程序位于 `/root/sensor-demos/`。
编译结果位于 `build/sensor-demos/`，源码位于 `apps/sensor-demos/`。

## 实验顺序与现象

| 命令中的名称 | 硬件/通道 | 源码 | 你要做的观察 |
| --- | --- | --- | --- |
| `als` | AP3216C 环境光 | `ap3216c.c` | 用手遮住传感器，再移开，比较 `als_raw` |
| `proximity` | AP3216C 接近 | `ap3216c.c` | 手掌靠近/远离，观察 `proximity_raw`；不能直接当厘米 |
| `ir` | AP3216C 红外强度 | `ap3216c.c` | 比较不同环境/遮挡下的 `ir_raw`；它不解码遥控器按键 |
| `accel` | ICM20608 三轴加速度 | `imu.c` | 轻轻改变板子朝向，观察重力在三轴的分量 |
| `gyro` | ICM20608 三轴角速度 | `imu.c` | 轻轻旋转后放稳，比较旋转时和静止时的值 |
| `imu-temp` | ICM20608 内部温度原始通道 | `imu.c` | 看原始值和驱动参数，当前镜像暂不提供可信的摄氏度换算 |
| `adc` | SoC ADC 0～4 通道 | `adc.c` | 理解原始码与电压换算；未连接输入不能当作某种传感器 |
| `temperature` | SoC 内部温度 | `temperature.c` | 看 CPU 芯片温度，不能当作室温 |
| `keys`（附加） | USER 按键 | `input_events.c` | 按/松 USER 键，观察事件 `type/code/value` |
| `touch`（附加） | 触摸屏 | `input_events.c` | 点击/移动手指，观察触摸事件 |

加速度/陀螺仪实验请托稳板子，避免拉扯供电线、USB 线和显示屏排线。
ADC 先做读取，不要求接线；接入外部电压前应先核对板卡引脚、电压范围和共地。

按键/触摸单独运行，不包括在 `all` 中。这两项 `--count` 表示观察秒数，范围 1～600：

```sh
./scripts/sensor-demos.sh run keys --count 15
./scripts/sensor-demos.sh run touch --count 15
```

普通按键事件 `type=1`，通常 `value=1` 为按下，`0` 为松开，`2` 为自动重复；
触摸事件包含坐标、接触状态等原始事件，没有封装成手势。
读取是非独占的，屏幕上的界面仍会响应触摸。

## 代码与硬件之间的关系

AP3216C 的三种测量在同一颗芯片内：

```text
ap3216c.c → /dev/i2c-0 → I2C 总线（地址 0x1e）→ AP3216C
```

程序自己读写寄存器：保存旧模式 → 开启连续测量 → 等待 → 读低/高字节 → 输出 → 恢复旧模式。
同一个 I2C 设备使用协作文件锁，避免与之前的光线采集器同时操作。
红外/接近数据遇到无效标志时输出 `null` 和 `valid=false`。
`object_flag` 是芯片阈值判断标志，仍需结合有效性和实际标定理解。
ALS/IR/PS 都是原始计数，当前没有标定成 lux 或距离。
采样周期受芯片自身转换时间限制，调小软件间隔不代表获得新数据。

ICM20608 的总线读写已经由内核驱动完成：

```text
imu.c → /sys/bus/iio/devices/... → 内核 IIO 驱动 → SPI → ICM20608
```

程序按设备名称查找 IIO 路径，不依赖固定的 `iio:device1` 编号。
加速度 `raw × scale` 的单位是 m/s²；角速度为 rad/s，不是角度。
各轴按顺序读取，适合观察，不保证同时采样；静止也可能有偏置、噪声。
它还不是姿态解算或稳定控制程序。

ADC 同样通过 IIO 读取，`raw × scale` 输出 mV。
CPU 温度通过 thermal 子系统读取，毫摄氏度除以 1000 得到 °C。
`common.h` 负责参数校验、等待、信号退出以及通用 sysfs 读取。
这些输入将来都可以封装成统一数据，再交给线程、串口或 MQTT 模块。

## 当前覆盖范围和待接硬件

实板确认的板载传感芯片是 **AP3216C 与 ICM20608**，上表包含它们暴露的测量通道，
并补充了 ADC、CPU 温度、按键和触摸输入。八个采集名称不代表八颗物理传感器。

系统还加载了 `/dev/dht11`、`/dev/ds18b20`、`/dev/adxl345` 和 `/dev/irda` 等扩展驱动。
**节点存在不能证明模块已经接好**。本组没有猜测这些自定义驱动的数据格式，
也没有把它们加入 `all`。确认实物、接线与驱动接口后，再补扩展 demo。
`/dev/video0`、WM8960 音频属于摄像/音频实验，需确认摄像头、麦克风和连接情况后单独安排。

旧系统给 IMU 温度的 `scale=0.002941`、`offset=12421`，存在旧驱动单位/芯片系数问题的疑点。
这里保留原始信息用于后续排查，不把它显示为已校准温度。
可参考 Linux 官方历史修复说明：
[inv_mpu6050 温度单位修复](https://kernel.googlesource.com/pub/scm/linux/kernel/git/stable/stable-queue/+/master/releases/5.4.4/iio-imu-inv_mpu6050-fix-temperature-reporting-using-bad-unit.patch)。

## 上板验证记录（2026-11-11）

设备：ADB `100ask_IMX6ULL`；Linux `4.9.88`。
使用 ARMv7 hard-float 静态链接，开启 `-Wall -Wextra -Werror`。

- 光线、接近、红外均读到有效样本；例如 ALS=148，PS=139，IR=7（现场数值会变化）。
- 加速度/角速度读到三轴值及 scale 换算值；尚未做六面校准和精度验证。
- IMU 温度原始数据、ADC 0～4 和 CPU 温度均能读取；CPU 当时约 60°C。
- 按键与触摸输入节点均可打开并按时退出；实际按键/触摸响应需你操作确认。
- 非法参数返回 2；同时启动第二个 AP3216C 采集器返回 1，避免相互覆盖模式。
- 正常退出、SIGTERM、Mac 终端 Ctrl+C 后检查模式寄存器：均从采集时 `0x03` 恢复到原来的 `0x00`，无残留采集进程。
- 未连接或未确认的扩展传感器不记为通过。

这轮通过的是读取链路，不代表已验证量程、精度、动态响应或每个物理刺激。
本轮完整采集日志保存在 `build/sensor-demos/validation.log`（生成目录，不加入 Git）。
