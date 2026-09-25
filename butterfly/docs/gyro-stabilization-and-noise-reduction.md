# 仿生蝴蝶：陀螺仪增稳 + 降噪设计方案

> 基于上一份调研的推荐路线：Mech-Butterfly 机身 + 双舵机扑翼 + ESP32-S3（Arduino 框架）。
> 这里的“降噪”包括三层：**① 传感器 / 振动降噪（核心）**，**② 电源 / 电气降噪**，**③ 语音降噪（为后续语音模块预留）**。

---

## 0. 先搞清楚难点：扑翼机的“噪声”和多旋翼不一样

多旋翼的振动来自电机，频率在几百 Hz，离控制带宽（几 Hz 到几十 Hz）很远，一个低通滤波器就能滤掉。
蝴蝶扑翼机不一样：

| 噪声源 | 频率 | 特点 | 处理方式 |
|---|---|---|---|
| 扑翼引起的机体周期晃动 | $f = 3 \sim 5\ \text{Hz}$ 及其谐波 $2f,\ 3f,\dots$ | **落在控制带宽内**，而且是真实的机体运动，不是测量误差 | 陷波（Notch）/ 整周期平均，**不去跟随它** |
| 扑翼的线加速度 | 同上 | 严重污染加速度计，“重力方向”不可信 | 加速度计门控 + 低信任度融合 |
| 舵机齿轮 / 电机振动 | 几十 Hz 到几百 Hz | 高频、宽带 | 软减震 + IMU 内部抗混叠滤波 + 低通 |
| 舵机电流冲击 | 与扑翼同步 | 电压跌落，导致 IMU 读数跳变、MCU 复位 | 电源隔离 + 大电容 |

**核心设计思想：**

$$
\boxed{\text{控制的目标是“一个扑翼周期内的平均姿态”，而不是每一瞬间的姿态}}
$$

真蝴蝶飞行时身体本来就在上下起伏。增稳系统要是去对抗每一次扑翼带来的晃动，会和扑翼本身“打架”，导致舵机饱和、振荡加剧。

---

## 1. 系统总体架构

```
            ┌───────────────────── ESP32-S3 ─────────────────────┐
 ICM-42688  │ Core 1 (实时)                                      │
  (SPI) ───►│  IMU 1 kHz ─► 滤波链 ─► 姿态估计 ─► 级联 PID ─►      │
            │              (§3)       (§4)        (§5)    │      │
            │                                          ▼         │
            │                 波形发生器 θ_L(t), θ_R(t) ─► 混控 ─┼─► 舵机 L/R
            │                                          ▲         │
            │ Core 0 (通信)                            │         │
 ESP-NOW ──►│  指令解析 ─► 模式 / 设定值 ──────────────┘         │
 / BLE      │  遥测 / 日志 ◄── 滤波前后的数据、FFT 调试           │
            └────────────────────────────────────────────────────┘
```

| 任务 | 频率 | 核心 |
|---|---|---|
| IMU 读取 + 滤波 | $1\ \text{kHz}$ | Core 1 |
| 姿态估计 | $500\ \text{Hz} \sim 1\ \text{kHz}$ | Core 1 |
| 控制（PID + 混控 + 波形） | $200\ \text{Hz}$ | Core 1 |
| 舵机 PWM 刷新 | 数字舵机 $200 \sim 333\ \text{Hz}$，模拟舵机 $50\ \text{Hz}$ | LEDC 硬件 |
| 通信 / 遥测 | $20 \sim 50\ \text{Hz}$ | Core 0 |

---

## 2. 硬件选型与安装

### 2.1 IMU 选型

| 型号 | 陀螺噪声密度 | 接口 | 片内滤波 | 评价 |
|---|---|---|---|---|
| MPU-6050 | 约 $5\ \text{mdps}/\sqrt{\text{Hz}}$ | I²C（400 kHz） | 仅 DLPF | 便宜、资料多；已停产，市面上有不少仿品 |
| **ICM-42688-P** ✅ | 约 $2.8\ \text{mdps}/\sqrt{\text{Hz}}$ | **SPI**（最高 24 MHz） | 可编程抗混叠滤波（AAF）+ 陷波 | 低噪声，现代 FPV 飞控主流，**推荐** |
| BMI088 | — | SPI | — | 抗振性能强，PX4 常用；体积和价格略高 |

