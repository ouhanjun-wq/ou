# 仿生蝴蝶固件 (Firmware)

| 目录 | 作用 |
|---|---|
| `butterfly_fc/` | **机上飞控**：ICM-42688-P 陀螺仪增稳、扑翼同步滤波、双舵机混控、ESP-NOW 通信、USB 命令行 |
| `ground_station/` | **地面站**：蓝牙手柄（**盖世小鸡 G7 Pro** 等，Bluepad32）⇢ ESP-NOW 桥接；也支持 Xbox BLE 和自制摇杆；提供手机网页和文本指令接口 |
| `camera_node/` | **可选摄像头节点**（XIAO ESP32S3 Sense）：MJPEG 视频流，画面显示在手机网页里 |
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
5. 飞控 `butterfly_fc` **不需要第三方库**。
6. 地面站 `ground_station` 有三种手柄后端，在文件顶部用 `PAD_BACKEND` 选择：

| `PAD_BACKEND` | 手柄 | 地面站开发板 | 开发板包 / 库 |
|---|---|---|---|
| **`PAD_BP32`（默认）** | **盖世小鸡 G7 Pro**（蓝牙模式）、Xbox、PS4/PS5、Switch Pro、8BitDo 等 | **原版 ESP32**，推荐 **FireBeetle 2 ESP32-E** | 开发板包 **esp32_bluepad32**，地址见下方 |
| `PAD_XBOX` | Xbox Series X\|S | XIAO ESP32S3 | 标准 esp32 包 + 库 `XboxSeriesXControllerESP32_asukiaaa` |
| `PAD_DIY` | 自制摇杆和开关 | XIAO ESP32S3 | 标准 esp32 包 |

Bluepad32 开发板包的地址（加到“附加开发板管理器网址”里）：
`https://raw.githubusercontent.com/ricardoquesada/esp32-arduino-lib-builder/master/bluepad32_files/package_esp32_bluepad32_index.json`
安装 **esp32_bluepad32** 后，开发板选 **FireBeetle 2 ESP32-E**。其他原版 ESP32 板子（ESP32 Dev Module 等）也可以。

> 两个工程各有一份 `protocol.h`，内容**必须完全一致**。CI 会自动检查这一点。

## 2. 接线

### 机上电源：内置电池 + Type-C 充电

电池**内置在机身里，不用拆**。插上 Type-C 就能充电；充电期间飞控**自动锁定、不会解锁**。

```
 Type-C 充电口
      │ 5 V
 ┌────┴──────────────────────┐   VBUS ── 100kΩ ──┬── D6（充电检测）
 │ 2S 升压充电模块           │                   └── 200kΩ ── GND
 │ 5 V → 8.4 V 恒流/恒压     │
 └────┬──────────────────────┘
      │ BAT+ / BAT−
 ┌────┴──────────────────────┐
 │ 2S 保护板（带均衡，过流 ≥ 5 A）│── B+ / BM / B− ── 内置 2S LiPo 150–200 mAh
 └────┬──────────────────────┘
      │ P+ / P−
 电源开关（P-MOS AO3401 + 小拨动开关）   ← 关机时仍然可以充电
      │ SYS+（7.4 V）
      ├──► 舵机 L / R 红线（高压舵机）＋ 470 µF 低 ESR 电容（贴近舵机）
      ├──► 降压模块 5.0 V ──► SS14 ──► XIAO 5V 引脚
      └──► 200kΩ ──┬── 100kΩ ── GND   （电池电压检测，分压比 3.0，对应参数 vbat_ratio）
                   └── D0
```

P-MOS 电源开关的接法（开关本身只走微小电流，所以小拨动开关就够用）：

```
 P+ ──┬────────── AO3401 S           AO3401 D ──► SYS+
      └─ 100kΩ ── AO3401 G ── 拨动开关 ── GND    （开关闭合 = 开机）
```

### 机上信号（XIAO ESP32S3）

