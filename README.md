# 6 自由度机械臂（6-DOF Robot Arm）

基于 ESP32（Arduino）的 6 舵机桌面机械臂（5 个关节 + 夹爪）：盖世小鸡 G7 Pro 蓝牙手柄操控，支持**关节模式**和 **XYZ 直线模式**（逆运动学），动作自动平滑，能示教回放，夹爪夹到东西会自动停，带过载保护和硬件急停。还预留了**语音模块**和 **AI 文字指令**接口，手机网页可以看状态、发指令。

![系统总览](docs/img/fig0-overview.svg)

| 从这里开始 | 内容 |
|---|---|
| 📋 [`docs/build-plan.md`](docs/build-plan.md) | **总计划**：材料清单（名称 / 规格 / 型号 / 价格）、分阶段步骤、验收标准、故障排查、安全规则 |
| 🔌 [`docs/assembly-guide.md`](docs/assembly-guide.md) | **组装指南**：电源和信号接线图、焊接方法、机械装配、标定，每一步都有注意事项和检查点 |
| 🧮 [`docs/motion-control.md`](docs/motion-control.md) | 运动控制原理：正 / 逆运动学、平滑轨迹、夹爪检测、保护逻辑、舵机扭矩校核 |
| 🔎 [`docs/robot-arm-research.md`](docs/robot-arm-research.md) | GitHub 开源项目调研与选型 |
| 🔧 [`firmware/README.md`](firmware/README.md) | 固件：开发环境、引脚、手柄连接、文字指令、参数 |
| 🧩 [`cad/README.md`](cad/README.md) | 3D 打印件：电控托板、舵机线夹（OpenSCAD 参数化模型 + STL） |
| 🤖 [`tools/arm_client.py`](tools/arm_client.py) | 电脑 / AI 通过 Wi-Fi 或 USB 发文字指令 |

## 硬件一览

| 部分 | 型号 |
|---|---|
| 主控 | ESP32-WROOM-32E 开发板（Bluepad32 固件包） |
| 舵机驱动 | PCA9685（I²C 0x41） |
| 电流 / 电压检测 | INA226（0.01 Ω，I²C 0x40） |
| 舵机 | J1、J3：DS3218MG · J2：DS3225MG · J4、J5、J6：MG996R |
| 电源 | 12 V 5 A 适配器 → 20 A 降压模块 6.0 V（舵机）+ MP1584 5.0 V（主控）；急停 + 继电器 |
| 手柄 | 盖世小鸡 G7 Pro（蓝牙模式） |
| 结构 | 6 自由度铝合金机械臂支架套件 + 400 × 300 mm 底板 |

> 这个仓库之前是“仿生蝴蝶”项目。最后一个蝴蝶版本在提交 `380c94d`（`git checkout 380c94d` 可以看到全部蝴蝶资料）。