> 选 SPI 是为了读得又快又稳：舵机大电流造成的地线噪声，比较容易让 I²C 总线出错卡死。

### 2.2 机械减震（机械降噪）

IMU 板和机身之间用**软材料**连接，相当于一个机械低通滤波器。它的固有频率为：

$$
f_n = \frac{1}{2\pi}\sqrt{\frac{k}{m}}
$$

- 目标：$f_n$ 要**远低于**舵机的齿轮振动频率，才能把高频振动隔掉；同时要**远高于**扑翼频率 $f$，这样才能如实传递真实姿态。经验上取 $f_n \approx 30 \sim 60\ \text{Hz}$。
- 实现：3M VHB 双面泡棉胶，或者小块硅胶减震垫。**不要用硬螺丝直接锁在碳杆上。**
- IMU 尽量装在**机身重心附近**，远离舵机，减少离心和切向加速度的干扰：$a = \dot\omega \times r + \omega \times (\omega \times r)$，离重心越远（$r$ 越大），误差越大。
- 轴向对齐：IMU 的 $X$ 轴指向机头。安装角度偏差可以在软件里用校准矩阵补偿。

### 2.3 电源 / 电气降噪

舵机每拍一次都会产生一个电流尖峰。电池内阻 $R_{\text{int}}$ 上的压降为 $\Delta V = I_{\text{peak}} \cdot R_{\text{int}}$，这会让 3.3 V 电源轨抖动，进而影响 IMU 和 ADC 的读数。

```
LiPo ──┬──────────────────────────► 舵机 L/R （独立走线，粗线）
       │   ▲
       │  470 µF 低 ESR 电解电容（贴近舵机插针）
       │
       └─► 肖特基二极管 ─► 磁珠 ─► LDO 3.3 V ─┬─► ESP32-S3
                                    │         └─► 10 µF + 100 nF ─► IMU（就近去耦）
                                  (二极管 + 电容防止舵机拉低电压时 MCU 掉电复位)
```

- **星形接地**：舵机的地回路和 IMU / MCU 的地只在电池负极汇合一点。
- 舵机信号线和电源线**双绞**，减少辐射干扰。
- 2S 电池搭配高压舵机，可以直接由电池给舵机供电；主控经 LDO 或 Buck 降压。
- 软件侧：在 ESP32 上启用 brownout 检测，并记录复位原因，方便排查是不是电源问题。

---

## 3. 数字滤波链（传感器降噪核心）

```
原始陀螺 ─► ① 片内 AAF ─► ② PT1 低通 ─► ③ 扑翼同步陷波 ×2 ─► 给速率环（快）
                                                   │
                                                   └─► ④ 整周期平均 ─► 给角度环 / 航向（慢、干净）

原始加速度 ─► ① 片内 AAF ─► ② 低通 ─► ⑤ 可信度门控 ─► 姿态融合
```

### ① 片内抗混叠滤波（IMU 内部）

ICM-42688-P 设置：$\text{ODR} = 1\ \text{kHz}$，AAF 带宽约 $200 \sim 250\ \text{Hz}$。
这样能保证采样前就把高频成分滤掉，满足 $f_{\text{signal}} < f_s / 2$，避免高频振动混叠成低频“假姿态”。

### ② PT1 一阶低通（去除舵机的高频噪声）

$$
y_k = y_{k-1} + \beta\,(x_k - y_{k-1}),\qquad
\beta = \frac{\Delta t}{\tau + \Delta t},\qquad
\tau = \frac{1}{2\pi f_c}
$$

陀螺仪取 $f_c \approx 40 \sim 60\ \text{Hz}$，加速度计取 $f_c \approx 10 \sim 20\ \text{Hz}$。

### ③ 扑翼同步陷波（Flap-synchronous Notch）⭐

扑翼频率 $f$ 是**我们自己发给舵机的指令**，所以精确已知。这和 Betaflight 的 RPM Filter 思路一样：陷波中心频率**实时跟随** $f$，分别放在 $f$ 和 $2f$ 两处。

