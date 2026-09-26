# 新手计划：从 0 开始到用手机遥控六足机器人

> **写给谁**：没做过机器人、没用过 Linux / ROS 也没关系。会用电脑、会用手机、愿意照着一步步做就行。
> **最后的样子**：手机连上家里 Wi-Fi，浏览器打开一个网页，手指推屏幕上的摇杆，六足机器人**前进、后退、原地转圈**，连续走 3 米不摔倒。
> **怎么用这份计划**：从上往下做，**每做完一步就勾一下**（网页版能勾选，进度会显示在左边目录下面）。每一步都写了 “✅ 成功的样子”，**没看到就先别往下做**，按 “❌ 卡住了” 去查。

---

## 0. 开始之前：先看懂要做什么

### 0.1 这台机器人由哪几部分组成

| 部分 | 是什么 | 大白话 |
|---|---|---|
| 身体 | 3D 打印的底板、甲板、6 条腿 | 骨架 |
| 12 个舵机 | MG90S，每条腿 2 个 | 肌肉：一个管腿左右摆，一个管腿上下抬 |
| PCA9685 | 舵机驱动板 | 帮主控同时指挥 12 个舵机 |
| XIAO ESP32S3 Plus | 主控板（你已经有了） | 小脑：算步态、连 Wi-Fi |
| BNO085 | 姿态传感器 | 平衡感：知道身体歪没歪 |
| LD14P | 360° 激光雷达 | 眼睛：扫描周围的墙和障碍 |
| 3S 电池 + 5 V 降压 | 电源 | 心脏 |
| 电脑（Ubuntu + ROS 2） | 大脑 | 建地图、规划路线、把手机的指令转给机器人 |
| 手机浏览器 | 遥控器 | 屏幕上的摇杆 |

![系统总览](img/fig0-overview.svg)

### 0.2 几个名词（不用背，遇到了回来查）

