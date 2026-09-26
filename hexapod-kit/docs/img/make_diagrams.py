#!/usr/bin/env python3
"""Generate the diagrams (SVG) used by the hexapod-kit docs.

    python3 hexapod-kit/docs/img/make_diagrams.py

Pure standard library. Reuses the drawing helpers of hexapod/docs/img/make_diagrams.py.
"""
import math
import os
import sys

sys.dont_write_bytecode = True

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "..", "hexapod", "docs", "img")))
import make_diagrams as base  # noqa: E402

base.OUT = HERE
Svg, C = base.Svg, base.C

# Default geometry from firmware/hexapod_g7pro/params.h (mm)
COXA, FEMUR, TIBIA = 28, 50, 75
FRONT_X, FRONT_Y, FRONT_A = 60, 40, 45
MID_Y = 55
STANCE = 80


def fig_system():
    s = Svg(900, 540, "系统连接图", "手柄用蓝牙直接连主板：走路时不需要电脑，也不需要手机")
    s.box(30, 100, 200, 110, "盖世小鸡 G7 Pro", "背面开关拨到「蓝牙」\n长按配对键")
    s.box(340, 90, 230, 140, "ESP32 主板（WROOM）", "本项目固件 hexapod_g7pro\nBluepad32：经典蓝牙连手柄\n50 Hz 逆运动学 + 步态", fill=C["hi"])
    s.box(650, 90, 220, 110, "电脑（只在设置时用）", "USB-C 刷固件\n串口命令：找插口 / 标定 / 调参", dashed=True)
    s.box(340, 320, 230, 110, "18 路舵机扩展板", "PCA9685（I²C）或 GPIO 直连\n板上稳压，给舵机供电")
    s.box(650, 320, 220, 110, "18 × MG90S 舵机", "6 条腿 × 3 个关节\n基节 / 大腿 / 小腿")
    s.box(30, 320, 200, 110, "2 × 18650 电池盒", "7.4 V（充满 8.4 V）\n套件自带充电线")
    s.box(650, 450, 220, 64, "ESP32-CAM / 超声波", "以后再玩（第 8 步）", fill="#FFFFFF", dashed=True)

    s.wire([(230, 150), (340, 150)], C["rf"], 3, dashed=True)
    s.text(285, 138, "经典蓝牙", 12, "middle", "700", C["mute"])
    s.wire([(570, 145), (650, 145)], C["gnd"], 3)
    s.text(610, 133, "USB-C", 12, "middle", "700", C["mute"])
    s.arrow(455, 230, 455, 318, C["sig"])
    s.text(465, 280, "I²C 或 18 路 PWM 信号", 12, "start", "700", C["sig"])
    s.arrow(570, 375, 648, 375, C["sig"])
    s.wire([(230, 375), (340, 375)], C["bat"], 4)
    s.text(285, 363, "7.4 V", 12, "middle", "700", C["bat"])
    s.wire([(400, 320), (400, 260), (380, 260), (380, 232)], C["v5"], 3)
    s.text(372, 290, "5 V", 12, "end", "700", C["v5"])
    s.legend(30, 505, [("电池电源", C["bat"], False), ("5 V", C["v5"], False), ("信号", C["sig"], False),
                       ("蓝牙（无线）", C["rf"], True)])
    s.save("fig1-system.svg")