二阶陷波器（RBJ Biquad）：

$$
H(z) = \frac{b_0 + b_1 z^{-1} + b_2 z^{-2}}{1 + a_1 z^{-1} + a_2 z^{-2}}
$$

$$
\omega_0 = 2\pi \frac{f_0}{f_s},\quad
\alpha = \frac{\sin\omega_0}{2Q},\quad
b_0 = b_2 = \frac{1}{1+\alpha},\quad
b_1 = a_1 = \frac{-2\cos\omega_0}{1+\alpha},\quad
a_2 = \frac{1-\alpha}{1+\alpha}
$$

- $f_0 \in \{f,\ 2f\}$，$Q \approx 2 \sim 4$。$Q$ 越大，陷波越窄、延迟越小，但对频率误差越敏感。
- 优点：延迟很小，适合**速率环**。

### ④ 整周期滑动平均（Stroke Averaging）⭐

取窗口长度正好等于一个扑翼周期，$N = \operatorname{round}\!\left(f_s / f\right)$：

$$
\bar{x}_k = \frac{1}{N}\sum_{i=0}^{N-1} x_{k-i}
$$

它的频率响应是

$$
\left|H(e^{j\omega})\right| = \left|\frac{\sin(N\omega/2)}{N\sin(\omega/2)}\right|
$$

零点落在 $\omega = \dfrac{2\pi k}{N}$ 处，也就是 $f,\ 2f,\ 3f,\dots$。**所有扑翼谐波一次性清零**，本质是一个梳状滤波器（Comb Filter）。

- 代价是群延迟 $\approx \dfrac{N-1}{2}$ 个采样点，约 $T/2$。以 $f = 4\ \text{Hz}$ 为例，延迟约 $125\ \text{ms}$。
- 所以它**只用于慢速的角度环和航向保持**，不用于速率环。
- 实现上用环形缓冲区加累加和，每步只需 $O(1)$ 计算。

### ⑤ 加速度计可信度门控

扑翼时加速度计测到的是 $\vec{a}_{\text{meas}} = \vec{g} + \vec{a}_{\text{flap}}$，不能直接当重力方向用。
只有当测量值的模长接近 $1\,g$ 时，才信任它来修正姿态：

$$
w_{\text{acc}} =
\begin{cases}
1, & \big|\,\|\vec a\| - g\,\big| < 0.1\,g \\[4pt]
0, & \big|\,\|\vec a\| - g\,\big| > 0.3\,g \\[4pt]
\text{线性过渡}, & \text{其他}
\end{cases}
$$

再配合 ④ 的整周期平均：$\vec{a}_{\text{flap}}$ 在一个周期内的平均值近似为 $0$，所以平均后的加速度更接近真实重力方向。

---

## 4. 姿态估计

推荐 **Mahony 互补滤波**。它比 EKF 更省算力、更容易调；和 Madgwick 相比，可以直接对加速度修正项加权。

$$
\vec{e} = \hat{v}_{\text{acc}} \times \hat{v}_{\text{est}},\qquad
\vec{\omega}_{\text{corr}} = \vec{\omega}_{\text{gyro}} + K_p\, w_{\text{acc}}\,\vec{e} + K_i \!\int w_{\text{acc}}\,\vec{e}\,dt
$$

$$
\dot{q} = \tfrac{1}{2}\, q \otimes \begin{bmatrix} 0 \\ \vec{\omega}_{\text{corr}} \end{bmatrix}
$$

- $K_p$ 取得较小（约 $0.5 \sim 1$），因为加速度计噪声大。
- $K_i$ 用来估计陀螺零偏（bias），积分项要限幅。
- **偏航（Yaw）只靠陀螺积分**。磁力计会被舵机电流严重干扰，不建议使用。蝴蝶续航只有 3–5 分钟，陀螺漂移在这段时间内可以接受。
- 上电时静止 2 秒，自动做陀螺零偏校准：$b_\omega = \frac{1}{M}\sum \omega_i$。

互补滤波的简化形式（单轴，便于理解）：

