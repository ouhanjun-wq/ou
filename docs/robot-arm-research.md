# 6 自由度机械臂：开源项目调研与选型

目标：做一台 **Arduino Uno R3** 控制、**盖世小鸡 G7 Pro** 手柄操控的 6 自由度桌面机械臂（常见的黑色铝合金 6 舵机套件），能旋转、上下、左右、前后移动，能抓取物体、示教回放，后面还能接 **语音模块** 和 **AI 控制**。

## 1. 先说清楚“6 自由度”

市面上叫“6 自由度机械臂”的舵机套件，几乎都是 **6 个舵机 = 5 个关节 + 1 个夹爪**：

| 编号 | 关节 | 作用 |
|---|---|---|
| J1 | 底座旋转 | 左右转 |
| J2 | 大臂（肩） | 抬高 / 放低 |
| J3 | 小臂（肘） | 伸出 / 收回 |
| J4 | 手腕俯仰 | 夹爪抬头 / 低头 |
| J5 | 手腕旋转 | 夹爪转角度 |
| J6 | 夹爪 | 张开 / 闭合 |

夹爪能**上下、左右、前后**移动（3 个移动方向轴，6 个方向），还能俯仰、旋转，外加开合。严格来说，末端能独立控制的是 5 个量：位置 $x, y, z$，加上俯仰角 $\varphi$ 和旋转角 $\psi$；末端的“偏航”跟着底座转。**抓取、搬运、摆放**这些桌面任务完全够用。

## 2. 调研到的开源项目

| 项目 | 驱动方式 | 控制器 | 和本方案的关系 |
|---|---|---|---|
| [carter-howell/dual-robotic-arms](https://github.com/carter-howell/dual-robotic-arms) | 舵机（MG995 / MS90）+ PCA9685 | ESP32 + **Bluepad32** + Xbox 手柄 | **最接近**：同样是 ESP32 用 Bluepad32 读蓝牙手柄、PCA9685 驱动舵机。不过它是直接映射关节，没有逆运动学，也没有保护功能 |
| [mailecampbell/6DOF-Robotic-Arm-Inverse-Kinematics](https://github.com/mailecampbell/6DOF-Robotic-Arm-Inverse-Kinematics) | 6 舵机 | Arduino | 几何法逆运动学（肘上 / 肘下两组解）、工作空间检查、三次插值平滑。MIT 协议。本方案的逆运动学也用几何法 |
| [arduino-libraries/Braccio](https://github.com/arduino-libraries/Braccio) | 6 舵机（TinkerKit Braccio） | Arduino | 官方 6 舵机机械臂库，结构和本方案一样（5 关节 + 夹爪）。LGPL-2.1 |
| [cgxeiji/CGx-InverseK](https://github.com/cgxeiji/CGx-InverseK) | 舵机 | Arduino / Teensy | “3 连杆 + 旋转底座”的逆运动学库，在 Braccio 上测试过 |
| [BCN3D/BCN3D-Moveo](https://github.com/BCN3D/BCN3D-Moveo) | 步进电机 | Arduino Mega + RAMPS | 全 3D 打印的 5 轴教育机械臂，资料齐全。步进电机更准，但需要限位开关回零，成本和难度都更高 |
| [PAROL6](https://source-robotics.github.io/PAROL-docs/) | 步进电机 + 闭环 | 自研控制板 | 工业级桌面机械臂，精度高，适合做完这台以后进阶 |
| [peng-zhihui/Dummy-Robot](https://github.com/peng-zhihui/Dummy-Robot)（稚晖君） | 步进电机 + 自研闭环驱动 | STM32 | 约 1.5 万星的明星项目，全自研硬件，难度很高，适合参考结构和软件设计 |

## 3. 选型结论

**用舵机方案，主控用 Arduino Uno R3。** 原因：

1. **从零开始最容易成功**：舵机自带减速箱和位置闭环，不需要限位开关回零，也不需要电机驱动板。
2. **成本低**：铝合金套件（含 6 个 MG996R）约 180 元，其余电子件和电源约 220–330 元（Uno、电阻、排针、杜邦线用现成的学习套件）。步进电机方案至少翻倍。
3. **Uno 够用**：逆运动学、平滑轨迹、示教回放都能在 Uno 上跑，大约用掉 96% 的程序空间。
4. **手柄**：Uno 没有蓝牙，所以加一块 **USB Host Shield 2.0**，G7 Pro 用 USB 线连接（[USB Host Shield 2.0 库](https://github.com/felis/USB_Host_Shield_2.0) 的 XBOXUSB 驱动）。

Uno 版在参考项目的基础上实现了这些：

| 功能 | 做法 |
|---|---|
| 手柄两种操控模式 | **关节模式**，以及 **XYZ 模式**（夹爪上下、左右、前后走直线，逆运动学自动算各关节角度） |
| 动作平滑 | 每个关节限速度和加速度；自动动作用五次多项式轨迹，所有关节同时起步、同时到达 |
| 示教回放 | A 键记录路点，X 键回放或循环回放，路点存在 EEPROM，断电不丢 |
| 夹爪防堵转 | 松开 RT 后夹爪自动退 3% |
| 安全保护 | 软件限位、工作空间限位（不撞桌面、不碰底座）、硬件急停 + A3 检测 |
| 语音 / AI 接口 | 串口文字指令（`MOVE 220 0 40 -60`、`CLOSE`、`HOME` …），来自电脑脚本或语音模块 |

受 Uno 的 32 KB 程序空间 / 2 KB 内存限制，**没有**做的功能：手机网页（没有 Wi-Fi）、电流检测（过载保护、夹爪夹到东西自动停）。以后换成 Arduino Mega 2560 或 ESP32 可以加回来（ESP32 无线版见提交 `fe0b446`）。

下一步：[总计划与材料清单](build-plan.md) → [组装指南](assembly-guide.md) → [运动控制原理](motion-control.md)。