def fig_gamepad():
    s = Svg(900, 560, "G7 Pro 按键图", "Xbox 布局；手柄断开时机器人自动停下站稳")
    cx, cy = 450, 290
    body = (f"M{cx - 150},{cy - 90} Q{cx},{cy - 120} {cx + 150},{cy - 90} "
            f"Q{cx + 205},{cy - 80} {cx + 215},{cy + 20} Q{cx + 230},{cy + 150} {cx + 170},{cy + 160} "
            f"Q{cx + 130},{cy + 165} {cx + 95},{cy + 90} L{cx - 95},{cy + 90} "
            f"Q{cx - 130},{cy + 165} {cx - 170},{cy + 160} Q{cx - 230},{cy + 150} {cx - 215},{cy + 20} "
            f"Q{cx - 205},{cy - 80} {cx - 150},{cy - 90} Z")
    s.parts.append(f'<path d="{body}" fill="{C["box"]}" stroke="{C["boxline"]}" stroke-width="2"/>')
    for x in (cx - 130, cx + 130):                                  # bumpers
        s.parts.append(f'<rect x="{x - 45}" y="{cy - 122}" width="90" height="20" rx="10" fill="#FFFFFF" '
                       f'stroke="{C["boxline"]}" stroke-width="1.6"/>')
    s.text(cx - 130, cy - 107, "LB", 12, "middle", "700")
    s.text(cx + 130, cy - 107, "RB", 12, "middle", "700")

    def stick(x, y, label):
        s.parts.append(f'<circle cx="{x}" cy="{y}" r="30" fill="#FFFFFF" stroke="{C["boxline"]}" stroke-width="2"/>')
        s.parts.append(f'<circle cx="{x}" cy="{y}" r="17" fill="{C["mute"]}"/>')
        s.text(x, y + 4, label, 11, "middle", "700", "#FFFFFF")

    ls, rs, dp, ab = (cx - 130, cy - 30), (cx + 70, cy + 50), (cx - 70, cy + 50), (cx + 130, cy - 30)
    stick(*ls, "L")
    stick(*rs, "R")
    x, y = dp                                                       # D-pad
    s.parts.append(f'<path d="M{x - 9},{y - 27} h18 v18 h18 v18 h-18 v18 h-18 v-18 h-18 v-18 h18 Z" '
                   f'fill="#FFFFFF" stroke="{C["boxline"]}" stroke-width="1.6"/>')
    for name, dx, dy, col in (("Y", 0, -24, "#C9A100"), ("X", -24, 0, "#1F6FB5"), ("B", 24, 0, "#D23B2F"),
                              ("A", 0, 24, "#1E8A5A")):
        s.parts.append(f'<circle cx="{ab[0] + dx}" cy="{ab[1] + dy}" r="12" fill="{col}"/>')
        s.text(ab[0] + dx, ab[1] + dy + 4.5, name, 12, "middle", "700", "#FFFFFF")
    s.parts.append(f'<circle cx="{cx}" cy="{cy - 62}" r="14" fill="#FFFFFF" stroke="{C["boxline"]}" stroke-width="1.6"/>')
    s.parts.append(f'<circle cx="{cx}" cy="{cy - 62}" r="7" fill="#1E8A5A"/>')

    def call(px, py, tx, ty, lines, anchor):
        s.wire([(px, py), (tx + (8 if anchor == "start" else -8), ty - 4)], C["mute"], 1.3)
        s.parts.append(f'<circle cx="{px}" cy="{py}" r="3.5" fill="{C["ink"]}"/>')
        for i, line in enumerate(lines):
            s.text(tx + (12 if anchor == "start" else -12), ty + i * 18, line, 13 if i == 0 else 12, anchor,
                   "700" if i == 0 else "400", None if i == 0 else C["mute"])

    L, R = 30, 870
    call(cx - 170, cy - 112, 180, 100, ["LB：速度档 −", "3 档：慢 / 中 / 快"], "end")
    call(ls[0] - 30, ls[1], 180, 205, ["左摇杆：走路", "上下 = 前进 / 后退", "左右 = 横着走", "按住 Y 时：俯仰 / 侧倾"], "end")
    call(dp[0] - 27, dp[1], 180, 330, ["十字键", "上 / 下 = 机身升高 / 降低", "左 / 右 = 抬腿降低 / 升高"], "end")
    call(cx + 170, cy - 112, R - 150, 100, ["RB：速度档 +"], "start")
    call(ab[0] + 12, ab[1] - 24, R - 150, 160, ["Y（按住）：扭身模式", "脚不动，只动身体"], "start")
    call(ab[0] - 36, ab[1], R - 150, 212, ["X：换步态", "三角 → 涟漪 → 波浪"], "start")
    call(ab[0] + 36, ab[1], R - 150, 262, ["B：急停", "松开两个摇杆后解除"], "start")
    call(ab[0], ab[1] + 36, R - 150, 312, ["A：起立 / 趴下", "开机后第一次按 = 舵机上电"], "start")
    call(rs[0] + 30, rs[1], R - 150, 372, ["右摇杆：左右 = 原地转向", "按住 Y 时：扭腰 / 前后平移"], "start")
    call(cx, cy - 76, cx, 470, ["Xbox 键：开机；配对时长按背面配对键"], "middle")
    s.note(L, 525, 1, "手柄背面模式开关必须在「蓝牙」档；2.4G 接收器和有线模式单片机用不了")
    s.save("fig2-gamepad.svg")


