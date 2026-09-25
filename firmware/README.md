# 仿生蝴蝶固件 (Firmware)

| 目录 | 作用 |
|---|---|
| `butterfly_fc/` | **机上飞控**：ICM-42688-P 陀螺仪增稳、扑翼同步滤波、双舵机混控、ESP-NOW 通信、USB 命令行 |
| `ground_station/` | **地面站**（FireBeetle 2 ESP32-E）：**盖世小鸡 G7 Pro** 蓝牙手柄 ⇢ ESP-NOW 桥接；提供手机网页和文本指令接口 |
| `camera_node/` | **可选摄像头节点**（XIAO ESP32S3 Sense）：MJPEG 视频流，画面显示在手机网页里 |
| `tests/` | 主机端单元测试（滤波器、姿态解算、飞行逻辑、通信协议），不需要硬件 |
| `../tools/gyro_fft.py` | 采集陀螺仪数据并画频谱，调滤波器用 |

设计原理见 [`docs/gyro-stabilization-and-noise-reduction.md`](../docs/gyro-stabilization-and-noise-reduction.md)，完整制作流程见 [`docs/build-plan.md`](../docs/build-plan.md)，**接线图和组装步骤见 [`docs/assembly-guide.md`](../docs/assembly-guide.md)**。

---

## 1. 开发环境

1. 安装 **Arduino IDE 2.x**。
2. 打开 **文件 → 首选项 → 附加开发板管理器网址**，填入：
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. 打开 **开发板管理器**，搜索 `esp32`，安装 **esp32 by Espressif Systems 3.x**。
4. 开发板选 **XIAO_ESP32S3**，并确认 **USB CDC On Boot = Enabled**。
5. 飞控 `butterfly_fc` 和摄像头节点 `camera_node` **不需要第三方库**。
6. **地面站 `ground_station`** 使用 Bluepad32 开发板包（它负责连接 G7 Pro 手柄）：
   - 在“附加开发板管理器网址”里**再加一行**：
     `https://raw.githubusercontent.com/ricardoquesada/esp32-arduino-lib-builder/master/bluepad32_files/package_esp32_bluepad32_index.json`
   - 在开发板管理器里安装 **esp32_bluepad32**。
   - 上传地面站程序时，开发板选 **ESP32 + Bluepad32 Arduino → FireBeetle 2 ESP32-E**。
   - ⚠️ 地面站必须用**原版 ESP32**：G7 Pro 的蓝牙模式要求主机支持经典蓝牙，而 ESP32-S3 / C3 只支持 BLE。

> 两个工程各有一份 `protocol.h`，内容**必须完全一致**。CI 会自动检查这一点。

## 2. 接线

**详细的接线图、焊接方法和注意事项见 [`docs/assembly-guide.md`](../docs/assembly-guide.md)。** 这里只列引脚速查表。

![图 0 系统总览](../docs/img/fig0-overview.svg)

### 飞控 XIAO ESP32S3 引脚速查

| 引脚 | 接什么 | 接线图 |
|---|---|---|
| 5V | Mini-360 5.0 V 输出 → SS14 二极管（条纹端朝 XIAO） | 图 2 |
| GND | 公共地 | 图 2 |
| 3V3 | 陀螺仪、气压计、GPS 的 VCC | 图 3 |
| D0 | 电池电压：SYS+ → 200 kΩ → **D0** → 100 kΩ → GND | 图 2 |
| D1 / D2 | 左 / 右舵机信号 | 图 2 |
| D3 | 陀螺仪 CS | 图 3 |
| D4 | 气压计 CSB | 图 3 |
| D5 | GPS RX（可不接） | 图 3 |
| D6 | 充电检测：VBUS → 100 kΩ → **D6** → 200 kΩ → GND | 图 2 |
| D7 | GPS TX | 图 3 |
| D8 / D9 / D10 | SPI：SCK / MISO / MOSI（陀螺仪和气压计共用） | 图 3 |

- 舵机红线接 **SYS+**（开关后的电池电压），所以必须用能耐 **7.4 V** 的高压舵机。
- D6 的分压电阻**一定要焊上**：其中的 200 kΩ 同时充当下拉电阻。否则 D6 悬空，读数会乱跳，可能导致无法解锁。
- 气压计要用一小块**开孔海绵**盖住，否则高度读数会随扑翼节奏乱跳。

### 地面站：FireBeetle 2 ESP32-E + 盖世小鸡 G7 Pro

![图 4 地面站](../docs/img/fig4-ground-station.svg)

1. 1S 锂电池插到 FireBeetle 2 的电池座；用它的 USB-C 口烧录程序，也用它充电。
2. **G7 Pro 配对**：背面中间的模式开关拨到**蓝牙** → 短按 Xbox 键开机 → **长按配对键**，直到指示灯循环闪烁。地面站会自动连接。
3. 解锁、上锁、返航时手柄会振动提示。想换一个手柄时，串口输入 `PAIR`，清除旧的配对记录。
4. 可选语音模块：TX → GPIO16，VCC → 3V3，GND → GND。

| G7 Pro 按键 | 功能 |
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

可以从地面站的 USB 串口或 GPIO16（语音模块）输入，每条指令占一行，不区分大小写：

```
ARM | DISARM | MODE MANUAL|STAB|HOLD|AUTO|RTH | RTH | TAKEOFF | LAND | UP | DOWN | THR 0.6
LEFT 30 | RIGHT 45 | TURN -90 | STICKS | SET rate_p_roll 0.1 | SAVE | CALIB
TEL ON | TEL OFF | STATUS | PAIR
```

- `TAKEOFF`：在 AUTO 模式下，会自动做起飞手势并爬升 1.5 秒，然后定高；在 HOLD 模式下，油门缓慢升到 0.75。
- `UP` / `DOWN`：在 AUTO 模式下把目标高度改变 ±1 m；在其他模式下把油门改变 ±0.1。
- **只要动一下摇杆，控制权立刻交回人手。**
- 手柄上的 B 键随时可以上锁。
- `PAIR`：清除已配对的手柄，然后接受新手柄配对。

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
