# GitHub 开源项目调研与选型

> 目标：找一个**用 Seeed XIAO ESP32S3 做主控**、**接入 ROS / ROS 2** 的**完整机器人**开源项目，能照着做出来。
> 调研日期：2026 年 9 月。

## 1. 怎么搜的

| 渠道 | 关键词 / 方法 |
|---|---|
| GitHub 仓库搜索 | `xiao esp32s3 ros`、`xiao micro-ros robot`、`xiao esp32 micro-ros` |
| GitHub 代码搜索 | `board = seeed_xiao_esp32s3` + `micro_ros`（PlatformIO 工程）；`XIAO_ESP32S3` + `rclc_executor` + `cmd_vel` |
| 网页搜索 | Hackster / Instructables / Seeed Wiki 上的 “XIAO ESP32S3 + ROS 2 / micro-ROS robot” |
| 列表 | [kaiaai/awesome-micro-ros-projects](https://github.com/kaiaai/awesome-micro-ros-projects) |

结论：**用 XIAO ESP32S3 做完整 ROS 机器人的项目非常少**。PlatformIO 代码搜索里同时出现 `seeed_xiao_esp32s3` 和 `micro_ros` 的仓库只有 3 个，其中只有 1 个是完整机器人。

## 2. 候选项目对比

| 项目 | 主控 | ROS | 是什么 | 完整度 | 结论 |
|---|---|---|---|---|---|
| ⭐ [SeekerRobot/seeker-robot](https://github.com/SeekerRobot/seeker-robot) | **XIAO ESP32S3 Sense** | ROS 2 Jazzy + micro-ROS（Wi-Fi） | 六足机器人：步态、IMU、360° 雷达、SLAM、Nav2、YOLO 找东西、语音 | 软件非常完整（Docker、仿真、30 个测试固件、Wiki）；**缺机身 CAD，电路是自制贴片 PCB** | ✅ **选它** |
| [2b-t/esp32s3-microros](https://github.com/2b-t/esp32s3-microros) | XIAO ESP32S3 Sense | ROS 2 Humble + micro-ROS | 只把摄像头画面传到 ROS 2（已归档） | 不是机器人 | ❌ 只能当摄像头例子 |
| [SimonSchwaiger/xiao_ros_cam](https://github.com/SimonSchwaiger/xiao_ros_cam) | XIAO ESP32S3 Sense | micro-ROS | 摄像头节点 | 不是机器人 | ❌ |
| [aaronxavier/esp_micro_ros](https://github.com/aaronxavier/esp_micro_ros) | XIAO ESP32S3 | micro-ROS | 个人练习工程，没有 README | 很少 | ❌ |
| [yan-gd/xiaozhi-diablo_ros2](https://github.com/yan-gd/xiaozhi-diablo_ros2) | ESP32S3（小智 AI） | ROS 2 | 语音控制商用 DIABLO 机器人 | 需要买 DIABLO 整机 | ❌ |
| [dorianborian/sesame-robot](https://github.com/dorianborian/sesame-robot) | ESP32-S2 Mini / 自制板 | ❌ 没有 ROS | 8 舵机四足，资料极全（BOM、STL、装配视频） | 完整，但不是 XIAO、不是 ROS | ❌（Seeker 的前身，可参考装配经验） |
| [PrwTsrt/microros_esp32_diffdrive](https://github.com/PrwTsrt/microros_esp32_diffdrive) | 亚博 ESP32-S3 板 | ROS 2 Humble | 两轮差速小车 | 完整 | ❌ 不是 XIAO |
| [linorobot/linorobot2_hardware](https://github.com/linorobot/linorobot2_hardware) | ESP32 / Teensy / Pico 等 | ROS 2 + micro-ROS | 通用轮式机器人固件 | 很成熟 | ❌ 没有 XIAO 的现成配置，要自己移植 |

## 3. 为什么选 Seeker

1. **唯一一个**完整的 “XIAO ESP32S3 + ROS 2” 机器人：从固件、micro-ROS 桥接、ROS 2 包、仿真到自主导航全都有。
2. **ROS 功能最全**：SLAM Toolbox 建图、Nav2 导航、EKF 融合 IMU、YOLO 识别、LLM 语音指令。
3. **可以先仿真**：`seeker_gazebo` 里不接硬件就能遥控、建图、找东西，买零件之前就能学 ROS 2。
4. **固件结构清楚**：每个子系统都有独立测试固件（`test_sub_*`），硬件一步步验证。
5. **开源协议 Apache-2.0**，可以自由修改。
6. 用 Docker 打包了全部开发环境，Ubuntu / Windows / macOS 都能用。

## 4. 用 XIAO ESP32S3 Plus 行不行

行。Plus 和 Sense 是同一颗 ESP32-S3、同样 8 MB OPI PSRAM，**D0–D10 引脚到 GPIO 的对应完全一样**（对照 Arduino-ESP32 的 `variants/XIAO_ESP32S3/pins_arduino.h` 和 `variants/XIAO_ESP32S3_Plus/pins_arduino.h`，D0–D10 部分逐行相同）。区别只有：Plus 的 Flash 是 16 MB、背面多 D11–D19，**没有摄像头和麦克风**。所以固件引脚不用改，摄像头功能交给原项目本来就支持的 “卫星板”。具体见 [`software-setup.md` §4.1](software-setup.md#41-用-xiao-esp32s3-plus-做主板)。

## 5. 它缺什么、这个文件夹怎么补

| 缺口 | 影响 | 补法 |
|---|---|---|
| **没有机身结构件**（README 只写 “Sesame-based hexapod”，仓库里没有 STL / CAD） | 没法照着打印 | [`../cad/`](../cad/README.md)：按固件 `HexapodConfig.h` 的安装位置（髋轴 ±60 / ±40 mm，腿朝向 ±60° / ±90° / ±120°）和腿长画了底板、甲板、大腿 A / B、小腿，OpenSCAD 参数化，装配预览检查过干涉 |
| **电路是自制贴片 PCB**（TPS54427 + PCA9685PW + BNO085 LGA + 贴片 XIAO），BOM 里立创编号是空的 | 新手很难做 | [`wiring-guide.md`](wiring-guide.md)：用 PCA9685 模块、BNO085 模块、降压模块 + 洞洞板搭出同样的电路。引脚从 KiCad 网表逐根核对（见下表），**固件引脚不用改** |
| **腿长、限位是占位数**（注释写着 `// MEASURE`） | 直接用会走不稳、腿打架 | [`software-setup.md` §4](software-setup.md#4-按本仓库的机身改固件参数)：4 行 `sed` 改成和打印件一致 |
| 没有中文资料、没有材料清单 | — | [`build-plan.md`](build-plan.md)：BOM、预算、分阶段步骤、校核计算 |

### 从原版 PCB 网表核对出来的引脚

从 `doc/pcb/sesamepcb/production/netlist.ipc`（IPC-D-356 网表）解析，和 `RobotConfig.h` 的 `ENV_ESP32S3SENSE` 一致：

| XIAO | 网络 | 接到 |
|---|---|---|
| D0 | `OE` | PCA9685 OE（23 脚）+ R8 10 kΩ 上拉 3V3 |
| D1 | `/G_INT` | BNO085 INT |
| D2 | `/RGBADDR` | SN74LV1T34 电平转换 → SK6812 ×4 → WS2812B |
| D3 | 分压 | R7 100 kΩ 到 VIN，R6 22 kΩ 到 GND |
| D4 / D5 | `SDA` / `SCL` | PCA9685、BNO085、OLED 插座 J2，R4 / R5 4.7 kΩ 上拉 |
| D6 / D7 | `/TX` / `/RX` | 雷达插座 J4 第 3 / 4 脚 |
| D8–D10 | 未布线 | 固件里接 I²S 功放（外接） |
| 5V | `5V` | 经 D1（1N5819）接舵机电源 `5V_SV`；雷达 J4 第 1 脚也在这里 |
| 3V3 | `3V3` | PCA9685 VDD、BNO085、OLED |

电源：电池 XT30 → SW1 拨动开关 → F1 4 A 保险丝 → VIN → TPS54427 降压（反馈电阻 120 k / 22 k，$V_{\text{out}} = 0.765 \times (1 + 120/22) \approx 4.94\ \text{V}$）→ `5V_SV`。原项目固件的电池标定和低电量报警（11.1 V）按 **3S 锂电** 写，所以这里也推荐 3S。
