# 仿生手固件（Arduino Uno R3）

| 目录 / 文件 | 作用 |
|---|---|
| [`bionic_hand_uno/bionic_hand_uno.ino`](bionic_hand_uno/bionic_hand_uno.ino) | 主程序：读电位器、驱动舵机、串口命令、按键、EEPROM |
| [`bionic_hand_uno/hand_core.h`](bionic_hand_uno/hand_core.h) | 和硬件无关的核心：标定、滤波、限位、限速、手势、掉线检测、设置校验（CRC） |
| [`tests/test_core.cpp`](tests/test_core.cpp) | 在电脑上跑的单元测试，不需要硬件 |

原理（公式）见 [`../docs/principles.md`](../docs/principles.md)，接线和标定步骤见 [`../docs/assembly-guide.md`](../docs/assembly-guide.md)。

---

## 1. 开发环境

1. 安装 **Arduino IDE 2.x**。
2. 开发板选 **Arduino Uno**。
3. 只用 IDE 自带的 `Servo` 和 `EEPROM` 库，**不用装任何第三方库**。
4. 打开 `bionic_hand_uno/bionic_hand_uno.ino` → 上传 → 打开串口监视器：**115200**，行尾选"换行"。

---

## 2. 引脚

| 通道 | 手指 | 电位器 | 舵机 |
|---|---|---|---|
| 0 | 食指 | A0 | D3 |
| 1 | 中指 | A1 | D5 |
| 2 | 无名指 | A2 | D6 |
| 3 | 小指 | A3 | D9 |
| 4 | 拇指弯曲 | A4 | D10 |
| 5 | 拇指转动 | A5 | D11 |
| — | 按键（可选） | D2 → GND | — |
| — | 板载 LED | D13 | 常亮 = 跟随；慢闪 = 标定中；快闪 = 手套掉线 |

- A0–A5 开了内部上拉（`INPUT_PULLUP`）。手套网线没插时读数都在 1023 附近，固件据此判断"手套掉线"。
- 用了 `Servo` 库以后，D9 / D10 不能再用 `analogWrite`（本项目没用到）。

---

## 3. 运行模式

| 模式 | 进入方法 | 行为 |
|---|---|---|
| **MIRROR**（开机默认） | `MODE MIRROR` 或短按按键 | 机械手跟随手套 |
| **DEMO** | `MODE DEMO` 或短按按键 | 每 1.5 秒自动换一个手势 |
| **GESTURE** | `G 手势名` | 停在一个手势 |
| **MANUAL** | `SERVO ch us` | 每个舵机单独给脉宽（调限位用） |

手套掉线（MIRROR 模式下，6 路同时 ≥ 1015 持续 200 ms）：先保持当前姿态 0.5 秒，然后以限速慢慢张开；插回网线立刻恢复跟随。

---

## 4. 串口命令（不分大小写）

| 命令 | 作用 |
|---|---|
| `HELP` | 列出命令和手势名 |
| `SHOW` | 每个通道的原始读数、位置 n（0–1000）、脉宽、标定值、限位 |
| `STREAM` | 开 / 关：每 0.2 秒打印 6 路原始读数（查接线用） |
| `CAL OPEN` / `CAL HALF` / `CAL CLOSED` | 记下手张开 / 半握 / 握拳时 6 路的读数（每路平均 32 次） |
| `LIM ch us_open us_closed` | 通道 ch 在伸直 / 握紧时的脉宽，500–2500 µs；`us_closed < us_open` 就是反向 |
| `SERVO ch us` | 进入 MANUAL 模式，把通道 ch 转到 us |
| `G name` | 手势：`paper` `rock` `scissors` `ok` `like` `one` `two` `three` `four` `rock_on` |
| `MODE MIRROR` / `MODE DEMO` | 切换模式 |
| `SLEW us` | 每 20 ms 最多变化多少 µs（默认 40，约 200°/s） |
| `SAVE` | 把标定、限位、限速存进 EEPROM（带 CRC 校验） |
| `DEFAULTS` | 恢复默认值（不自动保存） |

**按键**：短按在 MIRROR / DEMO 之间切换；**按住 2 秒**进入引导标定，之后按串口提示依次摆出"张开 → 半握 → 握拳"，每摆好一个短按一下。三步都通过检查后自动保存。

> 第一次上电（EEPROM 里没有设置）时，所有舵机的限位都是 1500 / 1500，也就是全部停在中位。这是故意的：方便在中位装舵盘。装好之后再按 [组装指南 §6.3](../docs/assembly-guide.md#63-设置舵机限位-lim) 设置 `LIM`。

---

## 5. 单元测试

```bash
g++ -std=gnu++11 -O1 -Wall -Wextra -Werror -I bionic_hand/firmware/bionic_hand_uno \
    bionic_hand/firmware/tests/test_core.cpp -o test_core && ./test_core
```

测试内容：
- 三点标定：两个方向、单调性、非法标定的处理；
- 四舍五入插值；
- EMA + 死区：不抖、能追平、两头能到位；
- 限位映射和反向；
- 限速；
- 设置的 CRC；
- 手势表；
- 跟随；
- 掉线：去抖、保持、张开、恢复；
- 模式切换。

CI（`.github/workflows/bionic_hand.yml`）会跑这些测试，并用 `arduino:avr:uno` 编译整个 sketch。
