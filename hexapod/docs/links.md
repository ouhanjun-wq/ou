# 所有网站汇总（下载 · 安装 · 接线资料 · 购买）

> 按 “做到哪一步用哪个” 排列。⭐ = 一定会用到。

## 1. 开源项目本身

| 网站 | 用途 |
|---|---|
| ⭐ [SeekerRobot/seeker-robot](https://github.com/SeekerRobot/seeker-robot) | **主项目**：ROS 2 工作空间 + ESP32 固件 + Docker 环境 + PCB |
| ⭐ [Seeker Wiki：Setup](https://github.com/SeekerRobot/seeker-robot/blob/main/docs/wiki/Setup.md) | 原项目的环境安装说明（英文） |
| ⭐ [Seeker Wiki：IRL-Tests](https://github.com/SeekerRobot/seeker-robot/blob/main/docs/wiki/IRL-Tests.md) | 原项目的真机上机测试手册 |
| [Seeker Wiki：MCU-Sketches](https://github.com/SeekerRobot/seeker-robot/blob/main/docs/wiki/MCU-Sketches.md) | 每个测试固件的用途、命令、排错 |
| [Seeker Wiki：MCU-Firmware](https://github.com/SeekerRobot/seeker-robot/blob/main/docs/wiki/MCU-Firmware.md) | 固件结构、编译开关 `BRIDGE_ENABLE_*` |
| [Seeker Wiki：ROS2-Packages](https://github.com/SeekerRobot/seeker-robot/blob/main/docs/wiki/ROS2-Packages.md) | 每个 ROS 2 包、话题、launch 文件 |
| [Seeker Wiki：Simulation](https://github.com/SeekerRobot/seeker-robot/blob/main/docs/wiki/Simulation.md) | Gazebo 仿真 |
| [Seeker 引脚定义 RobotConfig.h](https://github.com/SeekerRobot/seeker-robot/blob/main/mcu_ws/lib/RobotConfig/RobotConfig.h) | XIAO 引脚、I²C 地址、PCA9685 通道映射 |
| [Seeker 机身参数 HexapodConfig.h](https://github.com/SeekerRobot/seeker-robot/blob/main/mcu_ws/lib/RobotConfig/HexapodConfig.h) | 腿长、舵机标定、步态参数 |
| [Seeker 原版 PCB（KiCad）](https://github.com/SeekerRobot/seeker-robot/tree/main/doc/pcb/sesamepcb) | Sesame V2 PCB 原理图、Gerber、BOM |
| [dorianborian/sesame-robot](https://github.com/dorianborian/sesame-robot) | Seeker 的 “祖先”：Sesame 四足机器人（ESP32），参考它的打印和装配经验 |

## 2. 电脑环境安装

| 网站 | 用途 |
|---|---|
| ⭐ [Ubuntu 24.04 桌面版下载](https://ubuntu.com/download/desktop) | 推荐的电脑系统 |
| [balenaEtcher](https://etcher.balena.io/) / [Rufus](https://rufus.ie/) | 做 Ubuntu 启动 U 盘 |
| ⭐ [Docker Engine 安装（Ubuntu）](https://docs.docker.com/engine/install/ubuntu/) | 装 Docker |
| [Docker 免 sudo 设置](https://docs.docker.com/engine/install/linux-postinstall/) | 把用户加进 docker 组 |
| [Docker Desktop（Windows / macOS）](https://www.docker.com/products/docker-desktop/) | Windows / Mac 装 Docker |
| [Git](https://git-scm.com/downloads) · [Git LFS](https://git-lfs.com/) | 下载代码、下载 PCB 大文件 |
| [VS Code](https://code.visualstudio.com/) | 看 / 改代码 |
| [VcXsrv（Windows 显示 RViz / Gazebo）](https://sourceforge.net/projects/vcxsrv/) | Windows 的 X 服务器 |
| [XQuartz（macOS）](https://www.xquartz.org/) | macOS 的 X 服务器 |
| [usbipd-win](https://github.com/dorssel/usbipd-win) | Windows 把 USB 串口转给 WSL / Docker |
| [NVIDIA Container Toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html) | 可选：Gazebo 用显卡加速 |

## 3. ROS 2 / micro-ROS / 导航

| 网站 | 用途 |
|---|---|
| ⭐ [ROS 2 Jazzy 文档](https://docs.ros.org/en/jazzy/) | 官方文档（Seeker 用的版本） |
| [ROS 2 Jazzy 安装（Ubuntu，不用 Docker 时）](https://docs.ros.org/en/jazzy/Installation/Ubuntu-Install-Debs.html) | 想在电脑上直接装 ROS 2 |
| [ROS 2 入门教程](https://docs.ros.org/en/jazzy/Tutorials.html) | 话题、节点、launch 基础 |
| ⭐ [micro-ROS 官网](https://micro.ros.org/) | ESP32 和 ROS 2 之间的桥梁 |
| [micro-ROS Agent](https://github.com/micro-ROS/micro-ROS-Agent) | 电脑端的 Agent |
| [micro_ros_platformio](https://github.com/micro-ROS/micro_ros_platformio) | PlatformIO 里编译 micro-ROS 库 |
| [teleop_twist_keyboard](https://github.com/ros2/teleop_twist_keyboard) | 键盘遥控 |
| ⭐ [Nav2 导航](https://docs.nav2.org/) | 自主导航 |
| [Nav2 地图保存 map_server](https://docs.nav2.org/configuration/packages/configuring-map-server.html) | `map_saver_cli` 保存地图 |
| ⭐ [SLAM Toolbox](https://github.com/SteveMacenski/slam_toolbox) | 建图 |
| [robot_localization（EKF）](https://docs.ros.org/en/jazzy/p/robot_localization/) | IMU 融合 |
| [Gazebo Harmonic](https://gazebosim.org/docs/harmonic/getstarted/) | 仿真 |
| [Ultralytics YOLO](https://docs.ultralytics.com/) | 找东西用的目标识别 |

## 4. 主控 XIAO ESP32S3 Plus / Sense

| 网站 | 用途 |
|---|---|
| ⭐ [XIAO ESP32S3 Plus 产品页](https://www.seeedstudio.com/Seeed-Studio-XIAO-ESP32S3-Plus-p-6361.html) | 你用的主板：16 MB Flash、8 MB PSRAM、背面多 9 个 GPIO |
| [Arduino-ESP32 的 XIAO_ESP32S3_Plus 引脚定义](https://github.com/espressif/arduino-esp32/blob/master/variants/XIAO_ESP32S3_Plus/pins_arduino.h) | 核对 D0–D19 对应的 GPIO（D0–D10 和 Sense 相同） |
| [XIAO ESP32S3 Sense 产品页](https://www.seeedstudio.com/XIAO-ESP32S3-Sense-p-5639.html) | 摄像头卫星板（可选）、原项目用的主板 |
| ⭐ [XIAO ESP32S3 入门 Wiki](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/) | **引脚图**、进下载模式（BOOT 键）、供电 |
| [XIAO ESP32S3 引脚复用](https://wiki.seeedstudio.com/xiao_esp32s3_pin_multiplexing/) | 每个引脚能做什么（I²C / UART / I²S） |
| [XIAO ESP32S3 Sense 摄像头](https://wiki.seeedstudio.com/xiao_esp32s3_camera_usage/) | 摄像头排线怎么插 |
| [XIAO ESP32S3 Sense 麦克风](https://wiki.seeedstudio.com/xiao_esp32s3_sense_mic/) | PDM 麦克风 |
| [PlatformIO 的 seeed_xiao_esp32s3 板卡](https://docs.platformio.org/en/latest/boards/espressif32/seeed_xiao_esp32s3.html) | PlatformIO 板卡配置 |
| [pioarduino（Seeker 用的 ESP32 平台）](https://github.com/pioarduino/platform-espressif32) | PlatformIO 的 Arduino-ESP32 3.x 平台 |
| [Arduino-ESP32 文档](https://docs.espressif.com/projects/arduino-esp32/en/latest/) | ESP32 Arduino 核心 |

## 5. 各模块接线资料

| 模块 | 资料 |
|---|---|
| ⭐ PCA9685 舵机驱动 | [Adafruit 产品页 815](https://www.adafruit.com/product/815) · [接线教程（引脚、V+ 端子、OE）](https://learn.adafruit.com/16-channel-pwm-servo-driver) |
| ⭐ BNO085 IMU | [Adafruit 产品页 4754](https://www.adafruit.com/product/4754) · [引脚说明（DI = 地址脚，接高 = 0x4B）](https://learn.adafruit.com/adafruit-9-dof-orientation-imu-fusion-breakout-bno085/pinouts) · [Adafruit_BNO08x 库](https://github.com/adafruit/Adafruit_BNO08x) |
| ⭐ LD14P 激光雷达 | [乐动 LD14P 产品页](https://www.ldrobot.com/ProductDetails?sensor_name=LD14P) · [数据手册（中文 PDF）](https://www.ldrobot.com/images/2023/03/02/LDROBOT_LD14P%20DataSheet_CN_v0.4_Wlmrp6QT.pdf) · [微雪 D200 / LD14P Wiki（接线、协议）](https://www.waveshare.com/wiki/D200_LiDAR_Kit) · [官方 ROS 2 驱动 ldlidar_sl_ros2](https://github.com/ldrobotSensorTeam/ldlidar_sl_ros2) · [ESP32 驱动库 kaiaai/LDS](https://github.com/kaiaai/LDS) · [2D 雷达汇总](https://github.com/kaiaai/awesome-2d-lidars) |
| MAX98357A 功放 | [Adafruit 产品页 3006](https://www.adafruit.com/product/3006) · [接线教程](https://learn.adafruit.com/adafruit-max98357-i2s-class-d-mono-amp) |
| SSD1306 OLED | [Adafruit OLED 教程](https://learn.adafruit.com/monochrome-oled-breakouts) |
| WS2812 / SK6812 灯 | [Adafruit NeoPixel 指南（电平、限流电阻）](https://learn.adafruit.com/adafruit-neopixel-uberguide) · [FastLED](https://github.com/FastLED/FastLED) |
| MG90S 舵机 | 淘宝搜 `MG90S 金属齿 180度`；接线：棕 GND · 红 V+ · 橙 信号 |

## 6. 3D 打印 / PCB

| 网站 | 用途 |
|---|---|
| ⭐ [OpenSCAD 下载](https://openscad.org/downloads.html) | 打开本仓库 `cad/*.scad`，改参数、导出 STL |
| [Bambu Studio](https://bambulab.com/en/download/studio) / [OrcaSlicer](https://github.com/SoftFever/OrcaSlicer) / [Cura](https://ultimaker.com/software/ultimaker-cura/) | 切片软件 |
| [KiCad](https://www.kicad.org/download/) | 打开原版 Sesame V2 PCB 工程 |
| [Fabrication Toolkit（KiCad 插件）](https://github.com/bennymeg/Fabrication-Toolkit) | 一键导出嘉立创生产文件 |
| [嘉立创 JLCPCB](https://jlcpcb.com/) · [立创商城](https://www.szlcsc.com/) | PCB 打样 + 贴片、元件选型 |

## 7. 延伸阅读

| 网站 | 内容 |
|---|---|
| [kaiaai/awesome-micro-ros-projects](https://github.com/kaiaai/awesome-micro-ros-projects) | micro-ROS 项目大全 |
| [linorobot/linorobot2_hardware](https://github.com/linorobot/linorobot2_hardware) | 另一个成熟的 micro-ROS 轮式机器人固件（想做轮式车可参考） |
| [2b-t/esp32s3-microros](https://github.com/2b-t/esp32s3-microros) | XIAO ESP32S3 Sense 摄像头 → ROS 2 的最小例子 |
