# 软件安装、烧录、逐项测试

> 电脑端和固件都用 **[SeekerRobot/seeker-robot](https://github.com/SeekerRobot/seeker-robot)** 原项目的代码，**不用自己写程序**。
> 整个开发环境（ROS 2 Jazzy + Gazebo + PlatformIO + micro-ROS Agent）都装在它提供的 **Docker 容器**里，电脑上只需要装 Git 和 Docker。
> 你只需要：填 Wi-Fi 配置、按这里的机身改 4 个参数、按顺序刷固件测试。
>
> 所有网址汇总在 [`links.md`](links.md)。

---

## 1. 电脑准备

| 系统 | 推荐度 | 说明 |
|---|---|---|
| **Ubuntu 24.04 LTS**（装在实体机或双系统） | ⭐⭐⭐ 推荐 | Docker 用 `host` 网络，micro-ROS 的 UDP、RViz 图形界面、USB 串口都直接能用 |
| Windows 10 / 11 + Docker Desktop（WSL2） | ⭐⭐ | 要多装 **VcXsrv**（显示 RViz / Gazebo）和 **usbipd-win**（把 USB 串口给容器）；网络用 `bridge` 模式 |
| macOS + Docker Desktop | ⭐ | 容器里**不能**用 USB 刷固件，只能在 macOS 上另装 PlatformIO 刷，或者用 OTA 无线刷 |

Ubuntu 24.04 下载：<https://ubuntu.com/download/desktop>（做启动 U 盘用 [balenaEtcher](https://etcher.balena.io/) 或 [Rufus](https://rufus.ie/)）。

> 💡 电脑和机器人要连**同一个 2.4 GHz Wi-Fi**。建议电脑用**网线**连路由器，Wi-Fi 只留给机器人，延迟更稳。

---

## 2. 安装 Git、Git LFS、Docker、VS Code

### 2.1 Ubuntu

```bash
# Git + Git LFS（Seeker 的 PCB 文件用 LFS 存）
sudo apt update
sudo apt install -y git git-lfs curl
git lfs install

# Docker Engine + Compose 插件（官方脚本；详细步骤见 https://docs.docker.com/engine/install/ubuntu/）
curl -fsSL https://get.docker.com | sudo sh
sudo usermod -aG docker $USER      # 让当前用户不用 sudo 就能用 docker
newgrp docker                      # 或者注销重新登录
docker run --rm hello-world        # 打印 Hello from Docker! 就对了

# 串口权限（刷 XIAO 用）
sudo usermod -aG dialout $USER

# VS Code（可选，看代码方便）
sudo snap install code --classic
```

### 2.2 Windows

1. 装 [Git for Windows](https://git-scm.com/download/win)（自带 Git LFS），打开 Git Bash 运行 `git lfs install`。
2. 装 [Docker Desktop](https://www.docker.com/products/docker-desktop/)，设置里勾选 **Use the WSL 2 based engine**。
3. 装 [VcXsrv](https://sourceforge.net/projects/vcxsrv/)，每次用之前运行 **XLaunch**，勾选 **Disable access control**。
4. 装 [usbipd-win](https://github.com/dorssel/usbipd-win/releases)（刷固件时把 XIAO 的 USB 转给容器，见 §7.1）。
5. 装 [VS Code](https://code.visualstudio.com/)（可选）。

---

## 3. 下载 Seeker 并填配置

```bash
git clone --recurse-submodules https://github.com/SeekerRobot/seeker-robot.git
cd seeker-robot
git lfs pull                                   # 可选：下载 PCB 文件（做原版 PCB 才需要）

cp docker/.env.example docker/.env
cp mcu_ws/platformio/network_config.example.ini mcu_ws/platformio/network_config.ini
```

> `--recurse-submodules` 一定要加：FastLED、micro-ROS、LDS 雷达驱动、BNO08x 驱动都是子模块。忘了加就补一句 `git submodule update --init --recursive`。

### 3.1 `docker/.env`

| 键 | Ubuntu 填 | Windows / macOS 填 |
|---|---|---|
| `COMPOSE_PROJECT_NAME` | `seeker-robot` | `seeker-robot` |
| `BUILD_TARGET` | `dev`（带 Gazebo、RViz） | `dev` |
| `DISPLAY_CONFIG` | `${DISPLAY}` | `host.docker.internal:0` |
| `NETWORK_MODE_CONFIG` | `host` | `bridge` |
| `FISH_API_KEY`、`GEMINI_API_KEY` | 留空（语音功能才要） | 留空 |

### 3.2 `mcu_ws/platformio/network_config.ini`（Wi-Fi 和 IP）

```ini
[network]
wifi_ssid     = 你的WiFi名            ; 必须是 2.4 GHz
wifi_password = 你的WiFi密码
agent_ip      = { 192, 168, 8, 134 }  ; ← 电脑的 IP（micro-ROS Agent 跑在电脑上）
agent_port    = 8888
static_ip     = { 192, 168, 8, 50 }   ; ← 机器人主板的固定 IP（必须填，不能用 DHCP）
gateway       = { 192, 168, 8, 1 }    ; ← 路由器 IP
subnet        = { 255, 255, 255, 0 }
ota_upload_port = 192.168.8.50        ; 和 static_ip 一样，无线刷固件用
satellite_ip  = { 192, 168, 8, 51 }   ; 摄像头卫星板（阶段 8）的固定 IP
satellite_ota_upload_port = 192.168.8.51
```

怎么填：

1. 电脑上 `ip addr`（Windows：`ipconfig`）看自己的 IP 和网段，比如 `192.168.1.23` → 网段是 `192.168.1.x`，路由器一般是 `192.168.1.1`。
2. `agent_ip` 填电脑 IP，**最好在路由器后台把电脑 IP 也绑定成固定的**。
3. `static_ip` / `satellite_ip` 选同网段里**没人用**的地址（比如 `.50`、`.51`），写成逗号分隔的格式 `{ 192, 168, 1, 50 }`。
4. ⚠️ 原项目的摄像头相关 launch 文件里**写死了** `http://192.168.8.51/cam`。如果你的网段不是 `192.168.8.x`，要么把路由器的局域网改成 `192.168.8.x`（最省事），要么一次性替换：

   ```bash
   grep -rl "192.168.8.51" ros2_ws/src | xargs sed -i 's/192\.168\.8\.51/192.168.1.51/g'   # 换成你的 satellite_ip
   ```

---

## 4. 按本仓库的机身改固件参数

原项目的腿长是占位数（文件里写着 `// MEASURE`）。用这个仓库 [`cad/`](../cad/README.md) 打印的机身，在 `seeker-robot` 目录里运行下面 4 行（已在原项目当前版本上验证过能匹配）：

```bash
F=mcu_ws/lib/RobotConfig/HexapodConfig.h
sed -i 's/constexpr float kL2 = 50.0f;/constexpr float kL2 = 65.0f;/' $F                 # 小腿 65 mm
sed -i 's/constexpr float kNeutralKnee = 45.0f;/constexpr float kNeutralKnee = 60.0f;/' $F  # 站立膝角 60°
sed -i 's/constexpr float kHipMin = -60.0f;/constexpr float kHipMin = -25.0f;/' $F       # 髋关节 ±25°，防止相邻腿打架
sed -i 's/constexpr float kHipMax = 60.0f;/constexpr float kHipMax = 25.0f;/' $F
git diff --stat                                                                           # 应该显示 HexapodConfig.h 改了 4 行
```

| 参数 | 原值 | 改成 | 为什么 |
|---|---|---|---|
| `kL1`（大腿） | 45 | **45（不用改）** | 本仓库大腿就是 45 mm |
| `kL2`（小腿） | 50 | **65** | 本仓库小腿 65 mm |
| `kBodyHalfLength` / `kBodyHalfWidth` | 60 / 40 | **不用改** | 底板上 6 个髋轴就是按这两个数画的 |
| `kNeutralKnee` | 45° | **60°** | 站高一点，膝舵机力矩小 30 %（见总计划 §3.2） |
| `kHipMin` / `kHipMax` | ±60° | **±25°** | 超过 ±25° 相邻两条腿会碰（总计划 §3.5）。ML / MR 两个舵机在 `kServoConfigs` 里单独写了 `max_angle = 30.0f`，也可以一并改成 25 |

**BNO085 地址**：固件用 `0x4B`（`RobotConfig.h` 的 `gyro_addr`）。你的模块如果是 `0x4A` 又不想改接线，就把 `ENV_ESP32S3SENSE` 那一段的 `gyro_addr = 0x4B` 改成 `0x4A`。

**仿真里的腿长**（可选，只影响 Gazebo 和 RViz 里的模型外观）：`ros2_ws/src/seeker_description/urdf/seeker_hexapod.urdf.xacro` 的 `femur_len` 改 `0.045`、`tibia_len` 改 `0.065`。

---

### 4.1 用 XIAO ESP32S3 Plus 做主板

原项目是按 XIAO ESP32S3 **Sense** 写的，Plus 可以直接用：

| 项目 | Sense | Plus | 影响 |
|---|---|---|---|
| D0–D10 引脚 | GPIO 1–6、43、44、7–9 | **完全一样** | 接线、`RobotConfig.h` 都不用改 |
| PSRAM | 8 MB（OPI） | 8 MB（OPI） | 固件的 `memory_type = qio_opi` 照用 |
| Flash | 8 MB | 16 MB | 用 `seeed_xiao_esp32s3` 板卡编译没问题（只用到前 8 MB） |
| 摄像头 / 麦克风 | 有 | **没有** | 摄像头交给卫星板（§8.4）；麦克风没有也不影响行走、建图、导航 |
| 板载 LED | GPIO21 | GPIO21 | `test_threaded_blink` 照用 |

所以**所有测试固件和主固件都照常用 `esp32s3sense*` 环境刷**。唯一的小优化（可选）：主固件 `esp32s3sense_offload` 默认会开麦克风服务，Plus 上没有麦克风，只会读到静音，不报错。想关掉省点内存，在 `mcu_ws/src/main/platformio.ini` **末尾**加一个环境：

```ini
; XIAO ESP32S3 Plus：和 esp32s3sense_offload 一样，只是关掉麦克风
[env:esp32s3plus_offload]
extends = env:esp32s3sense_offload
build_flags =
    ${env:esp32s3sense_offload.build_flags}
    -UENABLE_MIC
    -DENABLE_MIC=0
```

以后刷主固件就用 `pio run -e esp32s3plus_offload -t upload`（`-U` 先取消原来的 `ENABLE_MIC=1`，再定义成 0，和原项目 `platformio.ini` 里的写法一致）。

> ⚠️ Plus 一定要**插上棒状天线**再测 Wi-Fi。

## 5. 构建 Docker 容器和 ROS 2 工作空间

```bash
cd docker
docker compose build --no-cache init-bootstrap   # 第一次必须加 --no-cache
docker compose build                              # 第一次要 20–60 分钟，下载 ROS 2 Jazzy、Gazebo 等
docker compose up init-bootstrap                  # 只需运行一次：初始化数据卷
docker compose up -d ros2                         # 后台启动开发容器
docker compose exec ros2 bash                     # 进入容器（以后每开一个终端都执行这句）
```

进容器以后：

```bash
cd ~/ros2_workspaces
source /opt/ros/jazzy/setup.bash
colcon build
source install/setup.bash
echo 'source ~/ros2_workspaces/install/setup.bash' >> ~/.bashrc    # 以后新终端自动生效

# 自检
ros2 pkg list | grep seeker                                # 能看到 seeker_description、seeker_navigation 等
ros2 run micro_ros_agent micro_ros_agent --help            # micro-ROS Agent 装好了
pio --version                                              # PlatformIO 装好了
```

> 有 NVIDIA 显卡想让 Gazebo 更流畅：`docker compose --profile nvidia up -d ros2-nvidia`（需要先装 [NVIDIA Container Toolkit](https://docs.nvidia.com/datacenter/cloud-native/container-toolkit/latest/install-guide.html)）。

---

## 6. 先在仿真里跑通（不接硬件）

```bash
# 终端 1（容器里）
ros2 launch seeker_gazebo sim_teleop.launch.py

# 终端 2（再开一个终端：docker compose exec ros2 bash）
ros2 run teleop_twist_keyboard teleop_twist_keyboard
```

在终端 2 里按 `i`（前进）、`,`（后退）、`j` / `l`（原地转）、`J` / `L`（平移）、`k`（停）。Gazebo 里的六足跟着走，就说明电脑端全部装好了。

其他仿真：

| 命令 | 内容 |
|---|---|
| `ros2 launch seeker_gazebo sim_slam_ekf.launch.py` | 仿真建图（和真机流程一样） |
| `ros2 launch seeker_gazebo sim_object_seek.launch.py` + `ros2 run seeker_navigation find teddy_bear --feedback` | 仿真里找泰迪熊 |

---

## 7. 烧录和逐项测试

### 7.1 怎么刷固件

**所有命令都在容器里执行**，格式都一样：

```bash
cd ~/mcu_workspaces/seeker_mcu/src/<固件文件夹>
pio run -e <环境名> -t upload        # 编译 + 刷写（第一次编译 micro-ROS 要 5–10 分钟）
pio device monitor -b 921600         # 看串口输出，Ctrl+C 退出
```

- XIAO 用 USB-C **数据线**接电脑。刷不进去时：**按住 XIAO 上的 B（BOOT）键 → 插 USB → 松开**，再刷。
- **刷固件时关掉电池开关**（只用 USB 供电），舵机线可以先拔掉。
- Windows：PowerShell（管理员）里先 `usbipd list` 找到 XIAO 的 BUSID，然后 `usbipd bind --busid <BUSID>`、`usbipd attach --wsl --busid <BUSID>`，容器里就能看到 `/dev/ttyACM0`。
- 环境名：每个测试文件夹的默认环境都是 `esp32s3sense`（不写 `-e` 就用它）；原项目的上机手册里，IMU、雷达、舵机、步态这几项用的是 `esp32s3senseserial`，下表照抄。
- 刷过带 `ENABLE_ARDUINO_OTA=1` 的固件以后，可以用 `-e <环境名>_ota` **无线刷**（地址是 `ota_upload_port`）。

### 7.2 micro-ROS Agent（每次测 Wi-Fi 固件前先开）

```bash
# 单独开一个终端，一直开着
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888
# 机器人连上时会打印 create_session
```

### 7.3 测试顺序（前一步不过，不要往下）

| # | 固件文件夹 | 环境 | 怎么验证 | 期望结果 |
|---|---|---|---|---|
| 1 | `test_threaded_blink` | `esp32s3sense` | 看板子 | 板载灯闪烁 |
| 2 | `test_sub_wifi` | `esp32s3sense` | 串口 | `WiFi: CONNECTED \| SSID … \| IP 192.168.x.50 \| RSSI …`，RSSI 最好 > −70 dBm |
| 3 | `test_sub_heartbeat` | `esp32s3sense` | 另一终端 `ros2 topic echo /mcu/heartbeat` | 每秒一个递增的数 |
| 4 | `test_sub_gyro_nondma` | `esp32s3senseserial` | 串口 | BNO085 初始化成功；转动机器人，四元数跟着变 |
| 5 | `test_sub_lidar` | `esp32s3senseserial` | 串口，输入 `scan` / `info` | 每圈约 720 个点，约 6 Hz |
| 6 | `test_sub_battery` | `esp32s3sense` | 串口，输入 `read` | 见 §7.8 标定 |
| 7 | `test_sub_servo` | `esp32s3senseserial` | 串口 | 见 §7.7 标定 |
| 8 | `test_sub_gait` | `esp32s3senseserial` | 串口：`neutral` → `start` → `vel 0.03` → `status` → `halt` | 三角步态，六条腿都动，不打架 |
| 9 | `test_bridge_gait` | `esp32s3sense` | `ros2 run teleop_twist_keyboard teleop_twist_keyboard` | 键盘遥控真机走路 |

> 环境名如果报 `Unknown environment`，打开该文件夹的 `platformio.ini` 看 `[env:...]` 里写的是哪个（原项目偶尔会改名）。

### 7.4 找不到设备 / 地址时：I²C 扫描

`test_sub_gyro_nondma` 报 `BNO08x not detected` 时，先确认 I²C 上有哪些地址：正常应该看到 `0x3C`（OLED）、`0x40`（PCA9685）、`0x4B`（BNO085），PCA9685 可能还会多出一个 `0x70`（全体广播地址，正常）。可以用 Arduino IDE 自带的 **Wire → i2c_scanner** 例子（SDA = D4，SCL = D5）临时刷一下看。

### 7.5 雷达

`test_sub_lidar` 里 `freq 10` 可以把转速调到 10 Hz。没数据先查：雷达 TX 是否接到 **D7**、RX 是否接到 **D6**，雷达 5V 脚电压是否 ≥ 4.8 V。

### 7.6 IMU 方向

```bash
# 刷 test_bridge_all（esp32s3sense 环境）后：
ros2 run tf2_ros static_transform_publisher 0 0 0 0 0 0 odom base_link     # 终端 A
rviz2                                                                       # 终端 B：Fixed Frame 选 odom，Add → Imu → /mcu/imu
```

把机器人**机头抬起**，RViz 里的箭头也应该机头抬起；**左边抬起**，箭头也左边抬起。反了就是 BNO085 装反了（X 箭头要朝机头、芯片面朝上）。

### 7.7 舵机标定（最花时间的一步）

**目的**：让固件里的 “0°” 和 “90°” 对应到腿的真实位置。刷 `test_sub_servo`（`esp32s3senseserial` 环境），**打开电池开关**，串口里：

```
arm                      # 先解锁（OE 拉低）
attach 6                 # 挂上通道 6（FL 髋）
angle 6 90               # 转到中间
...                      # 用 angle / vel / invert 等命令找到合适的位置，help 看全部命令
disarm
```

对每个舵机记下两个点的 PWM 值（12 位，50 Hz 下 $\text{val} = \dfrac{t_{\text{pulse}}}{20000\ \mu\text{s}} \times 4096$，例如 1500 µs ≈ 307）：

| 关节 | 固件角度 | 腿应该在的位置 | 填到 |
|---|---|---|---|
| 髋 | `kHipMin`（−25°） | 大腿往后摆 25° | `min_pwm` |
| 髋 | `kHipMax`（+25°） | 大腿往前摆 25° | `max_pwm` |
| 膝 | 0° | 小腿**水平**朝外 | `min_pwm` |
| 膝 | 90° | 小腿**竖直**朝下 | `max_pwm` |

- 12 组数填到 `mcu_ws/lib/RobotConfig/HexapodConfig.h` 的 `kServoConfigs[12]` 里（每组有注释写着是哪条腿的哪个关节）。
- 角度增大时腿往**反方向**走，就把那一组的 `inverted` 改成相反值。
- 髋关节 0° 的位置：让**脚尖**（不是大腿）落在腿的安装方向上（见接线指南 §6.3 的提示）。
- 改完重新刷 `test_sub_gait`，`neutral` 以后六条腿应该对称、机身水平。

> ⚠️ `main` 固件会把舵机和步态参数**存到闪存（NVS）里**。改了 `HexapodConfig.h` 却没生效，就先清一次闪存：`pio run -e esp32s3sense_offload -t erase`，再重新刷。

### 7.8 电池电压标定

刷 `test_sub_battery`，串口输入 `read`，同时用万用表量电池电压，记两组 “ADC 原始值 ↔ 实际电压”：一组电池**快没电**时（约 11.1 V），一组**刚充满**时（约 12.6 V）。填到 `mcu_ws/src/main/src/main.cpp` 的：

```cpp
static constexpr Subsystem::BatteryCalibration kBattCalibration(
    /*raw_lo=*/2432, /*volt_lo=*/11.52f,     // ← 换成你量的低点
    /*raw_hi=*/2648, /*volt_hi=*/12.62f);    // ← 换成你量的高点
```

固件按这两点**线性换算**：

$$
V = V_{\text{lo}} + \frac{\text{raw} - \text{raw}_{\text{lo}}}{\text{raw}_{\text{hi}} - \text{raw}_{\text{lo}}}\,\big(V_{\text{hi}} - V_{\text{lo}}\big)
$$

低于 11.1 V 时，状态灯会**红色快闪**，提醒换电池。

---

## 8. 最终固件 + 建图 + 导航

### 8.1 刷主固件

```bash
cd ~/mcu_workspaces/seeker_mcu/src/main
pio run -e esp32s3sense_offload -t upload
```

`esp32s3sense_offload` 是原项目的默认配置：**步态 + 雷达 + IMU + 电池 + 麦克风 + 喇叭**都在这块 XIAO 上，**摄像头交给第二块板**（§8.4）。XIAO ESP32S3 Plus 正好用这个环境（或 §4.1 的 `esp32s3plus_offload`）；没有摄像头板时建图和导航不受影响。

启动时状态灯：彩虹 → 红色追逐（连 Wi-Fi）→ 黄色呼吸（等 micro-ROS）→ **青色慢呼吸（就绪）**。

```bash
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888            # 终端 1
ros2 topic hz /mcu/scan                                              # 终端 2：约 6 Hz
ros2 topic echo /mcu/battery_voltage                                 # 电池电压
ros2 topic pub /cmd_vel geometry_msgs/msg/Twist '{linear: {x: 0.05}}' --once   # 往前走一下
```

### 8.2 建图（SLAM Toolbox）

```bash
ros2 launch seeker_navigation real_slam_ekf.launch.py                 # 终端 2：EKF + SLAM
ros2 run teleop_twist_keyboard teleop_twist_keyboard                  # 终端 3：慢慢遥控绕房间一圈
rviz2                                                                 # 终端 4：Fixed Frame = map，Add → Map /map、LaserScan /mcu/scan、TF
```

建好以后保存地图（容器里，[nav2_map_server](https://docs.nav2.org/configuration/packages/configuring-map-server.html)）：

```bash
ros2 run nav2_map_server map_saver_cli -f ~/my_room
```

### 8.3 导航（Nav2）

```bash
ros2 launch seeker_navigation real_ball_search.launch.py      # EKF + SLAM + Nav2 + 自动巡逻（约 25 s 后开始动）
```

RViz 工具栏里用 **2D Goal Pose** 在地图上点一个目标，机器人会自己规划路径走过去。

### 8.4 可选：摄像头卫星板 + 找东西

1. 一块 XIAO ESP32S3 Sense 插电脑（Plus 没有摄像头，不能当卫星板）：

   ```bash
   cd ~/mcu_workspaces/seeker_mcu/src/main_satellite
   pio run -e esp32s3sense_satellite -t upload      # 用 ESP32-CAM 的话：pio run -e esp32cam_satellite -t upload
   ```

2. 装到甲板前沿的竖板上，5V、GND 从 5V_SV 取电。浏览器打开 `http://<satellite_ip>/cam` 能看到画面。
3. 找东西：

   ```bash
   ros2 launch seeker_navigation real_object_seek.launch.py
   ros2 run seeker_navigation find teddy_bear --feedback        # 任何 COCO 类别：sports_ball、chair、cup ……
   ros2 service call /wander std_srvs/srv/Trigger               # 回到自由巡逻
   ```

---

## 9. 常见问题

| 现象 | 解决 |
|---|---|
| `docker compose build` 很慢 / 失败 | 网络问题：给 Docker 配镜像加速或代理；失败后重跑会接着下载 |
| RViz / Gazebo 窗口打不开（`cannot open display`） | Ubuntu：宿主机执行 `xhost +local:docker`；Windows：确认 XLaunch 开着且勾了 Disable access control |
| 容器里没有 `/dev/ttyACM0` | Ubuntu：重新插 USB 后 `docker compose restart ros2`；Windows：重新 `usbipd attach` |
| 编译报 `network_config.ini` 找不到 | §3 的 `cp` 没做 |
| micro-ROS 连不上（一直黄色呼吸） | Agent 没开；`agent_ip` 不是电脑 IP；Ubuntu 防火墙 `sudo ufw allow 8888/udp`；Windows 用 `bridge` 模式时要给容器映射 UDP 8888 或改用 Ubuntu |
| 状态灯品红色追逐 | 连续重启 5 次进入了**安全模式**（只开 Wi-Fi 和 OTA），看串口的原因，修好后重新刷 |
| 走路时一侧的腿往后划 | 那一侧髋舵机的 `inverted` 反了 |
| `/mcu/scan` 有数据但地图乱飘 | 走得太快：`teleop` 里按 `z` 把速度降到 0.05 m/s 以下；确认用的是 `real_slam_ekf` |

更多排查见原项目 Wiki：[IRL-Tests](https://github.com/SeekerRobot/seeker-robot/blob/main/docs/wiki/IRL-Tests.md)、[MCU-Sketches](https://github.com/SeekerRobot/seeker-robot/blob/main/docs/wiki/MCU-Sketches.md)、[Setup](https://github.com/SeekerRobot/seeker-robot/blob/main/docs/wiki/Setup.md)。
