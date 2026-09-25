# 仿生蝴蝶固件 (Firmware)

| 目录 | 作用 |
|---|---|
| `butterfly_fc/` | **机上飞控**：ICM-42688-P 陀螺仪增稳、扑翼同步滤波、双舵机混控、ESP-NOW 通信、USB 命令行 |
| `ground_station/` | **遥控器 / 地面站**：摇杆 + 开关，另外提供文本指令接口（语音 / AI / 电脑都通过它控制） |
| `tests/` | 主机端单元测试（滤波器、姿态解算、飞行逻辑、通信协议），不需要硬件 |
| `../tools/gyro_fft.py` | 采集陀螺仪数据并画频谱，调滤波器用 |

设计原理见 [`docs/gyro-stabilization-and-noise-reduction.md`](../docs/gyro-stabilization-and-noise-reduction.md)，完整制作流程见 [`docs/build-plan.md`](../docs/build-plan.md)。

---

## 1. 开发环境

1. 安装 **Arduino IDE 2.x**。
2. 打开 **文件 → 首选项 → 附加开发板管理器网址**，填入：
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. 打开 **开发板管理器**，搜索 `esp32`，安装 **esp32 by Espressif Systems 3.x**。
4. 开发板选 **XIAO_ESP32S3**，并确认 **USB CDC On Boot = Enabled**。
5. 分别打开 `butterfly_fc/butterfly_fc.ino` 和 `ground_station/ground_station.ino` 编译上传。**不需要安装任何第三方库。**

> 两个工程各有一份 `protocol.h`，内容**必须完全一致**。CI 会自动检查这一点。

## 2. 接线

### 机上（XIAO ESP32S3）

```
                ┌──────────── XIAO ESP32S3 ────────────┐
 电池分压 ─────► D0 (GPIO1)                  5V ◄──┤◄── SS14 ◄── 降压模块 5.0V ◄── 2S+
 左舵机信号 ◄─── D1 (GPIO2)                  GND ─── 公共地
 右舵机信号 ◄─── D2 (GPIO3)                  3V3 ──► IMU VCC (+10µF +100nF)
 IMU CS    ◄─── D3 (GPIO4)                  D10 ──► IMU SDI / MOSI
                                             D9  ◄── IMU SDO / MISO
                                             D8  ──► IMU SCLK
                └──────────────────────────────────────┘

 2S+ ──┬──► 舵机 L 红线、舵机 R 红线（高压舵机直接接电池）
       └──► 470µF 低 ESR 电容（尽量贴近舵机插头，另一端接 GND）
 2S+ ── 200kΩ ──┬── 100kΩ ── GND        （分压比 3.0，对应参数 vbat_ratio）
                └── D0
```

- XIAO 的 5V 引脚可以作为电源输入，但**必须串一个二极管**：阳极接电源，阴极接 5V 引脚。这样插着 USB 调试时不会倒灌。
- IMU 用 **SPI** 接法。模块上的丝印可能写成 `SCL/SCLK`、`SDA/SDI`、`SAO/SDO`、`CS`，CS 必须接上。
- 如果用的是 6 V 舵机（非高压），舵机要改由 **6 V BEC** 供电，不能直接接 2S 电池。

### 遥控器（XIAO ESP32S3）

| 引脚 | 接什么 |
|---|---|
| D0 | 油门电位器中间脚（两端分别接 3V3 和 GND） |
| D1 / D2 | 右摇杆 X（横滚）/ Y（俯仰） |
| D3 | 左摇杆 X（偏航） |
| D4 | ARM 开关 → GND（闭合 = 解锁） |
| D5 | MODE 开关 → GND（闭合 = STABILIZE） |
| D7 | 语音模块 TX（可选，115200） |

上电时**两个摇杆必须回中**：程序会在开机时记录摇杆中位。

## 3. 飞控 USB 命令行

打开串口监视器：115200，行尾选 **换行 (Newline)**。

