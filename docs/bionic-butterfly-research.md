# 仿生蝴蝶 (Bionic Butterfly) 开源项目调研

> 需求：能**真实起飞**、能**转向**，优先 Arduino 生态；预留后续接入**语音控制**与 **AI 控制**模组的能力。
> 调研时间：2026-09

---

## 1. 结论速览 (TL;DR)

| 排名 | 项目 | 主控 | 能飞 | 转向 | 无线 | 开发环境 | 推荐理由 |
|---|---|---|---|---|---|---|---|
| ⭐ 1 | [Tzenthin/Mech-Butterfly](https://github.com/Tzenthin/Mech-Butterfly) | ESP32-C3 | ✅ | ✅ | BLE / ESP-NOW / WiFi | Arduino 兼容 | 中文、真正的蝴蝶构型，含固件+原理图+翅膀模板+3D 打印件+遥控 APP，⭐123 |
| ⭐ 2 | [KazuKaku/2ServoFlapOrnithopter](https://github.com/KazuKaku/2ServoFlapOrnithopter) | Pro Mini / XIAO RP2040 / **XIAO ESP32S3** | ✅ | ✅ | PPM 遥控 | Arduino IDE | 双舵机扑翼的经典实现，**明确支持 butterfly 构型**，MIT 协议，2026 年仍在更新 |
| 3 | [RCmags/ServoFlappingControl](https://github.com/RCmags/ServoFlappingControl) | Arduino Nano / ATtiny85 | ✅ | ✅ | PWM 遥控 | Arduino IDE | 混控算法最完整（振幅/中位/上下拍频率不对称） |
| 4 | [dantiel/OrniFlight](https://github.com/dantiel/OrniFlight) | STM32 (Betaflight 目标板) | ✅ | ✅ + 陀螺增稳 | ELRS/CRSF 等 | Betaflight 工具链 | 基于 Betaflight 4.0.6 的扑翼专用飞控，GPL-3.0，进阶路线 |
| 5 | [RCmags/ornithopter-hover](https://github.com/RCmags/ornithopter-hover) | Arduino Nano + MPU-6050 | ✅ (悬停型) | ✅ | 遥控 | Arduino IDE | IMU 闭环参考，FreeCAD 结构文件 |

**参考 / 不推荐直接用于飞行：**

- [rythmraj/PETAL](https://github.com/rythmraj/PETAL---A-Bionic-Butterfly)：Pro Micro + FS-i6，**只扑翼不起飞**，可参考正弦/三角波扑翼算法。
- [SukhsimranpreetChana/Motor-Butterfly](https://github.com/SukhsimranpreetChana/Motor-Butterfly)：ESP32 + MAX9814 麦克风，声音越大扑得越快——**挂墙装饰，不能飞**，但它是“声音 → 扑翼”最简单的示例。
- [xhtx0510/Butterfly](https://github.com/xhtx0510/Butterfly)：“创源启明”仿生蝴蝶代码，STM32 + 磁编码电机 PID，资料很少。
- [KEYLAN000/butterfly](https://github.com/KEYLAN000/butterfly)：几乎无文档。

**立创开源硬件平台 (oshwhub) 上的蝴蝶飞控板**（PCB 可直接打样，本次环境无法访问该站，信息来自搜索摘要）：

- [仿生蝴蝶（深圳创电优选）](https://oshwhub.com/ni_hao_a/zh_fangshenghudie_1)：翼展约 85 cm、重 75 g、扑翼 3–5 Hz、2S 180 mAh、续航 3–5 min，集成六轴陀螺仪，ESP-NOW / WiFi / 蓝牙控制，可直飞、盘旋。
- [类舵机方案仿生蝴蝶主控 V3.0](https://oshwhub.com/jf2641163240/v3-0-kai-yuan-fang-sheng-hu-die)
- [YDIFLY 蝴蝶扑翼机飞控主控板](https://oshwhub.com/ydi_pcb/ydi-flapping-wing-flight-control-main-control-board-open-source)（湖北大学）

---

## 2. 最终推荐方案

### 主控选 ESP32-S3（用 Arduino IDE 开发）

纯 Arduino（ATmega328P，16 MHz、2 KB RAM）**能飞、能转向**，但无法承载语音识别 / AI 推理，后续扩展一定要换板。ESP32 系列可以直接用 Arduino IDE 编程，所以“Arduino 生态”这个要求不受影响：

| 需求 | Arduino Nano / Pro Mini | ESP32-C3 | **ESP32-S3（推荐）** |
|---|---|---|---|
| 双舵机扑翼 + 转向 | ✅ | ✅ | ✅ |
| 无线（无需额外接收机） | ❌ 需要 nRF24/PPM 接收机 | ✅ BLE / WiFi / ESP-NOW | ✅ BLE / WiFi / ESP-NOW |
| IMU 增稳 | 勉强 | ✅ | ✅ |
| 离线语音识别 | ❌ | ❌ | ✅ ESP-SR（WakeNet / MultiNet） |
| 接入大模型 AI | ❌ | 勉强 | ✅（可参考 [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)） |
| 算力 / 内存 | 16 MHz / 2 KB | 160 MHz / 400 KB | 240 MHz 双核 / 512 KB + PSRAM |

**落地路线：**

1. **机体 + 翅膀**：照搬 **Mech-Butterfly**（翅膀 A3 模板、碳杆、3D 打印件）。
2. **扑翼 / 转向算法**：参考 **2ServoFlapOrnithopter** 的 `…forESP32S3` 分支和 **ServoFlappingControl** 的混控逻辑。
3. **主控**：Seeed XIAO ESP32S3（约 21 × 17.5 mm，重量很轻），先用 ESP-NOW / BLE 手动遥控飞稳。
4. **增稳（可选）**：加 MPU-6050 / ICM-42688，参考 ornithopter-hover 和 OrniFlight 的陀螺混控思路。
5. **语音 / AI**：见第 4 节，**优先放在地面端**。

---

## 3. 起飞与转向原理

双舵机方案里，左右翅膀各由一个舵机独立驱动，每个舵机的角度是一个带偏置的周期信号：

$$
\theta_L(t) = \theta_0 + \delta_{p} + \delta_{r} + A_L \cdot w(2\pi f t)
$$

$$
\theta_R(t) = \theta_0 + \delta_{p} - \delta_{r} + A_R \cdot w(2\pi f t)
$$

| 符号 | 含义 | 对应遥控通道 |
|---|---|---|
| $\theta_0$ | 舵机中位角 | — |
| $f$ | 扑翼频率，蝴蝶构型一般为 $3 \sim 5\ \text{Hz}$ | 油门 / Ch5 |
| $A_L,\ A_R$ | 左右扑动幅值 | 油门（整体）+ 方向舵（差动） |
| $\delta_p$ | 两翼**同向**偏置，改变拍动中心的前后位置 → **俯仰** | 升降舵 Ch2 |
| $\delta_r$ | 两翼**反向**偏置 → **横滚 / 转向** | 副翼 Ch1 |
| $w(\cdot)$ | 波形：正弦，或者三角波 → 方波（油门越大越接近方波，舵机输出功率越大） | — |

- **起飞（Lift-off）**：升力近似满足 $L \propto \rho\, f^{2} A^{2} S$，所以提高 $f$ 或 $A$ 就能增加升力；起飞条件是 $L > mg$，因此**重量是第一约束**。参考 oshwhub 上的实测数据：$m \approx 75\ \text{g}$，翼展 $\approx 85\ \text{cm}$。
- **转向（Turning）**：
  - **差动幅值**：$A_L \neq A_R$ 时，两侧升力 / 推力不对称，机体随之产生偏航和横滚力矩：$M_{\text{yaw}} \propto (A_L^{2} - A_R^{2})$。
  - **中位偏置**：$\delta_r \neq 0$ 时，两侧拍动中心不对称，产生横滚，再通过倾斜转弯改变航向。
  - 2ServoFlapOrnithopter 的 README 说明：蝴蝶 / 蜻蜓构型的副翼逻辑与带平尾构型**相反**，代码里需要把 `/2` 改为 `/1.12`。

核心混控伪代码（Arduino / ESP32 通用）：

```cpp
// phase: 0 ~ 2π, advances at 2π·f·dt
float wave  = sinf(phase);                      // or triangle→square blend
float angL  = center + pitchBias + rollBias + ampL * wave;
float angR  = center + pitchBias - rollBias + ampR * wave;
servoL.write(angL);
servoR.write(180 - angR);                       // mirrored mounting
```

---

## 4. 语音 / AI 扩展架构（关键：重量预算）

蝴蝶机的有效载荷非常有限。一般经验是新增质量 $\Delta m$ 不超过整机质量的 $10\%$ 左右：

$$
\Delta m \lesssim 0.1\, m_{\text{total}} \approx 7\ \text{g} \quad (m_{\text{total}} = 75\ \text{g})
$$

所以**推荐把语音 / AI 放在地面端**，机上只保留一个很薄的“指令接收层”：

```
 ┌──────────────── 地面端 Ground Station ────────────────┐
 │  麦克风 → 离线语音模块 (ASRPRO / CI1302 / LD3320)       │
 │      或  ESP32-S3-BOX / 手机 APP → 云端大模型 (LLM)    │
 │                    │ 解析成标准指令                     │
 │                    ▼                                   │
 │        ESP32 遥控器 ──── ESP-NOW / BLE ────┐            │
 └────────────────────────────────────────────┼───────────┘
                                              ▼
 ┌──────────────── 机上 Butterfly (ESP32-S3) ─────────────┐
 │  CommandParser → FlightState{throttle, roll, pitch}    │
 │  → Mixer(θ_L, θ_R) → 2× Servo      (+ IMU 增稳可选)     │
 └────────────────────────────────────────────────────────┘
```

**统一指令协议**：手动遥控、语音、AI 都发同一种指令，机上固件不需要区分指令来源：

| 指令 | 含义 | 语音示例 |
|---|---|---|
| `TAKEOFF` | 渐增 $f, A$ 到起飞值 | “起飞” |
| `LAND` | 渐降 $f, A$ | “降落” |
| `TURN_L <deg>` / `TURN_R <deg>` | 设置 $\delta_r$ / 差动幅值 | “左转” / “右转” |
| `UP` / `DOWN` | 调整 $f$ 或 $\delta_p$ | “飞高一点” |
| `HOVER` / `CIRCLE` | 预设动作序列 | “绕圈飞” |
| `RAW thr roll pitch` | 原始摇杆量（手动 / AI 闭环用） | — |

**扩展路径：**

- **语音 v1（最简单）**：地面端用离线语音模块（如 ASRPRO / 天问、CI1302）识别固定词条，通过 UART 交给遥控器 ESP32，再由 ESP-NOW 发给蝴蝶。机上零改动。
- **语音 v2**：地面端换成 ESP32-S3 + I2S 麦克风（INMP441），跑乐鑫 ESP-SR，支持自定义唤醒词和命令词。
- **AI v1**：用 [xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) 这类“ESP32 + 大模型”框架，通过 MCP / function-calling 把 `TURN_L`、`TAKEOFF` 等注册成工具，让大模型根据自然语言生成指令。
- **AI v2（机载智能）**：参考 2026 年 arXiv 论文 [*Spiking Neural Network Control of a Flapping-Wing Robot on Resource-Constrained Hardware*](https://arxiv.org/html/2605.19430)。该工作在 ESP32 上跑 SNN，实现了 30 g 以下蝴蝶扑翼机的机载闭环稳定飞行。
- **视觉（远期）**：地面或手机做视觉跟踪，给出 `RAW` 指令闭环控制；或者等载荷允许后再上机载 ESP32-S3 摄像头版本。

---

## 5. 硬件清单参考 (BOM)

| 部件 | 推荐 | 备注 |
|---|---|---|
| 主控 | Seeed XIAO ESP32S3 / ESP32-C3 SuperMini | Arduino IDE 直接支持 |
| 舵机 ×2 | BLUEARROW AF D43S-6.0-MG（2ServoFlap 推荐），或 PTK7350 | 需要高速、轻量、金属齿 |
| 电池 | 1S/2S LiPo，70–180 mAh | 2S 需要搭配高压舵机 |
| IMU（可选） | MPU-6050 / ICM-42688 | 增稳用 |
| 翅膀 | 碳纤维杆 + 轻薄膜 / 纸 | Mech-Butterfly 提供 A3 模板 |
| 地面语音 | ASRPRO / CI1302 模块，或 ESP32-S3-BOX | 不上机 |

---

## 6. 参考来源 (Sources)

- <https://github.com/Tzenthin/Mech-Butterfly>
- <https://github.com/KazuKaku/2ServoFlapOrnithopter>
- <https://github.com/RCmags/ServoFlappingControl>
- <https://github.com/RCmags/ornithopter-hover>
- <https://github.com/dantiel/OrniFlight>
- <https://github.com/rythmraj/PETAL---A-Bionic-Butterfly>
- <https://github.com/SukhsimranpreetChana/Motor-Butterfly>
- <https://github.com/xhtx0510/Butterfly>
- <https://github.com/KEYLAN000/butterfly>
- <https://github.com/topics/ornithopter>
- <https://oshwhub.com/ni_hao_a/zh_fangshenghudie_1>
- <https://oshwhub.com/jf2641163240/v3-0-kai-yuan-fang-sheng-hu-die>
- <https://oshwhub.com/ydi_pcb/ydi-flapping-wing-flight-control-main-control-board-open-source>
- <https://arxiv.org/html/2605.19430>
- <https://www.instructables.com/Opensource-Ornithopter-Prototype-Arduino-Powered-a/>
