# 机械臂固件（Firmware）

| 目录 | 作用 |
|---|---|
| `arm_controller/` | **主控固件**（ESP32 + Bluepad32）：G7 Pro 手柄、逆运动学、平滑运动、示教回放、夹爪检测、过载 / 急停保护、手机网页、文字指令 |
| `tests/` | 电脑上运行的单元测试（运动学、限速限加速、回放、保护、文字指令），不需要硬件 |
| `../tools/arm_client.py` | 电脑 / AI 发文字指令的小工具（Wi-Fi 或 USB 串口） |

原理见 [`docs/motion-control.md`](../docs/motion-control.md)，接线和组装见 [`docs/assembly-guide.md`](../docs/assembly-guide.md)，完整流程见 [`docs/build-plan.md`](../docs/build-plan.md)。

| 文件 | 内容 |
|---|---|
| `arm_controller.ino` | 主循环（50 Hz）、手柄读取、输出、网页 |
| `motion.h` | 状态机：点动、XYZ 点动、五次多项式轨迹、示教回放、夹爪检测、过载 / 急停（纯 C++，可单元测试） |
| `kinematics.h` | 正 / 逆运动学（纯 C++） |
| `commands.h` | 文字指令解析（纯 C++） |
| `params.h` | 全部可调参数和默认值 |
| `cli.cpp` | 配置命令：`SET` / `SAVE PARAMS` / `PULSE` / `MARK` / `I2C` / `PAIR` |
| `pca9685.*` `ina226.*` | 两个 I²C 芯片的精简驱动（不需要装第三方库） |
| `storage.*` | 参数和路点存进 flash（NVS） |
| `web_ui.h` | 手机网页 |
| `config.h` | 引脚、I²C 地址、Wi-Fi 名称和密码 |

---

## 1. 开发环境

1. 安装 **Arduino IDE 2.x**。
2. **文件 → 首选项 → 附加开发板管理器网址**，填入：
   `https://raw.githubusercontent.com/ricardoquesada/esp32-arduino-lib-builder/master/bluepad32_files/package_esp32_bluepad32_index.json`
3. **开发板管理器**：搜索 `esp32_bluepad32`，安装。
4. **开发板**：选 **ESP32 + Bluepad32 Arduino → ESP32 Dev Module**。
5. 打开 `firmware/arm_controller/arm_controller.ino`，**不需要安装任何第三方库**。
6. USB 线连上 ESP32，选对端口，点**上传**。如果上传时一直显示 `Connecting....`，按住板子上的 **BOOT** 键，等开始写入再松开。

> ⚠️ 必须用**原版 ESP32**（ESP32-WROOM-32 / 32E）。G7 Pro 的蓝牙模式需要经典蓝牙，ESP32-S3 / C3 只有 BLE。
> 💡 CI（`.github/workflows/firmware.yml`）每次提交都会用同一个开发板包自动编译，并运行单元测试。

## 2. 引脚速查

![图 2 信号接线](../docs/img/fig2-signals.svg)

| ESP32 引脚 | 接什么 |
|---|---|
| GPIO21 | I²C SDA → PCA9685、INA226 |
| GPIO22 | I²C SCL → PCA9685、INA226 |
| GPIO25 | 继电器 IN（高电平 = 舵机上电） |
| GPIO26 | 有源蜂鸣器 |
| GPIO16 / GPIO17 | UART2 RX / TX ↔ 语音模块 TX / RX（可选） |
| GPIO2 | 状态 LED（多数开发板自带） |
| 5V | ← MP1584 5.0 V（经 SS14） |
| 3V3 | → PCA9685 VCC、INA226 VCC、蜂鸣器 |

| I²C 设备 | 地址 |
|---|---|
| INA226 | 0x40（默认） |
| PCA9685 | **0x41**（A0 焊盘短接） |