| 名词 | 意思 |
|---|---|
| **ROS 2** | 机器人软件的 “操作系统”，各个程序通过 “话题” 互相发消息。我们用 **Jazzy** 版本 |
| **话题** | 程序之间传消息的频道，比如 `/cmd_vel` 就是 “速度指令” 频道 |
| **micro-ROS** | 能跑在单片机上的迷你 ROS，让 XIAO 能通过 Wi-Fi 收发 ROS 消息 |
| **Agent** | 电脑上的一个程序，负责和机器人上的 micro-ROS 对接，**每次用都要先开** |
| **Docker / 容器** | 一个 “打包好的软件盒子”，ROS 2 和所有工具都装在里面，不会弄乱你的电脑 |
| **固件** | 刷进 XIAO 的程序 |
| **PlatformIO（`pio`）** | 编译、刷固件的工具，已经装在容器里 |
| **Seeker** | 我们用的开源项目 [SeekerRobot/seeker-robot](https://github.com/SeekerRobot/seeker-robot)，程序都是现成的 |

### 0.3 要花多少时间和钱

| 项目 | 大约 |
|---|---|
| 钱 | **¥600–1110**（XIAO 和两个套件你已经有了，明细见 [`bom.md`](bom.md)） |
| 时间 | 每周末做 1–2 天，**5–7 个周末**。其中等快递 1–2 周可以同时装电脑软件 |
| 最难的一步 | 阶段 7 的舵机标定，要耐心，留一整天 |

### 0.4 安全规则（先读，很重要）

1. **锂电池**：充电时人要在旁边；鼓包、变形立刻停用；不要过放（每节不低于 3.3 V，整包不低于 9.9 V）。
2. **降压模块先单独量电压**，确认是 5 V 左右再接任何东西，否则会烧坏主控和舵机。
3. **刷固件时关掉电池开关**，只用 USB 供电。
4. 舵机第一次通电时**手离开腿**，舵机会突然转动。
5. 电烙铁 300 °C 以上，用完放回烙铁架，拔电源。

- [ ] 我读完了第 0 节，知道要做什么、要花多少钱和时间、安全规则是什么

---

## 1. 买东西（第 1 周）

**目标**：把缺的零件和工具买齐。**时间**：下单 1 小时，等快递 1–2 周（等的时候去做阶段 2）。

- [ ] **1.1 核对你已经有的**：XIAO ESP32S3 Plus、Arduino 学习套件（顶配豪华版）、电子元件包。能用的零件见 [`bom.md` 第 0 节](bom.md#0-你已经有的arduino-学习套件顶配豪华版-电子元件包)（电压检测模块、灯环、面包板、杜邦线、排针、电阻、二极管、小螺丝刀）。
- [ ] **1.2 下单电子模块**：PCA9685 驱动板 × 1、BNO085 模块 × 1、LD14P 雷达（带转接板套装）× 1、**MG90S 金属齿 180° 舵机 × 14**。
  - 舵机一定要写着 “180°” 和 “金属齿”；雷达一定是 **LD14P**，不要买成 LD06 / LD19。
- [ ] **1.3 下单电源**：3S 11.1 V 1300 mAh XT30 电池 × 2、平衡充电器 × 1（有航模充电器就不用）、12 V 转 5 V 5 A 灌胶降压模块 × 1、迷你刀片保险丝座 + 5 A 保险丝、KCD1 船型开关 × 2、XT30 公母带线 × 2 对、1000 µF 16 V 电容 × 2、锂电 BB 响 × 1。
- [ ] **1.4 下单线材和结构件**：20 AWG 红黑硅胶线各 2 m、热缩管套装、M3 × 25 铜柱 4 根 + M3 × 6 螺丝 8 颗、M2 × 6 自攻螺丝 100 颗、M3 尼龙柱套装、魔术贴扎带 × 2、尼龙扎带一包、双面泡棉胶。
- [ ] **1.5 工具**（没有的才买）：**万用表**（必须）、电烙铁 + 焊锡、**舵机测试仪**（¥5，强烈建议）、游标卡尺、十字螺丝刀 PH0 / PH1、剥线钳。
- [ ] **1.6 3D 打印件**：有打印机就自己打（阶段 4）；没有就把 [`../cad/stl/`](../cad/stl/) 里的 5 个文件发给淘宝 “3D 打印代打” 店铺，材料选 **PETG**，数量：底板 1、甲板 1、大腿 A 3、大腿 B 3、小腿 8。

**✅ 成功的样子**：所有订单都下好了，[`bom.md`](bom.md) 总表里除了 “可选” 的都有着落。
**💡 省钱**：OLED、功放、摄像头板都是可选的，先不买，不影响遥控走路。

---

## 2. 准备电脑（第 1–2 周，等快递的时候做）

**目标**：电脑上装好 Ubuntu 24.04、Docker，把 Seeker 项目下载好并编译成功。**时间**：半天到一天（大部分时间在等下载）。

### 2.1 装 Ubuntu 24.04

推荐用一台**旧笔记本**专门装 Ubuntu，或者在现在的电脑上装**双系统**（Windows 和 Ubuntu 开机时二选一）。

- [ ] **2.1.1** 在 <https://ubuntu.com/download/desktop> 下载 **Ubuntu 24.04 LTS 桌面版**（约 6 GB）。
- [ ] **2.1.2** 准备一个 ≥ 8 GB 的 U 盘，用 [balenaEtcher](https://etcher.balena.io/) 或 [Rufus](https://rufus.ie/) 把下载的 `.iso` 写进去。
- [ ] **2.1.3** 电脑插 U 盘开机，按 F12 / F2 / Esc（不同品牌不一样）选 U 盘启动，按官方教程安装：<https://ubuntu.com/tutorials/install-ubuntu-desktop>。
  - 装双系统时选 “Install Ubuntu alongside Windows”（和 Windows 共存），**不要选清空整个硬盘**。
- [ ] **2.1.4** 装好后连上 Wi-Fi，打开 “终端”（按 `Ctrl + Alt + T`），输入下面这句更新系统（输入密码时屏幕不显示，是正常的）：

  ```bash
  sudo apt update && sudo apt upgrade -y
  ```

**✅ 成功的样子**：能进 Ubuntu 桌面、能上网，终端里命令跑完没有红色 `E:` 错误。
**❌ 卡住了**：进不了 U 盘启动 → 进 BIOS 关掉 “Secure Boot” 再试。实在不想装 Ubuntu，用 Windows + Docker Desktop 也行，但步骤多几步（[`software-setup.md` §1](software-setup.md#1-电脑准备)）。

### 2.2 装 Git 和 Docker

- [ ] **2.2.1** 在终端里一行一行复制运行（每行运行完再下一行）：

  ```bash
  sudo apt install -y git git-lfs curl
  git lfs install
  curl -fsSL https://get.docker.com | sudo sh
  sudo usermod -aG docker $USER
  sudo usermod -aG dialout $USER
  ```

- [ ] **2.2.2** **注销重新登录**（右上角 → 注销），让权限生效。
- [ ] **2.2.3** 测试 Docker：

  ```bash
  docker run --rm hello-world
  ```

**✅ 成功的样子**：屏幕打印出 `Hello from Docker!`。
**❌ 卡住了**：`permission denied` → 没注销重新登录；下载很慢 → 网络问题，给 Docker 配镜像加速（[`software-setup.md` §9](software-setup.md#9-常见问题)）。

### 2.3 下载 Seeker 项目，填 Wi-Fi 配置

- [ ] **2.3.1** 下载项目（放在主目录下）：

  ```bash
  cd ~
  git clone --recurse-submodules https://github.com/SeekerRobot/seeker-robot.git
  cd seeker-robot
  cp docker/.env.example docker/.env
  cp mcu_ws/platformio/network_config.example.ini mcu_ws/platformio/network_config.ini
  ```

- [ ] **2.3.2** 查电脑的 IP 和路由器地址，记下来：

  ```bash
  hostname -I          # 第一个数字就是电脑 IP，比如 192.168.1.23
  ip route | head -1   # "default via" 后面就是路由器 IP，比如 192.168.1.1
  ```

- [ ] **2.3.3** 用文本编辑器打开 `~/seeker-robot/mcu_ws/platformio/network_config.ini`（文件管理器里右键 → 用文本编辑器打开），改成你家的：
  - `wifi_ssid` / `wifi_password`：你家 **2.4 GHz** Wi-Fi 的名字和密码（5 GHz 的不行）；
  - `agent_ip`：电脑 IP，写成 `{ 192, 168, 1, 23 }` 这样逗号分开；
  - `static_ip`：给机器人选一个没人用的地址，比如 `{ 192, 168, 1, 50 }`；
  - `gateway`：路由器 IP；`ota_upload_port`：和 static_ip 一样，写成 `192.168.1.50`。
  - 详细说明见 [`software-setup.md` §3.2](software-setup.md#32-mcu_wsplatformionetwork_configiniwi-fi-和-ip)。
- [ ] **2.3.4**（推荐）登录路由器后台，把**电脑的 IP 绑定成固定的**（一般在 “DHCP 静态分配 / IP 与 MAC 绑定”），这样以后电脑 IP 不会变。
- [ ] **2.3.5** 按本仓库的机身改 4 个参数（复制运行即可）：

  ```bash
  cd ~/seeker-robot
  F=mcu_ws/lib/RobotConfig/HexapodConfig.h
  sed -i 's/constexpr float kL2 = 50.0f;/constexpr float kL2 = 65.0f;/' $F
  sed -i 's/constexpr float kNeutralKnee = 45.0f;/constexpr float kNeutralKnee = 60.0f;/' $F
  sed -i 's/constexpr float kHipMin = -60.0f;/constexpr float kHipMin = -25.0f;/' $F
  sed -i 's/constexpr float kHipMax = 60.0f;/constexpr float kHipMax = 25.0f;/' $F
  git diff --stat
  ```

**✅ 成功的样子**：最后一行显示 `HexapodConfig.h | 8 ++++----`（改了 4 行）。
**❌ 卡住了**：显示 0 行改动 → 原项目更新了写法，打开 `HexapodConfig.h` 手动找到 `kL2`、`kNeutralKnee`、`kHipMin`、`kHipMax` 改数字（[`software-setup.md` §4](software-setup.md#4-按本仓库的机身改固件参数)）。

### 2.4 编译 ROS 2 容器（最久的一步）

- [ ] **2.4.1** 构建容器（**第一次要 30–60 分钟**，去喝杯茶）：

  ```bash
  cd ~/seeker-robot/docker
  docker compose build --no-cache init-bootstrap
  docker compose build
  docker compose up init-bootstrap
  docker compose up -d ros2
  ```

- [ ] **2.4.2** 进入容器，编译 ROS 2 程序：

  ```bash
  docker compose exec ros2 bash
  # 下面这些在容器里运行（提示符会变）
  cd ~/ros2_workspaces
  source /opt/ros/jazzy/setup.bash
  colcon build
  echo 'source ~/ros2_workspaces/install/setup.bash' >> ~/.bashrc
  source ~/.bashrc
  ros2 pkg list | grep seeker
  ```

**✅ 成功的样子**：`colcon build` 最后显示 `Summary: N packages finished`，最后一句列出 `seeker_description`、`seeker_navigation`、`seeker_web` 等。
**💡 以后每次开新终端**：先 `cd ~/seeker-robot/docker && docker compose exec ros2 bash` 进容器（下文简称 “**进容器**”）。

---

## 3. 第一次成功：用手机遥控**仿真里的**机器人（不需要任何硬件）

**目标**：在电脑的 3D 仿真里看到六足机器人，用**手机**推摇杆让它走。这一步做通了，说明电脑、网络、手机网页全都没问题。**时间**：1 小时。

- [ ] **3.1** 终端 1 进容器，启动仿真：

  ```bash
  ros2 launch seeker_gazebo sim_teleop.launch.py
  ```

  会弹出 Gazebo 窗口，里面有一只六足机器人。
- [ ] **3.2** 终端 2 进容器，启动网页控制台：

  ```bash
  ros2 launch seeker_web web.launch.py
  ```

- [ ] **3.3** 手机连**同一个 Wi-Fi**，浏览器打开 `http://电脑IP:8080`（比如 `http://192.168.1.23:8080`）。
- [ ] **3.4** 先把网页上的速度滑块 **lin max 调到 0.08**、**yaw max 调到 0.5**，然后手指按住摇杆圆盘往上推。

**✅ 成功的样子**：Gazebo 里的六足迈腿往前走；摇杆往左它原地左转。🎉 **你已经用手机遥控了一台（虚拟的）ROS 2 机器人！**
**❌ 卡住了**：
- Gazebo 窗口不出来 → 宿主机终端运行 `xhost +local:docker` 后重试；
- 手机打不开网页 → 手机和电脑不在同一个 Wi-Fi，或者电脑防火墙：`sudo ufw allow 8080/tcp`；
- 报 `No module named 'aiohttp'` → 容器里 `sudo apt install -y python3-aiohttp`。
- 详见 [`phone-control.md` §5](phone-control.md#5-常见问题)。

---

## 4. 打印结构件（第 2 周）

**目标**：拿到 1 块底板、1 块甲板、3 个大腿 A、3 个大腿 B、6（+2）个小腿。**时间**：打印约 10–15 小时（代打就等快递）。

- [ ] **4.1** 舵机到货后，用游标卡尺量一个 MG90S：机身长 / 宽、安装耳总长、两个孔的距离。和 [`../cad/README.md` §2](../cad/README.md#2-打印前先量尺寸) 的默认值差超过 0.5 mm，就改 [`../cad/params.scad`](../cad/params.scad) 重新导出（没有差别就直接用现成 STL）。
- [ ] **4.2** **先打 1 个大腿试装**：PETG，层高 0.2 mm，4 圈壁，30 % 填充，**有凹槽的一面贴热床**，不用支撑（[`../cad/README.md` §3](../cad/README.md#3-打印设置)）。
- [ ] **4.3** 试装：舵机稍微用力能推进方孔；舵机自带的**单边舵机臂**能压进凹槽。太紧 / 太松就改 `clearance` 重打。
- [ ] **4.4** 打全套：底板、甲板（3 圈壁 20 % 填充）；大腿 A × 3、大腿 B × 3、小腿 × 8（4 圈壁 30 % 填充）。
- [ ] **4.5** 去毛刺，方孔用小锉刀修一下。

**✅ 成功的样子**：13 个零件都在，舵机能塞进底板和大腿的方孔。

---

## 5. 电子入门：焊排针、第一次刷固件（第 3 周）

**目标**：学会用万用表和电烙铁，XIAO 焊好排针，刷进第一个程序让灯闪。**时间**：半天。

### 5.1 学两个基本功（各 10 分钟）

- [ ] **5.1.1 万用表**：拨到 **直流电压 20 V 档（V⎓）**，红笔接 +、黑笔接 −，量一节 5 号电池应显示约 1.5 V。再拨到 **蜂鸣档（🔊）**，两支笔碰一起会响——以后 “查短路” 就用这个档：两点之间**响了 = 通 / 短路**。
- [ ] **5.1.2 焊接**：在元件包的一段排针和一块洞洞板上先练 5 个焊点：烙铁头同时贴住焊盘和针脚 2 秒 → 送锡丝 → 先撤锡丝再撤烙铁。**好焊点是亮的小圆锥**，不是一坨球。B 站搜 “焊接入门 排针” 看 5 分钟视频再练更快。

### 5.2 给 XIAO 焊排针

- [ ] **5.2.1** 从元件包的 40Pin 排针上掰两段 **7 针**。
- [ ] **5.2.2** 把两段排针**长的一头插进面包板**，XIAO **正面朝上**（USB-C 朝外）放上去，这样排针保证是直的。
- [ ] **5.2.3** 焊 14 个点。**背面那些 1.27 mm 的小焊盘不要碰**（D11–D19，不用）。
- [ ] **5.2.4** **插上棒状天线**（背面小圆座，按下去 “咔” 一声）。

### 5.3 第一次刷固件：让灯闪

- [ ] **5.3.1** XIAO 用 USB-C **数据线**插电脑（充电线不行）。宿主机终端运行 `ls /dev/ttyACM*`，应该看到 `/dev/ttyACM0`。
- [ ] **5.3.2** 重启一下容器让它认到 USB：`cd ~/seeker-robot/docker && docker compose restart ros2`，然后进容器：

  ```bash
  cd ~/mcu_workspaces/seeker_mcu/src/test_threaded_blink
  pio run -e esp32s3sense -t upload
  ```

  第一次会下载工具链，要 5–15 分钟。

**✅ 成功的样子**：显示 `SUCCESS`，XIAO 上的小灯开始闪。🎉
**❌ 卡住了**：
- 刷不进去 → **按住 XIAO 上的 B（BOOT）键不放 → 插 USB → 松开**，再运行一次上传；
- 找不到 `/dev/ttyACM0` → 换一根 USB 线；
- 为什么 Plus 用 `esp32s3sense` 环境：两块板引脚一样，放心用（[`software-setup.md` §4.1](software-setup.md#41-用-xiao-esp32s3-plus-做主板)）。

---

## 6. 桌面上逐个测试模块（第 3–4 周）

**目标**：机器人还没组装，先在桌子上把每个模块接到面包板上，一个一个测通。**一次只加一个模块**，前一个不通不要往下。**时间**：1–2 天。

接线全部按 [`wiring-guide.md`](wiring-guide.md)：**第 1 节的接线总表**是每根线的去向，图 1 是电源，图 2 是信号。

![XIAO 引脚接线](img/fig2-signals.svg)

### 6.1 Wi-Fi

- [ ] 进容器刷 Wi-Fi 测试，打开串口看输出：

  ```bash
  cd ~/mcu_workspaces/seeker_mcu/src/test_sub_wifi
  pio run -e esp32s3sense -t upload
  pio device monitor -b 921600
  ```

**✅** 每隔几秒打印 `WiFi: CONNECTED | SSID: 你的WiFi | IP: 192.168.x.50 | RSSI: -xx`。按 `Ctrl + C` 退出。
**❌** 一直 `CONNECTING` → Wi-Fi 名字密码写错，或者是 5 GHz；天线没插。

### 6.2 和电脑的 ROS 2 连起来（micro-ROS 心跳）

- [ ] 终端 1 进容器，开 Agent（**以后每次都要先开它**）：

  ```bash
  ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888
  ```

- [ ] 终端 2 进容器，刷心跳测试并查看：

  ```bash
  cd ~/mcu_workspaces/seeker_mcu/src/test_sub_heartbeat
  pio run -e esp32s3sense -t upload
  ros2 topic echo /mcu/heartbeat
  ```

**✅** 终端 1 出现 `create_session`；终端 2 每秒打印一个递增的数字 `data: 1`、`data: 2`……🎉 机器人和电脑的 ROS 2 连上了。
**❌** 一直没有 `create_session` → `agent_ip` 不是电脑 IP；电脑防火墙 `sudo ufw allow 8888/udp`。

### 6.3 姿态传感器 BNO085

- [ ] 拔掉 USB，按接线表接：BNO085 的 **VIN → 3V3、GND → GND、SDA → D4、SCL → D5、INT → D1**，如果是 Adafruit 版再把 **DI → 3V3**（[`wiring-guide.md` §3.2](wiring-guide.md#32-bno085-姿态传感器)）。面包板两侧的长条孔当 3V3 和 GND 母线。
- [ ] 插 USB，刷测试：

  ```bash
  cd ~/mcu_workspaces/seeker_mcu/src/test_sub_gyro_nondma
  pio run -e esp32s3senseserial -t upload
  pio device monitor -b 921600
  ```

**✅** 打印 BNO085 初始化成功；转动传感器，输出的数字跟着变。
**❌** `BNO08x not detected` → SDA / SCL 接反了；地址不对（[`software-setup.md` §7.4](software-setup.md#74-找不到设备--地址时i²c-扫描)）。

### 6.4 电源链路（最重要，慢慢来）

先**不接 XIAO、不接任何模块**，只接电源这一串（[`wiring-guide.md` §2](wiring-guide.md#2-电源接线)）：

![电源接线](img/fig1-power.svg)

- [ ] **6.4.1** 焊一根 XT30 母头线（接电池用）：红线 → 保险丝座 → 船型开关 → 这一端叫 **VIN**；黑线叫 **GND**。每个焊点套热缩管。
- [ ] **6.4.2** VIN、GND 接降压模块的**输入**（红进黑进）。
- [ ] **6.4.3** 电池开关**关着**，插上电池。万用表蜂鸣档确认降压模块**输出**红黑两根线**不短路**（不响）。
- [ ] **6.4.4** 打开开关，万用表直流电压档量降压模块输出。

**✅** 输出 **5.0–5.2 V**。关开关。
**❌** 不在这个范围 → 不要往下接！换模块或者调（可调模块拧电位器到 5.1 V）。

- [ ] **6.4.5** 接电压检测模块：输入端子接 VIN 和 GND；输出 **S → XIAO D3**、**− → GND**。
- [ ] **6.4.6** 焊防倒灌二极管：降压模块输出红线 → **二极管（白色条纹那头朝 XIAO）** → 杜邦线 → XIAO **5V** 脚；降压输出黑线 → XIAO **GND**。套热缩管。
- [ ] **6.4.7** **拔掉 USB**，只开电池开关：XIAO 电源灯亮；万用表量 XIAO 的 3V3 脚 ≈ 3.3 V。

**✅** XIAO 用电池也能开机。🎉

### 6.5 雷达 LD14P

- [ ] 接线：雷达 **5V → 降压输出 5 V**、**GND → GND**、**雷达 TX → XIAO D7**、**雷达 RX → XIAO D6**（交叉接，线序看雷达板丝印，[`wiring-guide.md` §3.3](wiring-guide.md#33-ld14p-激光雷达)）。
- [ ] 开电池开关，刷测试：

  ```bash
  cd ~/mcu_workspaces/seeker_mcu/src/test_sub_lidar
  pio run -e esp32s3senseserial -t upload
  pio device monitor -b 921600      # 然后输入 scan 回车
  ```

**✅** 雷达转起来，每圈约 720 个点。
**❌** 不转 → 雷达 5 V 没接好；转但没数据 → TX / RX 接反了。

### 6.6 舵机驱动板 PCA9685 + 第一个舵机

- [ ] 接线：PCA9685 **VCC → 3V3、GND → GND、SDA → D4、SCL → D5、OE → D0**；降压模块 5 V 输出再接一路到 PCA9685 的**绿色螺丝端子 V+ / GND**；端子旁边并上 **1000 µF 电容（长脚接 +）**（[`wiring-guide.md` §3.1](wiring-guide.md#31-pca9685-舵机驱动板)）。
- [ ] 先插学习套件的 **SG90** 到 **通道 0**，**棕线对 GND**。
- [ ] 刷测试，按顺序输入命令：

  ```bash
  cd ~/mcu_workspaces/seeker_mcu/src/test_sub_servo
  pio run -e esp32s3senseserial -t upload
  pio device monitor -b 921600
  # 在串口里依次输入：
  #   arm
  #   attach 0
  #   angle 0 90
  #   angle 0 45
  #   disarm
  ```

**✅** SG90 跟着命令转。然后换一个 MG90S 再试，**14 个 MG90S 都在通道 0 试一遍**，挑出坏的（或者直接用舵机测试仪试）。🎉
**❌** 不动 → 忘了 `arm`；PCA9685 的 V+ 没电；OE 线没接。

### 6.7 电池电压

- [ ] 刷 `test_sub_battery`（环境 `esp32s3sense`），串口输入 `read`，同时用万用表量电池。记下 “ADC 原始值 ↔ 实际电压” 两组（满电一组、快没电一组），填到固件里（[`software-setup.md` §7.8](software-setup.md#78-电池电压标定)）。可以放到最后做，不影响走路。

**✅ 阶段 6 完成的样子**：Wi-Fi、心跳、IMU、雷达、舵机、电池都单独测通了。

---

## 7. 组装机器人 + 舵机标定（第 4–5 周）

**目标**：把所有东西装到打印件上，让六条腿在 “站立” 姿势时左右对称、身体水平。**时间**：1–2 天，**舵机标定最花时间**。

跟着 [`wiring-guide.md` 第 6 节](wiring-guide.md#6-机械装配) 的 14 步做，这里列出关键的检查点：

- [ ] **7.1 舵机回中**：12 个舵机**在装舵机臂之前**全部用舵机测试仪拨到 “中位”（[`wiring-guide.md` §6.2](wiring-guide.md#62-舵机回中装舵机臂之前必须做)）。
- [ ] **7.2 装髋舵机**：6 个舵机从上往下插进底板方孔，**输出轴朝下、靠底板外边**，每个 2 颗 M2 自攻螺丝。底板上刻着 FL、FR、ML、MR、RL、RR（左前、右前、左中、右中、左后、右后）。
- [ ] **7.3 装膝舵机**：左边三条腿用**大腿 A**、右边用**大腿 B**，舵机从侧板外面插进去，输出轴在远离髋的那一头。
- [ ] **7.4 舵机臂压进大腿和小腿的凹槽**，各用 2 颗 M2 自攻螺丝固定。
- [ ] **7.5 装腿**：大腿**沿方孔方向笔直朝外**按到髋舵机上；小腿**朝下斜 45°** 按到膝舵机上；拧舵机臂中心螺丝。
- [ ] **7.6 走线**：膝舵机线沿大腿穿过底板走线槽，髋附近**留 3–4 cm 余量**，手动把大腿转到两头线都不绷紧。
- [ ] **7.7 装铜柱、甲板、电池、雷达**：电池用魔术贴绑在底板下面；降压模块、保险丝粘在底板上面；甲板上装 PCA9685、面包板、BNO085（**X 箭头朝前**）、开关；雷达用 4 根 M3 × 20 尼龙柱架在最上面。
- [ ] **7.8 按通道插 12 个舵机**（[`wiring-guide.md` §4](wiring-guide.md#4-12-个舵机插哪个通道)）：

  | 腿 | 髋舵机 → 通道 | 膝舵机 → 通道 |
  |---|---|---|
  | FL 左前 | 6 | 13 |
  | FR 右前 | 1 | 9 |
  | ML 左中 | 5 | 12 |
  | MR 右中 | 2 | 10 |
  | RL 左后 | 4 | 11 |
  | RR 右后 | 3 | 0 |

- [ ] **7.9 通电检查**：按 [`wiring-guide.md` §7](wiring-guide.md#7-第一次通电检查清单) 的 7 步做一遍（先查短路，再逐步通电）。
- [ ] **7.10 舵机标定**：刷 `test_sub_servo`，**一个舵机一个舵机**找到：髋 −25° / +25°、膝 0°（小腿水平）/ 90°（小腿竖直向下）对应的数值，填进 `HexapodConfig.h` 的 `kServoConfigs`，方向反了改 `inverted`（[`software-setup.md` §7.7](software-setup.md#77-舵机标定最花时间的一步)）。
  - 💡 把机器人**架空**（垫个盒子，脚不着地）再标定，腿乱动也不会摔。
  - 💡 髋关节 0° 时，让**脚尖**对准腿的安装方向。
- [ ] **7.11 站起来试试**：刷 `test_sub_gait`（环境 `esp32s3senseserial`），串口输入 `neutral`。

**✅ 成功的样子**：`neutral` 以后六条腿对称，机身水平，底板离地约 7 cm。

---

## 8. 🎯 最终目标：用手机遥控机器人走路

**目标**：真机器人跟着手机摇杆走。**时间**：半天。

- [ ] **8.1 串口先走两步**：还是 `test_sub_gait`，架空状态下输入 `start` → `vel 0.03` → `status` → `halt`。六条腿应该按三角步态交替迈步（三条腿一组），不打架。
- [ ] **8.2 放到地上再试一次** `start` → `vel 0.03` → `halt`，它应该往前慢慢走。
- [ ] **8.3 刷主固件**（带步态 + 雷达 + IMU + 电池，接收 `/cmd_vel`）：

  ```bash
  cd ~/mcu_workspaces/seeker_mcu/src/main
  pio run -e esp32s3sense_offload -t upload
  ```

  拔 USB，开电池开关。状态灯（如果接了灯环）：彩虹 → 红色追逐（连 Wi-Fi）→ 黄色呼吸（等 ROS）→ **青色慢呼吸（就绪）**。
- [ ] **8.4 电脑开两个终端**（都要进容器）：

  ```bash
  # 终端 1：Agent
  ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888
  # 终端 2：手机网页（mcu_ip 填机器人的 static_ip）
  ros2 launch seeker_web web.launch.py mcu_ip:=192.168.1.50
  ```

- [ ] **8.5 手机打开** `http://电脑IP:8080`，网页上能看到电池电压、心跳在更新。
- [ ] **8.6 滑块调小**：lin max **0.06**、yaw max **0.4**。
- [ ] **8.7 手指轻推摇杆往上**，机器人往前走；往左推，原地左转；松手就停。
- [ ] **8.8 验收**：连续前进 3 米不摔倒，能原地转一圈，走回起点。

**✅ 成功的样子**：🎉🎉🎉 **你从零做出了一台能用手机遥控的 ROS 2 六足机器人！**
**❌ 卡住了**：
- 网页上数据不更新 → 终端 1 没有 `create_session`，机器人没连上（查 Wi-Fi 和 `agent_ip`）；
- 推前进它后退 / 转反了 → 舵机标定的 `inverted` 反了，回阶段 7.10；
- 走一下停一下 → Wi-Fi 信号差，靠近路由器；
- 走着走着趴下 → 电池没电了（BB 响会叫），或者膝角太小；
- 更多：[`phone-control.md` §5](phone-control.md#5-常见问题)、[`build-plan.md` §5](build-plan.md#5-故障排查)。

---

## 9. 以后可以继续玩的

| 进阶 | 看哪里 |
|---|---|
| 用手机遥控它绕房间一圈，**建出房间地图** | [`phone-control.md` §4](phone-control.md#4-边遥控边建图)、[`software-setup.md` §8.2](software-setup.md#82-建图slam-toolbox) |
| 在地图上点一个位置，它**自己走过去**（Nav2） | [`software-setup.md` §8.3](software-setup.md#83-导航nav2) |
| 加摄像头板，让它**找泰迪熊** | [`software-setup.md` §8.4](software-setup.md#84-可选摄像头卫星板--找东西) |
| 加 OLED 显示表情、喇叭说话 | [`bom.md`](bom.md) 的 A7、A8 |
| 把面包板换成焊好的洞洞板，更结实 | [`wiring-guide.md` §5](wiring-guide.md#5-面包板免焊-洞洞板布局) |

---

## 10. 时间表（参考）

| 周 | 做什么 | 阶段 |
|---|---|---|
| 第 1 周 | 下单；装 Ubuntu、Docker；下载 Seeker；编译容器 | 1、2 |
| 第 2 周 | 手机遥控仿真；打印结构件 | 3、4 |
| 第 3 周 | 练焊接、焊排针、刷第一个固件；测 Wi-Fi、心跳、IMU | 5、6.1–6.3 |
| 第 4 周 | 电源链路、雷达、舵机测试；开始组装 | 6.4–6.7、7.1–7.9 |
| 第 5 周 | 舵机标定；串口步态 | 7.10–7.11、8.1–8.2 |
| 第 6 周 | 刷主固件，**手机遥控真机** 🎯 | 8 |

每一步卡住超过一个小时，就停下来看对应文档的 “常见问题”，或者把**串口输出 / 报错截图**发给我，我帮你看。
