# 网站汇总

所有用到的软件、驱动、资料，按用到的顺序排列。

## 软件和开发板包

| 名称 | 网址 | 用途 |
|---|---|---|
| Arduino IDE 2 | <https://www.arduino.cc/en/software> | 编译、上传固件，串口监视器 |
| Arduino-ESP32 开发板地址 | `https://espressif.github.io/arduino-esp32/package_esp32_index.json` | 粘到“其他开发板管理器地址”，提供烧录工具 |
| Bluepad32 开发板地址 | `https://raw.githubusercontent.com/ricardoquesada/esp32-arduino-lib-builder/master/bluepad32_files/package_esp32_bluepad32_index.json` | 粘到“其他开发板管理器地址”，安装 `esp32_bluepad32` |
| Bluepad32 项目 | <https://github.com/ricardoquesada/bluepad32> | 手柄库源码和支持的手柄列表 |
| Bluepad32 文档 | <https://bluepad32.readthedocs.io/> | Arduino 用法、配对说明 |
| Python 3 | <https://www.python.org/downloads/> | 运行 esptool |
| esptool 文档 | <https://docs.espressif.com/projects/esptool/en/latest/esp32/> | 备份 / 恢复原厂固件（`read_flash`、`write_flash`） |

## USB 驱动

| 芯片（看主板 USB 口旁边） | 驱动 |
|---|---|
| CH340 / CH9102（Windows） | <https://www.wch.cn/downloads/CH341SER_EXE.html> |
| CH340 / CH9102（macOS） | <https://www.wch.cn/downloads/CH34XSER_MAC_ZIP.html> |
| CP2102 / CP2104 | <https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers> |

## 本项目

| 名称 | 网址 |
|---|---|
| 仓库（下载 ZIP） | <https://github.com/ouhanjun-wq/ou/tree/claude/ros-esp32-s3-project-ypzx2b> |
| 固件源码 | <https://github.com/ouhanjun-wq/ou/tree/claude/ros-esp32-s3-project-ypzx2b/hexapod-kit/firmware/hexapod_g7pro> |

## 芯片和元件资料

| 名称 | 网址 | 看什么 |
|---|---|---|
| ESP32 技术规格书 | <https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_cn.pdf> | 引脚功能、哪些引脚只能输入 |
| ESP32 引脚参考（英文） | <https://randomnerdtutorials.com/esp32-pinout-reference-gpios/> | 哪些引脚能安全地当输出、哪些是启动引脚 |
| PCA9685 数据手册 | <https://www.nxp.com/docs/en/data-sheet/PCA9685.pdf> | 16 路 PWM 芯片的寄存器 |
| Arduino-ESP32 LEDC 文档 | <https://docs.espressif.com/projects/arduino-esp32/en/latest/api/ledc.html> | GPIO 板怎么产生舵机信号 |

## 以后可以玩（第 9 步）

| 名称 | 网址 |
|---|---|
| ESP32-CAM 入门（英文） | <https://randomnerdtutorials.com/esp32-cam-video-streaming-face-recognition-arduino-ide/> |
| 超声波 HC-SR04 + ESP32（英文） | <https://randomnerdtutorials.com/esp32-hc-sr04-ultrasonic-arduino/> |
| ROS 2 路线（本仓库） | [`hexapod/`](../../hexapod/README.md) |
