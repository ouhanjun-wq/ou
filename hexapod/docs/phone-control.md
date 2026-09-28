# 手机控制（Wi-Fi，浏览器打开就能用）

> 不用买手柄、不用装 App、不用写代码：Seeker 项目自带一个网页控制台 **`seeker_web`**，在电脑上启动后，**手机连同一个 Wi-Fi，用浏览器打开一个网址**，屏幕上就有一个**触屏虚拟摇杆**，推一推机器人就走。

---

## 1. 怎么连

```mermaid
flowchart LR
  P[手机浏览器<br/>虚拟摇杆] -- Wi-Fi<br/>http://电脑IP:8080 --> W[电脑：seeker_web]
  W -- /cmd_vel --> A[micro-ROS Agent]
  A -- Wi-Fi UDP 8888 --> X[XIAO ESP32S3 Plus<br/>步态]
  X -- PCA9685 --> S[12 个舵机]
```

手机、电脑、机器人**连同一个路由器**就行。电脑在中间负责把手机的指令转成 ROS 2 的 `/cmd_vel`，这也是 Nav2 自动导航用的同一个接口。

---

## 2. 三步用起来

需要 2 个终端，都先 `docker compose exec ros2 bash` 进容器（第一次使用前已经 `colcon build` 过，见软件文档 §5）：

```bash
# 终端 1：micro-ROS Agent（机器人连电脑用）
ros2 run micro_ros_agent micro_ros_agent udp4 --port 8888

# 终端 2：网页控制台（mcu_ip 填机器人的固定 IP，就是 network_config.ini 里的 static_ip）
ros2 launch seeker_web web.launch.py mcu_ip:=192.168.8.50
```

然后：

1. 电脑上查 IP：`hostname -I`，比如 `192.168.8.134`。
2. 手机连**同一个 Wi-Fi**，浏览器打开 **`http://192.168.8.134:8080`**（换成你电脑的 IP）。
3. 机器人刷 `main` 固件并开机，状态灯变成青色慢呼吸（已连上），**手指按住屏幕上的摇杆圆盘拖动**，机器人就走。

> 💡 手机浏览器里点 “分享 → 添加到主屏幕”，以后像 App 一样一点就开。

---

## 3. 怎么操作

| 手机上 | 机器人 |
|---|---|
| 摇杆往**上**推 / 往**下**拉 | 前进 / 后退 |
| 摇杆往**左** / **右** | 原地左转 / 右转 |
| 松开手指 | 摇杆回中，速度变 0，停下 |
| **速度滑块**（lin max / yaw max） | 摇杆推到底时的最快速度 |
| **急停按钮（E-Stop）** | 立刻停 |
| 页面上的数据区 | 实时显示电池电压、IMU、雷达点云、心跳 |

**第一次用先把滑块调小**：网页默认最高 0.15 m/s、1.0 rad/s，本机身（髋关节 ±25°）最快约 0.16 m/s，建议先调到：

| 滑块 | 建议值 | 说明 |
|---|---|---|
| lin max（前后速度） | **0.06–0.10 m/s** | 走得稳；熟练了再加 |
| yaw max（转向速度） | **0.4–0.6 rad/s** | 约 23–34°/s |

网页每秒发 20 次指令：

$$
v_x = v_{\max} \cdot a_y,\qquad \omega_z = \omega_{\max} \cdot a_x,\qquad a_x, a_y \in [-1, 1]
$$

$a$ 是摇杆偏离中心的比例。固件里有 **0.5 s 安全看门狗**：手机锁屏、切到别的 App、Wi-Fi 断了，0.5 s 收不到指令机器人就自己停。

> 网页摇杆只有 “前后 + 转向”，没有横着平移。想横着走，用键盘遥控 `teleop_twist_keyboard`（按 Shift + J / L）。

---

## 4. 边遥控边建图

手机控制和建图可以同时开：

```bash
# 终端 3（容器里）
ros2 launch seeker_navigation real_slam_ekf.launch.py
```

用手机慢慢把房间走一圈，电脑上 `rviz2` 看地图，走完保存（软件文档 §8.2）。

> **Nav2 自动导航时不要同时推摇杆**：两边都往 `/cmd_vel` 发指令会打架。导航时把手机页面关掉或者不碰摇杆。注意网页开着时**摇杆不碰也会一直发 0**，所以用 Nav2 前要**关掉 `seeker_web`（终端 2 按 Ctrl+C）**。

---

## 5. 常见问题

| 现象 | 解决 |
|---|---|
| 手机打不开网页 | ① 手机和电脑是不是**同一个 Wi-Fi**（有的路由器 “访客网络” 互相隔离）；② 电脑防火墙：`sudo ufw allow 8080/tcp`；③ Windows + Docker Desktop 的 `bridge` 网络要在 `docker-compose.yml` 里映射 `8080:8080` 端口 |
| 终端 2 报 `No module named 'aiohttp'` | 容器里补装：`sudo apt update && sudo apt install -y python3-aiohttp` |
| 网页开了，推摇杆机器人不动 | 看网页上的心跳 / 电池数据有没有在更新：没有 → micro-ROS 没连上（终端 1 应该打印 `create_session`）；有 → 看网页下方显示的 `vx= … wz= …` 有没有变 |
| 走一下停一下 | Wi-Fi 信号差触发了 0.5 s 看门狗，机器人和手机都离路由器近一点 |
| 方向反了 | 舵机标定的 `inverted` 反了（软件文档 §7.7） |

---

## 6. 用到的资料

| 网站 | 用途 |
|---|---|
| [seeker_web 源码](https://github.com/SeekerRobot/seeker-robot/tree/main/ros2_ws/src/seeker_web) | 网页控制台（aiohttp 服务器 + 触屏摇杆，端口 8080） |
| [Seeker GaitRosParticipant](https://github.com/SeekerRobot/seeker-robot/blob/main/mcu_ws/lib/GaitController/GaitRosParticipant.h) | 固件怎么收 `/cmd_vel`（0.5 s 看门狗） |