```
                ┌──────────── XIAO ESP32S3 ────────────┐
 电池分压 ─────► D0 (GPIO1)                  5V ◄──┤◄── SS14 ◄── 降压模块 5.0 V
 左舵机信号 ◄─── D1 (GPIO2)                  GND ─── 公共地
 右舵机信号 ◄─── D2 (GPIO3)                  3V3 ──► IMU VCC (+10µF +100nF)
 IMU CS    ◄─── D3 (GPIO4)                  D10 ──► IMU SDI / 气压计 SDA  (MOSI)
 气压计 CSB ◄─── D4 (GPIO5)                  D9  ◄── IMU SDO / 气压计 SDO  (MISO)
 充电检测  ────► D6 (GPIO43)                 D8  ──► IMU SCLK / 气压计 SCL (SCK)
 GPS TX    ────► D7 (GPIO44)   （可选）      D5  ──► GPS RX（可选，只在配置模块时用）
                └──────────────────────────────────────┘
```

- XIAO 自己的 USB-C 口**只用来烧录和调试**。给电池充电走的是充电模块的 Type-C 口。
- XIAO 的 5V 引脚可以作为电源输入，但**必须串一个二极管**：阳极接电源，阴极接 5V 引脚。这样插着 USB 调试时不会倒灌。
- D6 的分压电阻**一定要焊上**：其中的 200 kΩ 同时充当下拉电阻。如果暂时不做充电检测，也要用一个 100 kΩ 电阻把 D6 接到 GND。否则 D6 悬空，读数会乱跳，可能导致无法解锁。
- IMU 和气压计**共用一组 SPI 线**，靠各自的 CS 区分。模块上的丝印可能写成 `SCL/SCLK`、`SDA/SDI`、`SAO/SDO`、`CS/CSB`，CS 必须接上。
- 气压计要用一小块**开孔海绵**盖住，挡住扑翼气流，否则高度读数会随扑翼节奏乱跳。
- 如果用的是 6 V 舵机（非高压），舵机要改由 **6 V BEC** 供电，不能直接接 2S 电池。

### 地面站：盖世小鸡 G7 Pro（默认，Bluepad32）

地面站是一块 FireBeetle 2 ESP32-E，插上 1S 锂电池就行，不需要接其他线（可选：语音模块 TX 接 GPIO16）。

1. G7 Pro 背面中间的模式开关拨到**蓝牙**，短按 Xbox 键开机，长按底部配对键，直到指示灯循环闪烁。
2. 地面站会自动连接第一个找到的手柄；解锁、上锁和返航时，手柄会振动提示。
3. 想换手柄时，串口输入 `PAIR`，清除旧的配对记录。
4. 按键功能见 [`docs/build-plan.md`](../docs/build-plan.md) 的“飞行能力与手柄操控”。

> 使用 Xbox Series 手柄 + XIAO ESP32S3 的旧方案时，设置 `PAD_BACKEND PAD_XBOX`；手柄固件需要升级到支持 BLE 的版本。

| 手柄 | 功能 |
|---|---|
| 左摇杆 ↑↓ | AUTO：爬升 / 下降（松手 = 定高）|
| 左摇杆 ←→ | 转航向 |
| 右摇杆 ↑↓ | 前推低头加速 / 后拉抬头减速 |
| 右摇杆 ←→ | 压坡度转弯 |
| RT | 油门（MANUAL / STAB / HOLD）|
| A 长按 1 秒 / B | 解锁 / 上锁 |
| 十字键 ↑ → ↓ ← | AUTO / HOLD / STAB / MANUAL |
| Y / LB / RB | 掉头 180° / 左转 45° / 右转 45° |
| X | 自动返航（需要 GPS），动一下摇杆取消 |
| View | 陀螺仪校准（上锁时）|

### 地面站：自制摇杆（`PAD_BACKEND PAD_DIY` 时，XIAO ESP32S3）

| 引脚 | 接什么 |
|---|---|
| D0 | 油门电位器中间脚（两端分别接 3V3 和 GND） |
| D1 / D2 | 右摇杆 X（横滚）/ Y（俯仰） |
| D3 | 左摇杆 X（偏航） |
| D4 | ARM 开关 → GND（闭合 = 解锁） |
| D5 | MODE 开关 → GND（闭合 = STABILIZE） |
| D7 | 语音模块 TX（可选，115200） |

上电时**两个摇杆必须回中**：程序会在开机时记录摇杆中位。

**遥控器供电**：把一块 1S LiPo（300–500 mAh）焊到 XIAO 背面的 **BAT+ / BAT−** 焊盘上。XIAO ESP32S3 板上自带锂电池充电电路，所以插上它的 USB-C 就能直接充电，不需要额外的充电模块。