| PCA9685 通道 | 0 | 1 | 2 | 3 | 4 | 5 |
|---|---|---|---|---|---|---|
| 关节 | J1 底座 | J2 大臂 | J3 小臂 | J4 手腕俯仰 | J5 手腕旋转 | J6 夹爪 |

**状态 LED**：常亮 = 手柄已连接；慢闪 = 等待手柄；快闪 = 急停 / 过载 / 故障。

## 3. 连接 G7 Pro

1. 手柄背面的模式开关拨到**蓝牙**。
2. 按 **Xbox 键**开机，然后**长按配对键**，指示灯快闪。
3. 串口显示 `gamepad connected`，手柄震一下，就连上了。以后开机会自动重连。
4. 连不上，或者想换一个手柄：串口输入 `PAIR`（清除已配对记录），再重新配对。

按键功能见图 5（[`docs/build-plan.md`](../docs/build-plan.md#手柄怎么操控)）。

![图 5 G7 Pro 按键功能](../docs/img/fig5-gamepad.svg)

## 4. 文字指令（语音 / AI / 网页 / 串口）

从 **USB 串口**（115200）、**语音模块串口**（UART2，9600）、**手机网页**的输入框，或者 **`tools/arm_client.py`** 都可以发。大小写都行，一行一条。单位是 mm 和度；夹爪 0 = 张开，100 = 闭合。

| 指令 | 作用 |
|---|---|
| `ON` / `OFF` | 舵机上电（先到停放姿态）/ 回停放姿态后断电（`PARK` 同 `OFF`） |
| `HOME` | 回原位 |
| `STOP` | 停止当前动作；过载后恢复 |
| `MODE JOINT` / `MODE CART` | 切换手柄模式 |
| `SPEED 1` / `2` / `3` | 慢 / 中 / 快 |
| `J <1–6> <角度>` | 单个关节走到指定角度（J6 是 0–100 %） |
| `JOINTS a b c d e [g]` | 5 个关节（和夹爪）一起走 |
| `MOVE x y z [pitch [roll]]` | 工具点走到 $(x, y, z)$；不写 pitch / roll 就保持当前值 |
| `MOVEBY dx dy dz` | 相对移动 |
| `UP` / `DOWN` / `LEFT` / `RIGHT` / `FORWARD` / `BACK` `[mm]` | 相对移动，默认 20 mm |
| `TURN <角度>` | 底座相对转动 |
| `PITCH <角度>` / `ROLL <角度>` | 工具俯仰 / 旋转（绝对值） |
| `GRIP <0–100>` / `OPEN` / `CLOSE` | 夹爪（闭合时夹到东西会自动停） |
| `REC` / `DELETE` / `CLEAR` | 记录路点 / 删除最后一个 / 清空 |
| `PLAY` / `PLAY LOOP` | 播放一次 / 循环播放 |
| `SAVE` | 把路点存进 flash |
| `STATUS` | 状态、角度、位置、电流、电压 |

回复以 `ok` 或 `error:` 开头。例：

```
> MOVE 160 0 40 -90
ok move to 160 0 40 pitch -90 roll 0
> MOVE 600 0 100
error: out of reach
```

**手柄优先**：执行文字指令或回放时，只要动一下摇杆、扳机或十字键 ←→，动作马上停止，控制权回到手柄。

### 给 AI 用

AI 智能体只需要一个工具：“发送一条文字指令，返回回复”。可以直接调用 `tools/arm_client.py`，或者发 HTTP 请求 `GET http://192.168.4.1/cmd?c=<指令>`（`GET /api` 返回 JSON 状态）。在提示词里写清楚：坐标系是 $x$ 朝前、$y$ 朝左、$z$ 朝上，单位是 mm；每次先 `STATUS` 看当前位置；够不到时会返回 `error`。

### 给语音模块用

在 CI-03T / ASR-PRO 的配置工具里，给每个词条设置“识别后串口输出”一行 ASCII 文字（结尾加 `\r\n`，波特率 9600）。例如：

| 说 | 串口输出 |
|---|---|
| 机械臂上电 | `ON` |
| 向上 / 向下 | `UP 30` / `DOWN 30` |
| 向左 / 向右 | `LEFT 30` / `RIGHT 30` |
| 抓住 / 松开 | `CLOSE` / `OPEN` |
| 回家 | `HOME` |
| 开始表演 | `PLAY` |
| 停 | `STOP` |

## 5. 配置命令（串口 / 网页）

| 命令 | 作用 |
|---|---|
| `HELP` | 列出所有命令 |
| `LIST` | 列出所有参数和当前值 |
| `GET <名字>` / `SET <名字> <值>` | 读 / 改参数（立即生效） |
| `SAVE PARAMS` | 参数存进 flash |
| `DEFAULTS` | 恢复默认参数（舵机断电时才能用，不会自动保存） |
| `PULSE <1–6> <µs>` | 标定用：给某个舵机直接发脉宽（400–2700） |
| `PULSE OFF` | 结束标定，关节重新按角度控制 |
| `MARK <1–6> <角度>` | “这个关节现在就在这个角度”，记两次就算出标定值（见组装指南第 6 步） |
| `I2C` | 扫描 I²C 地址 |
| `PAIR` | 清除已配对的手柄 |
| `REBOOT` | 重启 |

## 6. 参数

每个关节有 8 个参数（`j1_` … `j6_` 开头）：

| 参数 | 含义 |
|---|---|
| `jN_us0`、`jN_usdeg` | 标定：脉宽 = `us0` + `usdeg` × 角度（用 `MARK` 自动算，一般不用手改） |
| `jN_qmin`、`jN_qmax` | 软件限位（度；J6 是 %） |
| `jN_vmax`、`jN_amax` | 最大速度（°/s）、最大加速度（°/s²） |
| `jN_home`、`jN_park` | HOME 姿态、停放姿态 |

其他参数：

| 参数 | 默认 | 含义 |
|---|---|---|
| `d1` `l2` `l3` `l4` | 75 105 100 120 | 机械尺寸（mm，见图 4） |
| `z_min` / `r_min` | 10 / 60 | 工具点最低高度 / 离底座轴线的最小距离（mm） |
| `deadband` / `expo` | 0.08 / 0.3 | 摇杆死区 / 手感曲线 |
| `v_lin` / `v_ang` | 80 / 60 | XYZ 模式最高速度：mm/s、°/s |
| `speed_lvl` | 2 | 开机时的速度档 |
| `over_amps` / `over_s` / `fault_s` | 6 / 0.5 / 3 | 过载电流（A）/ 判定时间（s）/ 断电时间（s） |
| `estop_v` / `low_v` | 3.0 / 5.3 | 急停判定电压 / 低压警告（V） |
| `grip_amps` / `grip_backoff` | 0.6 / 3 | 夹爪检测的电流增量（A）/ 夹住后松开多少（%） |
| `dwell_s` | 0.3 | 回放时每个路点停留（s） |
| `pulse_min` / `pulse_max` | 500 / 2500 | 任何舵机的脉宽上下限（µs） |
| `pwm_osc_hz` | 25000000 | PCA9685 振荡器频率。实际芯片在 24–27 MHz 之间；舵机中位明显不准时再调 |
| `shunt_ohm` | 0.01 | INA226 采样电阻（Ω） |

改完参数记得 `SAVE PARAMS`。

## 7. 单元测试

```bash
g++ -std=c++17 -O1 -Wall -Wextra -Werror -I firmware/arm_controller firmware/tests/test_core.cpp -o test_core
./test_core
```

测试覆盖：正 / 逆运动学往返（2000 组随机姿态，误差 < 0.05°），限速限加速、不过冲，上电 / 停放 / 断电流程，关节和 XYZ 点动（走直线、边界滑动、不低于桌面），示教回放和手柄接管，过载 → 冻结 → 断电，急停，夹爪检测，文字指令，参数表。