$$
\hat{\phi}_k = \alpha\,\big(\hat{\phi}_{k-1} + \omega_k\,\Delta t\big) + (1-\alpha)\,\phi_{\text{acc},k},\qquad
\alpha = \frac{\tau}{\tau + \Delta t}
$$

---

## 5. 增稳控制器

### 5.1 控制量映射（和上一份文档的波形公式衔接）

$$
\theta_L(t) = \theta_0 + (\delta_p + u_p) + (\delta_r + u_r) + (A + u_y)\, w(2\pi f t)
$$

$$
\theta_R(t) = \theta_0 + (\delta_p + u_p) - (\delta_r + u_r) + (A - u_y)\, w(2\pi f t)
$$

| 轴 | 控制输出 | 作用方式 |
|---|---|---|
| 横滚 Roll | $u_r$ | 两翼拍动中心反向偏置 |
| 俯仰 Pitch | $u_p$ | 两翼拍动中心同向前后偏置 |
| 偏航 Yaw | $u_y$ | 左右幅值差动：$A_L - A_R = 2u_y$ |

### 5.2 级联 PID（外环角度，内环角速度）

$$
\omega_{\text{sp}} = K_{P,\text{ang}}\,\big(\phi_{\text{sp}} - \bar{\phi}\big)
\qquad\text{（外环：用整周期平均后的角度 } \bar\phi\text{）}
$$

$$
u = K_P\, e_\omega + K_I \!\int e_\omega\, dt - K_D\, \frac{d\,\omega_{\text{filt}}}{dt},
\qquad e_\omega = \omega_{\text{sp}} - \omega_{\text{filt}}
\qquad\text{（内环：用陷波后的角速度）}
$$

- **D 项对测量值求导**，不对误差求导，避免设定值突变时输出跳变；D 项再加一级 PT1 低通。
- **积分抗饱和**：只在“已起飞”时才积分（油门大于阈值）；积分量限幅；输出饱和时暂停积分。
- **输出限幅与优先级**：舵机行程有限，必须保证 $|\theta| \le \theta_{\max}$。**优先保证升力（幅值 $A$）**，其次保证横滚，最后才是偏航。
- **变化率限制**：每个控制周期内 $u$ 的变化量要限幅，避免舵机抖动和电流冲击。

### 5.3 飞行模式

| 模式 | 摇杆 / 指令的含义 | 用途 |
|---|---|---|
| `MANUAL` | 直接对应 $\delta_r,\ \delta_p,\ \Delta A$（和原项目一样） | 调试、IMU 故障时的兜底 |
| `STABILIZE` | 摇杆 = **目标角度**，松杆自动回平 | 日常飞行 ✅ |
| `HEADING_HOLD` | 在 STABILIZE 基础上，松杆时锁定航向 | 直线飞行、语音 / AI 控制 |

**和语音 / AI 的衔接**：有了增稳之后，`TURN_L 30` 这类指令会变成“偏航角设定值 $+30^\circ$”，而不是生硬的舵量。这样语音和 AI 控制都**更安全、更可预测**。

**失效保护（Failsafe）**：
- IMU 连续读错，或数据超出范围 → 切回 `MANUAL` 并降低扑翼频率，让它滑翔降落。
- 超过 $0.5\ \text{s}$ 收不到遥控 / 语音指令 → 回平，再缓慢降落。
- 姿态角超过 $60^\circ$ → 暂停积分并限制输出，防止越修越偏。

---

## 6. 参考代码（Arduino / ESP32-S3）

