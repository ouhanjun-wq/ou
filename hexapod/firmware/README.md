# 六足机器人 + 体感手套：固件

| 目录 | 板子 | 作用 |
|---|---|---|
| `hexapod/` | 套件的 **ESP32 DevKit**（开发板选 **ESP32 Dev Module**） | 收手套指令、步态、逆运动学、18 路舵机、电量保护、串口命令 |
| `glove/` | **Seeed XIAO ESP32S3**（开发板选 **XIAO_ESP32S3**） | 读 5 根弯曲传感器 + MPU6050、认手势、发 ESP-NOW、振动反馈 |
| `tests/` | 电脑 | 单元测试（运动学、步态、手势、协议、串口命令），不需要硬件 |

原理见 [`docs/gait-and-gestures.md`](../docs/gait-and-gestures.md)，接线和标定见 [`docs/assembly-guide.md`](../docs/assembly-guide.md)，完整流程见 [`docs/build-plan.md`](../docs/build-plan.md)。

## 1. 开发环境

1. **Arduino IDE 2.x**。附加开发板管理器网址：`https://espressif.github.io/arduino-esp32/package_esp32_index.json`，开发板管理器安装 **esp32 by Espressif Systems**（2.0.x 和 3.x 都能编译；CI 用最新版）。
2. **不需要装任何库**：PCA9685、MPU6050 的代码都直接写在程序里。
3. 上传前：套件 ESP32 选 **ESP32 Dev Module**；XIAO 选 **XIAO_ESP32S3**。串口监视器 **115200**，行尾“换行”。

> 💡 CI（`.github/workflows/hexapod.yml`）每次提交都会编译两个程序、运行单元测试，并检查两边的 `protocol.h` 完全一样。
>
> 电脑上跑单元测试：
> ```
> g++ -std=c++17 -O1 -Wall -Wextra -Werror -I hexapod/firmware/hexapod -I hexapod/firmware/glove \
>     hexapod/firmware/tests/test_core.cpp -o test_core && ./test_core
> ```

## 2. 文件

| 文件 | 内容 | 能单元测试 |
|---|---|---|
| `hexapod/protocol.h` = `glove/protocol.h` | 无线包格式、CRC-8、链路号（**两份必须一样**） | ✅ |
| `hexapod/leg_ik.h` | 一条腿的正 / 逆运动学、机身 ↔ 腿坐标 | ✅ |
| `hexapod/gait.h` | 三角 / 涟漪 / 波浪步态、落脚点、停步 | ✅ |
| `hexapod/robot.h` | 状态机（趴下 / 起立 / 站立 / 趴下中）、手套超时、加速度限制、机身姿态、低电量、避障 | ✅ |
| `hexapod/commands.h` | 串口命令 | ✅ |
| `hexapod/params.h` | 所有参数和默认值（存 ESP32 flash） | ✅ |
| `hexapod/servo_out.h` | 舵机输出：PCA9685（I2C）或直连 GPIO（硬件定时器） | — |
| `hexapod/config.h` | 引脚 | — |
| `hexapod/hexapod.ino` | 主循环 50 Hz、ESP-NOW、存储、传感器 | — |
| `glove/gesture.h` | 弯曲度、滞回、手势、防抖、互补滤波、倾角 → 指令 | ✅ |
| `glove/config.h` | 手套引脚 | — |
| `glove/glove.ino` | 100 Hz 采样、50 Hz 发送、按键、标定、振动、串口 | — |

## 3. 引脚

### 机器人（ESP32 DevKit）

| 引脚 | 接什么 |
|---|---|
| GPIO21 / GPIO22 | I2C SDA / SCL → 扩展板上的 PCA9685（A 型板） |
| GPIO34 | 电压检测模块 `S`（5 : 1 分压；只能输入的引脚，不会和舵机冲突） |
| GPIO2 | 板载蓝灯：灭 = 趴着断电，亮 = 站着，快闪 = 收不到手套，慢闪 = 电池没电 |
| `PIN_TRIG` / `PIN_ECHO` | HC-SR04（可选，默认 −1 = 没装）。ECHO 要经 1 kΩ / 2 kΩ 分压 |
| B 型板 | 18 个舵机的 GPIO 用 `CHMAP` 设置 |

### 手套（XIAO ESP32S3）

| 引脚 | 接什么 |
|---|---|
| D0 / D1 / D2 / D3 / D8 | 弯曲传感器：拇指 / 食指 / 中指 / 无名指 / 小指（都是 ADC1） |
| D4 / D5 | MPU6050 SDA / SCL |
| D9 | 按键（另一脚接 GND） |
| D10 | 振动马达模块 IN |
| 背面 BAT+ / BAT− | 3.7 V 锂电池（经拨动开关） |