| 命令 | 作用 |
|---|---|
| `help` / `status` / `imu` | 帮助 / 总体状态 / 打印一行陀螺仪数据 |
| `calib` | 陀螺仪零偏校准（需要未解锁、保持静止） |
| `list`，`get <名>`，`set <名> <值>`，`save`，`defaults` | 查看和修改参数；修改后要 `save` 才会写入 flash |
| `servo <L度> <R度>` / `servo off` | 舵机测试（仅限未解锁时） |
| `bench <油门0..1> [模式0/1/2]` | 台架扑翼：不用遥控器也能解锁。120 秒后自动关闭 |
| `bench stick <roll> <pitch> <yaw>` / `bench off` | 台架模式下模拟打杆 / 退出台架模式 |
| `log att` / `log fft` / `log raw` / `log off` | 输出 CSV 日志：姿态 50 Hz / 滤波前后陀螺 200 Hz / 原始陀螺 1 kHz |

## 4. 遥控器文本指令（语音 / AI 接口）

可以从 USB 串口或 D7（Serial1）输入，每条指令占一行，不区分大小写：

```
ARM | DISARM | MODE MANUAL|STAB|HOLD | TAKEOFF | LAND | UP | DOWN | THR 0.6
LEFT 30 | RIGHT 45 | TURN -90 | STICKS | SET rate_p_roll 0.1 | SAVE | CALIB
TEL ON | TEL OFF | STATUS
```

- `TAKEOFF` 会自动切到 HEADING_HOLD 模式，油门缓慢升到 0.75。
- **只要动一下摇杆，控制权立刻交回人手。**
- 实体 ARM 开关是总开关：开关没打开时，任何文本指令都不能让蝴蝶扑翼。

## 5. 关键参数

| 参数 | 默认值 | 说明 |
|---|---|---|
| `f_min` / `f_max` | 2.5 / 5.0 Hz | 油门从最低到最高时的扑翼频率 |
| `amp_min` / `amp_max` | 20 / 40° | 半行程幅值（从中位到最高点的角度） |
| `center` | 10° | 扑动中心，同时也是滑翔时翅膀的上反角 |
| `squareness` | 0.3 | 波形：0 为正弦波，越接近 1 越接近方波，出力越大 |
| `mix_roll_dir` / `mix_pitch_dir` / `mix_yaw_dir` | +1 | 打杆方向和实际飞行方向相反时，改成 −1 |
| `servo_dir_l` / `servo_dir_r` | +1 / −1 | 舵机镜像安装时用来修正方向 |
| `trim_l` / `trim_r` | 0 | 舵机中位微调；左右相反设置，可以修正飞行时的持续偏航 |
| `imu_yaw_quarter` / `imu_flip` | 0 / 0 | IMU 安装方向（旋转 n×90°，是否倒装） |
| `rate_p_*` / `rate_i_*` / `rate_d_*` | 0.08 / 0.05 / 0 | 速率环 PID。单位：°（翅膀偏置）每 °/s |
| `ang_p_roll` / `ang_p_pitch` | 3.0 | 角度环 P。单位：(°/s) 每 ° |
| `notch_on` / `stroke_avg_on` | 1 / 1 | 扑翼同步陷波 / 整周期平均的开关 |

## 6. 单元测试

```bash
cd firmware/tests
g++ -std=c++17 -O1 -Wall -Wextra -I../butterfly_fc test_core.cpp -o test_core && ./test_core
```

测试覆盖以下内容：

- 滤波器：PT1 截止频率、陷波深度、整周期平均能清除所有谐波。
- 姿态解算：Mahony 各轴符号、从加速度收敛、陀螺零偏估计。
- **扑翼干扰下的姿态仿真**：机体摆动 ±10°、加速度计受 ±1.5 g 干扰时，平均姿态误差小于 1.5°。
- 解锁逻辑：开机时开关已开不会解锁、油门高时拒绝解锁、失控保护、台架模式。
- 混控：频率和幅值映射、横滚 / 偏航差动、限幅。
- 增稳方向正确。
- 通信协议：CRC 校验、网络 ID 过滤。