```cpp
// ---------- ② PT1 low-pass ----------
struct PT1 {
  float y = 0, beta = 1;
  void setCutoff(float fc, float fs) {
    float dt = 1.0f / fs, tau = 1.0f / (2.0f * PI * fc);
    beta = dt / (tau + dt);
  }
  float apply(float x) { return y += beta * (x - y); }
};

// ---------- ③ Biquad notch, center follows flap frequency ----------
struct Notch {
  float b0, b1, b2, a1, a2, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  void set(float f0, float fs, float Q) {
    float w0 = 2.0f * PI * f0 / fs, alpha = sinf(w0) / (2.0f * Q);
    float n = 1.0f / (1.0f + alpha);
    b0 = b2 = n;  b1 = a1 = -2.0f * cosf(w0) * n;  a2 = (1.0f - alpha) * n;
  }
  float apply(float x) {
    float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1; x1 = x; y2 = y1; y1 = y;
    return y;
  }
};

// ---------- ④ Stroke-synchronous moving average (comb filter) ----------
template <int MAXN>
struct StrokeAvg {
  float buf[MAXN] = {0}, sum = 0; int n = 1, i = 0;
  void setWindow(float fs, float fFlap) {
    int newN = constrain((int)lroundf(fs / fFlap), 1, MAXN);
    if (newN != n) { n = newN; i = 0; sum = 0; memset(buf, 0, sizeof(buf)); }
  }
  float apply(float x) {
    sum += x - buf[i]; buf[i] = x; i = (i + 1) % n;
    return sum / n;
  }
};

// ---------- ⑤ Accelerometer trust weight ----------
float accTrust(float ax, float ay, float az) {   // units: g
  float err = fabsf(sqrtf(ax*ax + ay*ay + az*az) - 1.0f);
  if (err < 0.1f) return 1.0f;
  if (err > 0.3f) return 0.0f;
  return (0.3f - err) / 0.2f;
}

// ---------- Rate PID with D-on-measurement and anti-windup ----------
struct RatePID {
  float kp, ki, kd, iLim, outLim, integ = 0, prevMeas = 0;
  PT1 dFilt;
  float update(float sp, float meas, float dt, bool airborne) {
    float e = sp - meas;
    float d = dFilt.apply(-(meas - prevMeas) / dt);  prevMeas = meas;
    float out = kp * e + integ + kd * d;
    bool saturated = fabsf(out) > outLim;
    if (airborne && !saturated) integ = constrain(integ + ki * e * dt, -iLim, iLim);
    return constrain(out, -outLim, outLim);
  }
};

// ---------- Control task on Core 1 (sketch) ----------
void controlTask(void*) {
  const float FS = 1000.0f;
  TickType_t last = xTaskGetTickCount();
  for (;;) {
    readImuSpi(raw);                                   // ICM-42688-P, 1 kHz
    for (int ax = 0; ax < 3; ++ax) {
      float g = lpfGyro[ax].apply(raw.gyro[ax] - gyroBias[ax]);
      g = notch1[ax].apply(g);                         // at f
      g = notch2[ax].apply(g);                         // at 2f
      gyroF[ax] = g;                                   // -> rate loop
    }
    float w = accTrust(accF[0], accF[1], accF[2]);
    mahonyUpdate(gyroF, accF, w, 1.0f / FS);           // -> roll, pitch, yaw
    rollAvg = rollStroke.apply(roll);                  // -> angle loop
    pitchAvg = pitchStroke.apply(pitch);

    if (++ctrlDiv >= 5) {                              // 200 Hz control
      ctrlDiv = 0;
      runCascadedPid();                                // u_r, u_p, u_y
      if (flapFreqChanged()) retuneNotchAndWindow(FS, flapFreq);
      writeServos(mix(phase, u_r, u_p, u_y));          // θ_L, θ_R
    }
    vTaskDelayUntil(&last, pdMS_TO_TICKS(1));
  }
}
```

---

## 7. 调试流程（循序渐进，避免炸机）

1. **静态检查**：上电静止，看陀螺零偏是否稳定；手动倾斜机体，确认 roll / pitch / yaw 的**符号方向**正确。
2. **台架扑翼 + 频谱分析**：把机体固定住，开始扑翼。通过 ESP-NOW / WiFi 把**原始**陀螺数据和**滤波后**的陀螺数据都发到电脑，做 FFT。
   - 应该能看到 $f,\ 2f$ 处有尖峰，另外还有舵机噪声形成的宽带高频成分。
   - 对比滤波前后的频谱，确认尖峰被压下去了。