def fig_servo_numbers():
    s = Svg(900, 700, "腿和舵机编号（俯视）", "舵机编号 n = 腿号 × 3 + 关节号；标定和 map 命令都用这个编号")
    ox, oy, k = 450, 335, 1.35

    def scr(x, y):          # robot frame (x forward, y left) -> screen
        return ox - y * k, oy - x * k

    legs = [("LF", 0, FRONT_X, FRONT_Y, FRONT_A), ("LM", 1, 0, MID_Y, 90), ("LR", 2, -FRONT_X, FRONT_Y, 180 - FRONT_A),
            ("RF", 3, FRONT_X, -FRONT_Y, -FRONT_A), ("RM", 4, 0, -MID_Y, -90), ("RR", 5, -FRONT_X, -FRONT_Y, FRONT_A - 180)]
    outline = [scr(x, y) for _, _, x, y, _ in (legs[0], legs[1], legs[2], legs[5], legs[4], legs[3])]
    pts = " ".join(f"{x:.1f},{y:.1f}" for x, y in outline)
    s.parts.append(f'<polygon points="{pts}" fill="{C["hi"]}" stroke="{C["boxline"]}" stroke-width="2"/>')
    s.text(ox, oy - 8, "机身", 15, "middle", "700")
    s.text(ox, oy + 12, "ESP32 + 扩展板", 11.5, "middle", "400", C["mute"])
    s.arrow(ox, oy - 95, ox, oy - 170, C["ink"], 3)
    s.text(ox, oy - 180, "前（机头，超声波这一头）", 13, "middle", "700")
    for name, idx, mx, my, a in legs:
        ar = math.radians(a)
        hx, hy = scr(mx, my)
        kx, ky = scr(mx + (COXA + FEMUR) * math.cos(ar), my + (COXA + FEMUR) * math.sin(ar))
        fx, fy = scr(mx + (STANCE + 40) * math.cos(ar), my + (STANCE + 40) * math.sin(ar))
        s.wire([(hx, hy), (kx, ky)], C["sig"], 7)
        s.wire([(kx, ky), (fx, fy)], C["v5"], 5)
        s.parts.append(f'<circle cx="{hx:.1f}" cy="{hy:.1f}" r="9" fill="#FFFFFF" stroke="{C["ink"]}" stroke-width="3"/>')
        s.parts.append(f'<circle cx="{kx:.1f}" cy="{ky:.1f}" r="7" fill="#FFFFFF" stroke="{C["ink"]}" stroke-width="3"/>')
        ux, uy = fx - hx, fy - hy
        n = math.hypot(ux, uy)
        ux, uy = ux / n, uy / n
        lx, ly = fx + ux * 26, fy + uy * 26
        anchor = "start" if ux > 0.3 else ("end" if ux < -0.3 else "middle")
        if abs(ux) <= 0.3:
            ly += 10 if uy > 0 else -40
        s.text(lx, ly, f"{idx} {name}", 16, anchor, "700")
        s.text(lx, ly + 20, f"基节 {idx * 3} · 大腿 {idx * 3 + 1} · 小腿 {idx * 3 + 2}", 12.5, anchor, "600", C["sig"])
    s.note(40, 615, 1, "关节 0 基节（coxa）：+ = 俯视逆时针摆。关节 1 大腿（femur）：+ = 抬起")
    s.note(40, 643, 2, "关节 2 小腿（tibia）：+ = 膝盖张开、脚尖往外。方向反了就用 dir 命令翻过来")
    s.note(40, 671, 3, "腿号：0 左前 LF · 1 左中 LM · 2 左后 LR · 3 右前 RF · 4 右中 RM · 5 右后 RR")
    s.save("fig3-servo-numbers.svg")