## 4. 机器人串口命令

单位：mm、度、微秒。腿 0–5 = 左前 LF、左中 LM、左后 LR、右前 RF、右中 RM、右后 RR；关节 0–2 = 髋、大腿、小腿。大小写都行。

### 使用

| 命令 | 作用 |
|---|---|
| `STAND` | 起立（电池过低时拒绝） |
| `SIT` | 停下、趴下、舵机断电 |
| `OFF` | 立刻断舵机电（机器人会软下来） |
| `WALK x y turn [ms]` | 走：前进 / 左移 / 左转的百分比（−100–100），默认持续 2000 ms。例：`WALK 50 0 0`、`WALK 0 -60 0`、`WALK 40 0 30 5000` |
| `STOP` | 停（手套没开时用） |
| `GAIT TRIPOD` / `RIPPLE` / `WAVE` | 换步态（停下来以后生效） |
| `STATUS` | 状态、指令、高度、电压、每条腿的角度和脉宽 |

### 标定

| 命令 | 作用 |
|---|---|
| `CAL` | 所有关节到 0°（标定姿态），舵机上电。**装舵盘之前必须先 `CAL`** |
| `JOINT 腿 关节 角度` | `CAL` 状态下单独转一个关节，看方向 |
| `TRIM 腿 关节 微秒` | 0° 的脉宽偏移（绝对值，±400） |
| `DIR 腿 关节 1` / `-1` | 舵机方向 |
| `CH 腿 关节 通道` | 改一个舵机的通道 |
| `CHMAP c0 … c17` | 一次写 18 个通道（顺序：LF 髋 大腿 小腿，LM …，… RR 小腿）；`SAVE` 后重启生效 |
| `BACKEND AUTO` / `PCA9685` / `GPIO` | 舵机输出方式（默认 AUTO：有 0x40 就用 PCA9685） |
| `SCAN` | 扫描 I2C，看扩展板是 A 型还是 B 型 |

### 尺寸和手感

| 命令 | 默认 | 说明 |
|---|---|---|
| `GEO L1 L2 L3` | 28 45 75 | 髋、大腿、小腿长度 |
| `MOUNT 腿 x y 角度` | 图 6 | 髋轴位置和腿的朝向 |
| `HEIGHT` | 55 | 站立时髋平面离地高度 |
| `REACH` | 75 | 脚离髋轴的水平距离 |
| `LIFT` | 22 | 抬脚高度 |
| `STRIDE` | 34 | 最大步长，决定最高速度 $v_\text{max} = S / (\beta T)$ |
| `LINK 0–255` | 7 | 链路号，和手套一样才能通信 |
| `OBST 毫米` | 150 | 超声波避障距离（0 = 关） |
| `SHOW` / `SAVE` / `DEFAULTS` | | 显示 / 保存到 flash / 恢复默认值 |

## 5. 手套串口命令

| 命令 | 作用 |
|---|---|
| `SHOW` | 开 / 关实时显示（5 行 / 秒）：原始读数、弯曲度、手势、倾角、发出的指令、机器人状态 |
| `CAL` | 标定（和长按按键 2 秒一样） |
| `THUMB 0` / `1` | 没装 / 装了拇指传感器（没装时“竖拇指”改成“只伸小指”） |
| `AXIS p r` | 倾角方向：`AXIS -1 1` 前后反，`AXIS 1 -1` 左右反 |
| `LINK 0–255` | 链路号 |
| `DEAD 度` / `FULL 度` | 死区（默认 8°）/ 最快的角度（默认 35°） |
| `SAVE` / `DEFAULTS` | 保存 / 恢复默认（之后要重新标定） |

## 6. 无线协议（`protocol.h`）

ESP-NOW 广播，WiFi 信道 1，不加密。每个包带 `magic`、版本、类型、**链路号**、序号和 CRC-8，收到后五样都对才用。

| 包 | 方向 | 频率 | 内容 |
|---|---|---|---|
| `GlovePacket`（15 字节） | 手套 → 机器人 | 50 Hz | 模式、ARMED、事件 + 事件序号、x / y / turn、机身 pitch / roll（都是 −100–100） |
| `StatusPacket`（11 字节） | 机器人 → 手套 | 5 Hz | 状态、步态、电池电压、标志（电量低 / 前方障碍 / 限位） |

机器人 300 ms 收不到有效的包就当手套断了：停下站稳。