### 手机网页（GPS 位置 / 遥测 / 摄像头）

地面站默认开启 Wi-Fi 热点（`ENABLE_WEB 1`）。手机连接 **`Butterfly-GS`**（密码 `butterfly123`），用浏览器打开 **`http://192.168.4.1`**，就能看到实时遥测、以“家”为中心的 GPS 轨迹和地图跳转链接。如果装了摄像头节点，页面上还会显示画面。热点和 ESP-NOW 共用信道 1，不会影响控制链路。详见 [`docs/gps-and-camera.md`](../docs/gps-and-camera.md)。

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
| `gps` | GPS 状态：波特率、卫星数、是否定位、坐标、家的距离、北向对准、返航状态 |
| `log att` / `log fft` / `log raw` / `log off` | 输出 CSV 日志：姿态 50 Hz / 滤波前后陀螺 200 Hz / 原始陀螺 1 kHz |

## 4. 遥控器文本指令（语音 / AI 接口）

可以从 USB 串口或 D7（Serial1）输入，每条指令占一行，不区分大小写：

```
ARM | DISARM | MODE MANUAL|STAB|HOLD|AUTO|RTH | RTH | TAKEOFF | LAND | UP | DOWN | THR 0.6
LEFT 30 | RIGHT 45 | TURN -90 | STICKS | SET rate_p_roll 0.1 | SAVE | CALIB
TEL ON | TEL OFF | STATUS
```

- `TAKEOFF`：在 AUTO 模式下，会自动做起飞手势并爬升 1.5 秒，然后定高；在 HOLD 模式下，油门缓慢升到 0.75。
- `UP` / `DOWN`：在 AUTO 模式下把目标高度改变 ±1 m；在其他模式下把油门改变 ±0.1。
- **只要动一下摇杆，控制权立刻交回人手。**
- 使用手柄时，按 B 随时可以上锁；使用自制摇杆时，实体 ARM 开关是总开关。
- `PAIR`（仅限 Bluepad32 后端）：清除已配对的手柄，然后接受新手柄配对。

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
| `thr_hover` | 0.65 | AUTO 定高的基准油门。设成手动平飞时的油门值 |
| `max_climb` / `max_descent` | 1.0 / 0.7 m/s | AUTO 下摇杆推满时的最大爬升 / 下降速度 |
| `alt_p` / `vz_p` / `vz_i` | 1.0 / 0.15 / 0.1 | 高度环 P / 爬升率环 PI |
| `baro_lpf_hz` | 2.0 | 气压高度的低通截止频率 |
| `rth_enable` / `rth_radius` / `rth_max_s` | 1 / 15 m / 60 s | 失控自动返航开关 / 到家半径 / 最长返航时间 |
| `rth_thr` / `rth_loiter` | 0.7 / 0.25 | 没有气压计时的返航油门 / 在家上空绕圈的转弯强度 |

## 6. 单元测试

```bash
cd firmware/tests
g++ -std=c++17 -O1 -Wall -Wextra -I../butterfly_fc test_core.cpp -o test_core && ./test_core
```

测试覆盖以下内容：

- 滤波器：PT1 截止频率、陷波深度、整周期平均能清除所有谐波。
- 姿态解算：Mahony 各轴符号、从加速度收敛、陀螺零偏估计。
- **扑翼干扰下的姿态仿真**：机体摆动 ±10°、加速度计受 ±1.5 g 干扰时，平均姿态误差小于 1.5°。
- 解锁逻辑：开机时开关已开不会解锁、油门高时拒绝解锁、失控保护、台架模式、充电锁。
- AUTO 定高：回中才能解锁、起飞手势、松杆定高、`CMD_ALT` 调整目标高度、失控保护恢复后接着定高、没有气压计时降级为 HOLD。
- 混控：频率和幅值映射、横滚 / 偏航差动、限幅。
- 增稳方向正确。
- 通信协议：CRC 校验、网络 ID 过滤。
- 自动返航：家的方位计算、北向对准学习、失控返航方向正确、到家后滑翔、超时放弃、没有 GPS 或电量低时不返航、X 键返航后在家上空绕圈、没有对准时不返航。
- GPS：NMEA 标准例句解析、南纬 / 西经、RMC 地速和航向、校验失败拒收、无定位、乱码输入。
