# 固件与命令

固件在 [`firmware/hexapod_g7pro/`](../firmware/hexapod_g7pro)。用 Arduino IDE 打开 `hexapod_g7pro.ino` 上传，开发板选 **esp32_bluepad32 → ESP32 Dev Module**，步骤见 [新手计划第 1 步](beginner-plan.md#第-1-步电脑装-arduino-ide-和开发板包)。

## 文件结构

| 文件 | 作用 | 依赖 Arduino？ |
|---|---|---|
| `hexapod_g7pro.ino` | 主程序：Bluepad32 手柄、50 Hz 控制循环、串口命令 | 是 |
| `kinematics.h` | 3 自由度腿的逆运动学 / 正运动学、坐标变换 | 否 |
| `gait.h` | 步态引擎：速度和身体姿态进，18 个关节角出 | 否 |
| `teleop.h` | 手柄按键 → 机器人命令（模式、急停、速度档…） | 否 |
| `params.h` / `params.cpp` | 可调参数表，保存在 flash（Preferences） | 仅保存部分 |
| `servo_out.h` / `servo_out.cpp` | 舵机输出：PCA9685 或 GPIO（16 路 LEDC + 6 路 MCPWM），插口表、方向、中位 | 是 |

标“否”的文件是纯数学，能在电脑上编译测试：[`firmware/tests/test_core.cpp`](../firmware/tests/test_core.cpp)。

```bash
cd hexapod-kit/firmware/tests
g++ -std=c++17 -O1 -Wall -Wextra -Werror -I../hexapod_g7pro test_core.cpp \
    ../hexapod_g7pro/params.cpp ../hexapod_g7pro/servo_out.cpp -o test_core && ./test_core
```

测试内容：
- 逆运动学和正运动学来回算，误差要足够小。
- 三种步态 × 四种走法（前进、横走、转圈、斜走加转），每种都检查：
  - 脚每 20 ms 最多移动 12 mm，不会突然跳。
  - 所有脚都够得着，关节不超限位。
  - 着地的腿数够：三角 ≥ 3，涟漪 ≥ 4，波浪 ≥ 5。
  - 停下后每只脚都回到原位。
- 同一侧相邻的两条腿不会同时抬起。
- 手柄逻辑：上电、急停、换步态、断开后停下。
- 参数范围、舵机脉宽公式。

## 串口命令

串口监视器：115200 baud，换行（Newline）。输入 `help` 可以看到下面这张表。

| 命令 | 作用 |
|---|---|
| `help` | 命令列表 |
| `status` | 模式、步态、速度档、高度、手柄电量、找到的 PCA9685 |
| `stand` / `sit` / `off` | 站起 / 趴下 / 舵机放松 |
| `walk <vx> <vy> <wz> [秒]` | 用命令走路，例如 `walk 60 0 0 2`。单位：mm/s、mm/s、°/s，默认 2 秒 |
| `gait tripod\|ripple\|wave` | 换步态 |
| `pair` | 忘掉配对过的手柄，等待新手柄 |
| `stop` | 停止 `findall` 或 `walk` |
| `i2c` | 扫描 I²C 设备；`0x40`–`0x47` 是 PCA9685 |
| `find <gpio>` | 让一个 ESP32 引脚摆动 3 秒 |
| `findpca <地址> <通道>` | 让 PCA9685 的一个通道摆动 3 秒，例如 `findpca 0x40 3` |
| `findall` | 所有插口轮流摆动（有 PCA9685 就扫它的通道，否则扫 GPIO） |
| `map <n> gpio <引脚>` | $n$ 号舵机接在 ESP32 引脚上 |
| `map <n> pca <地址> <通道>` | $n$ 号舵机接在 PCA9685 通道上 |
| `map <n> none` | $n$ 号舵机不接 |
| `maps` | 显示插口表、方向、中位、当前脉宽 |
| `centerall` | 所有插口输出 1500 µs（不需要插口表），装舵机臂时用 |
| `center` | 已配好的 18 个舵机回到关节角 $0^\circ$（含 dir / trim） |
| `servo <n> <µs>` | 给 $n$ 号舵机直接发脉宽 |
| `joint <n> <度>` | 让 $n$ 号舵机转到某个关节角（含 dir / trim） |
| `dir <n> <1\|-1>` | 翻转舵机方向 |
| `trim <n> <µs>` | 中位偏移，范围 ±300 |
| `params` | 列出所有参数和允许范围 |
| `get <名字>` / `set <名字> <值>` | 读 / 改参数，马上生效 |
| `save` / `load` / `reset` | 保存到 flash / 从 flash 读回 / 恢复默认（`reset` 后要 `save` 才会写入） |

`servo`、`joint`、`center`、`centerall` 会先把机器人切到 OFF，避免和步态抢舵机。

## 参数表

改法：`set 名字 值`，确认没问题后 `save`。

| 参数 | 默认 | 单位 | 含义 |
|---|---|---|---|
| `coxa` / `femur` / `tibia` | 28 / 50 / 75 | mm | 腿长，**一定要实量** |
| `front_x` / `front_y` / `front_angle` | 60 / 40 / 45 | mm, mm, ° | 左前腿转轴位置和朝向（右边镜像） |
| `mid_y` | 55 | mm | 中腿转轴离中线的距离（朝向 ±90°） |
| `rear_x` / `rear_y` / `rear_angle` | 60 / 40 / 135 | mm, mm, ° | 左后腿转轴位置和朝向 |
| `stance` | 80 | mm | 站立时脚离基节转轴多远 |
| `height` | 60 | mm | 站立时机身离地高度 |
| `height_min` / `height_max` | 35 / 90 | mm | 十字键能调的高度范围 |
| `sit_height` / `sit_stance` | 15 / 95 | mm | 趴下姿势 |
| `gait` | 0 | — | 开机步态：0 三角，1 涟漪，2 波浪 |
| `cycle_s` | 0.8 | s | 三角步态走一个周期的时间（涟漪 ×1.5，波浪 ×2.25） |
| `step_h` | 30 | mm | 抬腿高度（开机值） |
| `step_min` / `step_max` | 10 / 50 | mm | 十字键能调的抬腿范围 |
| `stride_max` | 50 | mm | 一步最远走多少 |
| `accel` / `yaw_accel` | 250 / 180 | mm/s², °/s² | 加速度上限（越小越柔和） |
| `move_rate` / `tilt_rate` | 60 / 45 | mm/s, °/s | 高度、姿态变化的速度 |
| `max_vx` / `max_vy` / `max_wz` | 120 / 80 / 60 | mm/s, mm/s, °/s | 第 3 档满杆速度 |
| `tilt_max` / `twist_max` / `shift_max` | 12 / 15 / 20 | °, °, mm | 扭身模式的最大幅度 |
| `deadband` | 0.1 | — | 摇杆死区 |
| `us_per_deg` | 11.11 | µs/° | 舵机每度对应的脉宽（MG90S：2000 µs / 180°） |
| `coxa_lim` | 60 | ° | 基节最大转角 ± |
| `femur_min` / `femur_max` | −70 / 80 | ° | 大腿限位 |
| `tibia_min` / `tibia_max` | −80 / 70 | ° | 小腿限位 |
| `i2c_sda` / `i2c_scl` | 21 / 22 | — | PCA9685 板的 I²C 引脚（改完要 `save` 并重启） |

## 原理

### 1. 腿的逆运动学（已知脚尖位置，求 3 个关节角）

在一条腿自己的坐标系里：
- 原点在基节转轴上，$x$ 指向腿的正外方，$z$ 朝上。
- 脚尖位置记为 $(x, y, z)$。
- 腿长记为 $L_c$（coxa）、$L_f$（femur）、$L_t$（tibia）。

**基节**：俯视看，脚尖在哪个方向，基节就转到哪个方向。

$$\theta_c = \operatorname{atan2}(y,\ x)$$

**大腿和小腿**：在腿所在的竖直平面里，大腿轴到脚尖的水平距离 $r$ 和直线距离 $d$ 是

$$r = \sqrt{x^2 + y^2} - L_c, \qquad d = \sqrt{r^2 + z^2}$$

大腿、小腿和 $d$ 这条线组成一个三角形，用余弦定理：

$$\theta_f = \operatorname{atan2}(z,\ r) + \arccos\!\left(\frac{L_f^2 + d^2 - L_t^2}{2\,L_f\,d}\right)$$

$$\theta_t = \arccos\!\left(\frac{L_f^2 + L_t^2 - d^2}{2\,L_f\,L_t}\right) - 90^\circ$$

减去 $90^\circ$ 是为了让装配姿态正好是 $\theta_t = 0$：大腿水平、小腿竖直时，膝盖夹角是 $90^\circ$。

如果 $d$ 超出 $\big[\,|L_f - L_t|,\ L_f + L_t\,\big]$，腿就够不着。这时固件把脚尖拉到最近的够得着的位置，并在串口报警 `out of reach`。

### 2. 身体姿态（扭身模式）

脚在地上不动，身体平移 $\mathbf{t}$ 并旋转 $R = R_z(\psi)\,R_y(\theta)\,R_x(\phi)$（$\psi$ 扭腰、$\theta$ 俯仰、$\phi$ 侧倾）。从身体上看，脚尖的新位置是

$$\mathbf{p}' = R^{\mathsf{T}}\,(\mathbf{p} - \mathbf{t})$$

再转到每条腿的坐标系里，做上面的逆运动学。

### 3. 步态

每条腿的一个周期 $T$ 分成两段：
- **支撑相**：脚着地，占 $\beta T$。
- **摆动相**：脚抬起，往前挪，占 $(1 - \beta)\,T$。

$\beta$ 叫占空比。每条腿有自己的相位偏移，决定谁和谁一起抬。

| 步态 | $\beta$ | 周期 $T$ | 相位偏移（LF LM LR RF RM RR） |
|---|---|---|---|
| 三角 | $\tfrac{1}{2}$ | $T_0$ | $0,\ \tfrac12,\ 0,\ \tfrac12,\ 0,\ \tfrac12$ |
| 涟漪 | $\tfrac{2}{3}$ | $1.5\,T_0$ | $\tfrac13,\ \tfrac23,\ 0,\ \tfrac56,\ \tfrac16,\ \tfrac12$ |
| 波浪 | $\tfrac{5}{6}$ | $2.25\,T_0$ | $\tfrac26,\ \tfrac16,\ 0,\ \tfrac56,\ \tfrac46,\ \tfrac36$ |

其中 $T_0$ 是参数 `cycle_s`。

**步长**：机身速度为 $(v_x, v_y)$、转速为 $\omega$（弧度 / 秒）。第 $i$ 条腿的中立落脚点为 $\mathbf{n}_i = (n_x, n_y)$，它在一个支撑相里要相对机身后退的距离是

$$\mathbf{S}_i = \begin{pmatrix} v_x - \omega\, n_y \\[2pt] v_y + \omega\, n_x \end{pmatrix} \beta T$$

如果最长的一步 $\max_i \lVert \mathbf{S}_i \rVert$ 超过 `stride_max`，就把 $v_x, v_y, \omega$ 按同一比例缩小。所以步态越稳，最高速度越低。

**支撑相**：脚踩在地上不动，所以从机身上看，它朝反方向移动：

$$\dot{\mathbf{d}}_i = -\begin{pmatrix} v_x - \omega\, p_y \\ v_y + \omega\, p_x \end{pmatrix}$$

其中 $\mathbf{d}_i$ 是脚相对中立点的偏移，$\mathbf{p}$ 是脚的当前位置。脚的位置是一点点积分出来的，所以速度变化时脚不会跳。

**摆动相**：记 $w \in [0, 1]$ 为摆动进度，$\mathbf{d}_0$ 为抬脚时的偏移，$h$ 为抬腿高度 `step_h`。脚从 $\mathbf{d}_0$ 平滑地挪到中立点前方半步，同时按正弦曲线抬起再放下：

$$\mathbf{d}(w) = \mathbf{d}_0 + \left(\tfrac{1}{2}\mathbf{S} - \mathbf{d}_0\right) e(w), \qquad e(w) = 3w^2 - 2w^3$$

$$z(w) = h \sin(\pi w)$$

**起步和停步**：
- 起步时，抬腿高度在半个周期内从 0 渐渐加到 $h$，所以原本处在摆动相中间的腿不会猛地抬起。
- 松开摇杆后，速度先减到 0，然后还没回到中立点的腿各自再走一步落回原位，其他腿等着。全部回位后才算停下，此时切换步态才会生效。

### 4. 舵机脉宽

记 $s = \pm 1$ 为方向 `dir`，$\Delta$ 为中位偏移 `trim`（µs），$\theta$ 为关节角（已经过限位裁剪）。

$$t = 1500 + s \cdot \theta \cdot k + \Delta \quad [\mu\text{s}], \qquad k = \frac{2000\ \mu\text{s}}{180^\circ} \approx 11.1\ \mu\text{s}/^\circ$$

$t$ 最后限制在 $[500,\ 2500]$ µs。PCA9685 的 PWM 频率设为

$$f = \frac{25\ \text{MHz}}{4096 \times (121 + 1)} \approx 50.03\ \text{Hz}$$

GPIO 板用 ESP32 的 LEDC（16 路，16 位分辨率），多出来的舵机用 MCPWM。

## 安全设计

| 情况 | 固件怎么做 |
|---|---|
| 刚开机 | 舵机不上电（软的），按 A 或输入 `stand` 才上电 |
| 按 B | 速度立刻归零；两个摇杆都回中后才解除 |
| 手柄断开 | 当作所有按键都松开：机器人停下站稳 |
| 关节角超限 | 裁剪到 `coxa_lim`、`femur_min/max`、`tibia_min/max` |
| 速度太快 | 一步最远 `stride_max`；加速度限制在 `accel`、`yaw_accel` |
| 走路时换步态 | 等机器人停下再换 |
| 脚够不着 | 拉到最近的够得着的位置，串口报警 |