3. **舵机响应方向检查**：手持机体向左倾斜，STABILIZE 模式下舵机应该做出**把机体拉回水平**的反应。方向反了就改符号，**这一步必须确认后才能试飞**。
4. **系留试飞**：用细线拴住机体，先只开 roll 轴的 P 项，其他为 0。逐步加大 $K_P$，直到出现轻微振荡，再退回约 $60\%$。
5. **依次加 pitch → yaw → I 项 → D 项**，每次只改一个参数。
6. **自由飞行**：先在 MANUAL 模式下起飞，高度稳定后再切到 STABILIZE，对比两种模式的飞行表现。
7. **记录日志**：保存设定值、测量值、$u$、舵机角度、电池电压，用于复盘分析。

初始参数建议（与 `firmware/butterfly_fc/params.h` 的默认值一致，需要实测调整）：

| 参数 | 单位 | Roll | Pitch | Yaw |
|---|---|---|---|---|
| $K_{P,\text{ang}}$（`ang_p_*` / `head_p`） | $(^\circ/\text{s}) / ^\circ$ | 3.0 | 3.0 | 2.0（航向） |
| $K_P$（`rate_p_*`） | $^\circ / (^\circ/\text{s})$ | 0.08 | 0.08 | 0.06 |
| $K_I$（`rate_i_*`） | $^\circ / ^\circ$ | 0.05 | 0.05 | 0.03 |
| $K_D$（`rate_d_*`） | $^\circ / (^\circ/\text{s}^2)$ | 0（先不开） | 0 | 0 |

> 速率环输出 $u$ 的单位是“翅膀偏置角度”（°）。举例：横滚角速度误差为 $100\ ^\circ/\text{s}$ 时，$K_P = 0.08$ 会产生 $8^\circ$ 的扑动中心反向偏置。

---

## 8. 语音降噪（为后续语音模块预留）

| 方案 | 做法 | 效果 |
|---|---|---|
| **地面端识别（推荐）** | 麦克风在遥控器或地面端，离舵机噪声源远 | 物理上就规避了舵机噪声，最有效 |
| ESP-SR AFE | 乐鑫音频前端，内含 NS（噪声抑制）、VAD（语音活动检测），双麦克风时还有 BSS（盲源分离）和 AEC（回声消除） | 在 ESP32-S3 上可以直接用 |
| 双麦克风阵列 | 两个 INMP441，间距约 $4 \sim 6\ \text{cm}$，做波束成形 | 提升信噪比（SNR） |
| 按键说话（Push-to-talk） | 按住按键才开始识别 | 从根本上避免误触发 |
| 置信度阈值 + 二次确认 | 识别置信度低于阈值时不执行；`TAKEOFF` 这类关键指令需要确认 | 提高安全性 |

**不建议把麦克风装在蝴蝶身上**：舵机噪声（以 $f$ 为基频的谐波加齿轮噪声）离麦克风太近，信噪比极差，而且会占用本来就很紧张的载荷。

---

## 9. 物料增量与重量预算

| 新增部件 | 大致重量 | 必要性 |
|---|---|---|
| ICM-42688-P 小模块 | 约 1–2 g | 必须 |
| 减震泡棉 / 硅胶 | < 0.5 g | 必须 |
| 470 µF 电容 + 磁珠 + 二极管 | 约 1 g | 强烈建议 |
| 合计 | 约 2–3.5 g | 在 $\Delta m \lesssim 7\ \text{g}$ 的预算内 ✅ |

---

## 10. 参考资料

- Betaflight RPM Filter / Dynamic Notch 的设计思路：<https://github.com/betaflight/betaflight>
- OrniFlight（扑翼专用 Betaflight，陀螺仪调制扑翼波形）：<https://github.com/dantiel/OrniFlight>
- RCmags/ornithopter-hover（Arduino + MPU-6050 扑翼闭环）：<https://github.com/RCmags/ornithopter-hover>
- 蝴蝶扑翼机在 ESP32 上的机载闭环控制（SNN）：<https://arxiv.org/html/2605.19430>
- Mahony, R. et al., *Nonlinear Complementary Filters on the Special Orthogonal Group*, IEEE TAC, 2008
- RBJ Audio EQ Cookbook（Biquad 陷波公式）
- 乐鑫 ESP-SR（AFE：NS / VAD / BSS）：<https://github.com/espressif/esp-sr>