def fig_leg_measure():
    s = Svg(900, 500, "量腿长（侧视，装配姿态）", "量转轴中心到转轴中心；小腿量到脚尖。量好后用 set coxa / femur / tibia 写进固件")
    k = 3.0
    ax, py = 190, 210                       # hip-yaw axis x, femur pivot height
    fx = ax + COXA * k
    kx = fx + FEMUR * k
    gy = py + TIBIA * k
    s.parts.append(f'<rect x="40" y="{py - 40}" width="{ax - 40 + 20}" height="60" rx="6" fill="{C["hi"]}" '
                   f'stroke="{C["boxline"]}" stroke-width="1.6"/>')
    s.text(105, py - 5, "机身", 14, "middle", "700")
    s.wire([(ax, py - 90), (ax, gy + 10)], C["mute"], 1.5, dashed=True)
    s.text(ax, py - 98, "基节转轴（竖直）", 12, "middle", "700", C["mute"])
    s.wire([(ax, py), (fx, py)], C["i2c"], 8)
    s.wire([(fx, py), (kx, py)], C["sig"], 8)
    s.wire([(kx, py), (kx, gy)], C["v5"], 7)
    for x, y, r in ((ax, py, 9), (fx, py, 9), (kx, py, 8)):
        s.parts.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="#FFFFFF" stroke="{C["ink"]}" stroke-width="3"/>')
    s.parts.append(f'<circle cx="{kx}" cy="{gy}" r="5" fill="{C["ink"]}"/>')
    s.wire([(40, gy), (860, gy)], C["gnd"], 2)
    s.text(850, gy + 20, "地面", 12, "end", "700", C["mute"])

    def dim(x1, y1, x2, y2, label, color, off=(0, -14)):
        s.arrow(x1 + (x2 - x1) * 0.5, y1 + (y2 - y1) * 0.5, x1, y1, color, 1.8)
        s.arrow(x1 + (x2 - x1) * 0.5, y1 + (y2 - y1) * 0.5, x2, y2, color, 1.8)
        s.text((x1 + x2) / 2 + off[0], (y1 + y2) / 2 + off[1], label, 13, "middle", "700", color)

    dim(ax, py - 50, fx, py - 50, f"coxa ≈ {COXA}", C["i2c"])
    dim(fx, py - 50, kx, py - 50, f"femur ≈ {FEMUR}", C["sig"])
    dim(kx + 50, py, kx + 50, gy, f"tibia ≈ {TIBIA}", C["v5"], (52, 4))
    s.wire([(fx, py - 58), (fx, py - 12)], C["mute"], 1)
    s.wire([(kx, py - 58), (kx, py - 12)], C["mute"], 1)
    s.wire([(kx + 12, py), (kx + 58, py)], C["mute"], 1)
    s.wire([(kx + 12, gy), (kx + 58, gy)], C["mute"], 1)
    s.text(fx, py + 32, "大腿舵机轴", 12, "middle", "600", C["mute"])
    s.text(kx + 14, py + 32, "膝 = 小腿舵机轴", 12, "start", "600", C["mute"])
    s.text(kx - 12, gy - 10, "脚尖", 12, "end", "600", C["mute"])
    s.note(610, 290, 1, "装配姿态 = 所有关节角 0°：")
    s.text(638, 312, "大腿水平，小腿竖直朝下，", 12.5, color=C["mute"])
    s.text(638, 330, "基节垂直于机身边", 12.5, color=C["mute"])
    s.note(610, 368, 2, "图上数字是默认值（毫米），")
    s.text(638, 390, "只是估计，一定要自己量", 12.5, color=C["mute"])
    s.save("fig4-leg-measure.svg")


if __name__ == "__main__":
    fig_system()
    fig_gamepad()
    fig_servo_numbers()
    fig_leg_measure()
    print("diagrams written to", HERE)
