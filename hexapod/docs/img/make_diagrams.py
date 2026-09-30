#!/usr/bin/env python3
"""Generate the hexapod + gesture glove diagrams (SVG) used by hexapod/docs/*.md.

    python hexapod/docs/img/make_diagrams.py

Pure standard library; edit the drawing functions below and re-run.
"""
import os
from xml.sax.saxutils import escape

OUT = os.path.dirname(os.path.abspath(__file__))
FONT = "'PingFang SC','Microsoft YaHei','Noto Sans CJK SC','WenQuanYi Zen Hei',sans-serif"

C = {
    "v12": "#D23B2F",    # 12 V input
    "v6": "#E07B00",     # 6 V servo supply
    "v5": "#A87B00",     # 5 V logic supply
    "v33": "#7A4CC2",    # 3.3 V
    "gnd": "#222222",    # ground
    "sig": "#1F6FB5",    # signals
    "i2c": "#1E8A5A",    # I2C bus
    "rf": "#8A8A8A",     # wireless (dashed)
    "ink": "#1B2A2F",
    "mute": "#5E6E72",
    "box": "#F3F6F5",
    "boxline": "#2B3A3F",
    "hi": "#FFF3D6",
    "arm": "#DDE6EA",
}


class Svg:
    def __init__(self, w, h, title, subtitle=""):
        self.w, self.h = w, h
        self.parts = [
            f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}" '
            f'font-family="{FONT}">',
            f'<rect width="{w}" height="{h}" fill="#FFFFFF"/>',
        ]
        self.text(24, 36, title, 20, weight="700")
        if subtitle:
            self.text(24, 60, subtitle, 13, color=C["mute"])

    def text(self, x, y, s, size=13, anchor="start", weight="400", color=None):
        self.parts.append(
            f'<text x="{x}" y="{y}" font-size="{size}" text-anchor="{anchor}" font-weight="{weight}" '
            f'fill="{color or C["ink"]}">{escape(s)}</text>')

    def box(self, x, y, w, h, title, sub="", fill=None, dashed=False, title_dy=22):
        dash = ' stroke-dasharray="6 4"' if dashed else ""
        self.parts.append(
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="8" fill="{fill or C["box"]}" '
            f'stroke="{C["boxline"]}" stroke-width="1.6"{dash}/>')
        self.text(x + w / 2, y + title_dy, title, 14, "middle", "700")
        if sub:
            for i, line in enumerate(sub.split("\n")):
                self.text(x + w / 2, y + title_dy + 18 + i * 16, line, 11.5, "middle", color=C["mute"])

    def pin(self, x, y, label, side="left", color=None):
        """A terminal dot on a box edge with its label inside the box."""
        self.parts.append(f'<circle cx="{x}" cy="{y}" r="4" fill="{color or C["boxline"]}"/>')
        dx, anchor = (8, "start") if side == "left" else ((-8, "end") if side == "right" else (0, "middle"))
        dy = 4 if side in ("left", "right") else (16 if side == "top" else -8)
        self.text(x + dx, y + dy, label, 11.5, anchor, "600")

    def wire(self, pts, color, width=3, dashed=False):
        d = " ".join(f"{'M' if i == 0 else 'L'}{x},{y}" for i, (x, y) in enumerate(pts))
        dash = ' stroke-dasharray="7 5"' if dashed else ""
        self.parts.append(
            f'<path d="{d}" fill="none" stroke="{color}" stroke-width="{width}" stroke-linejoin="round" '
            f'stroke-linecap="round"{dash}/>')

    def arrow(self, x1, y1, x2, y2, color, width=2.5):
        import math
        self.wire([(x1, y1), (x2, y2)], color, width)
        a = math.atan2(y2 - y1, x2 - x1)
        p1 = (x2 - 10 * math.cos(a - 0.45), y2 - 10 * math.sin(a - 0.45))
        p2 = (x2 - 10 * math.cos(a + 0.45), y2 - 10 * math.sin(a + 0.45))
        self.parts.append(f'<path d="M{x2},{y2} L{p1[0]:.1f},{p1[1]:.1f} L{p2[0]:.1f},{p2[1]:.1f} Z" fill="{color}"/>')

    def marker(self, x, y, n):
        self.parts.append(f'<circle cx="{x}" cy="{y}" r="11" fill="{C["v12"]}" stroke="#FFFFFF" stroke-width="2"/>')
        self.text(x, y + 4.5, str(n), 12, "middle", "700", "#FFFFFF")

    def dot(self, x, y, color):
        self.parts.append(f'<circle cx="{x}" cy="{y}" r="5" fill="{color}"/>')

    def tag(self, x, y, label, color, anchor="start"):
        """Net label: a coloured pill meaning 'connect to the net of this name'."""
        w = 8 + 7.2 * sum(2 if ord(ch) > 127 else 1 for ch in label)
        rx = x if anchor == "start" else x - w
        self.parts.append(f'<rect x="{rx}" y="{y - 11}" width="{w}" height="22" rx="11" fill="{color}"/>')
        self.text(rx + w / 2, y + 4.5, label, 11.5, "middle", "700", "#FFFFFF")

    def part(self, x, y, w, h, label, vertical=False, fill="#FFFFFF"):
        """Small component (resistor, capacitor, diode) drawn as a labelled block."""
        self.parts.append(
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="3" fill="{fill}" stroke="{C["boxline"]}" '
            f'stroke-width="1.4"/>')
        if vertical:
            self.text(x + w + 6, y + h / 2 + 4, label, 11.5, "start", "600")
        else:
            self.text(x + w / 2, y + h / 2 + 4, label, 11, "middle", "600")

    def note(self, x, y, n, s, width=None):
        self.parts.append(f'<circle cx="{x + 10}" cy="{y - 4}" r="10" fill="{C["ink"]}"/>')
        self.text(x + 10, y + 0.5, str(n), 12, "middle", "700", "#FFFFFF")
        self.text(x + 28, y, s, 13)

    def legend(self, x, y, items):
        cx = x
        for label, color, dashed in items:
            self.wire([(cx, y), (cx + 28, y)], color, 3, dashed)
            self.text(cx + 34, y + 4, label, 12, color=C["mute"])
            cx += 44 + 13 * len(label)

    def save(self, name):
        self.parts.append("</svg>")
        with open(os.path.join(OUT, name), "w", encoding="utf-8") as f:
            f.write("\n".join(self.parts))

    def raw(self, s):
        self.parts.append(s)


import math

C["bat"] = "#D23B2F"     # 7.4 V battery
C["flex"] = "#B5487A"    # flex sensor signals
C["legA"] = "#E07B00"    # tripod group A
C["legB"] = "#1F6FB5"    # tripod group B

LEGS = ["LF", "LM", "LR", "RF", "RM", "RR"]
LEG_CN = ["左前", "左中", "左后", "右前", "右中", "右后"]
MOUNT = [(60, 40, 45), (0, 50, 90), (-60, 40, 135), (60, -40, -45), (0, -50, -90), (-60, -40, -135)]
TRIPOD_A = {0, 2, 4}
GAITS = [  # name, duty, offsets (LF LM LR RF RM RR) - same as gait.h
    ("三角步态 TRIPOD", 0.5, [0, 0.5, 0, 0.5, 0, 0.5]),
    ("涟漪步态 RIPPLE", 2 / 3, [0, 1 / 3, 2 / 3, 0.5, 5 / 6, 1 / 6]),
    ("波浪步态 WAVE", 5 / 6, [3 / 6, 4 / 6, 5 / 6, 0, 1 / 6, 2 / 6]),
]


def stub(s, x, y, dx, label, color, anchor):
    s.wire([(x, y), (x + dx, y)], color, 2.5)
    s.tag(x + dx + (4 if dx > 0 else -4), y, label, color, anchor)


# ------------------------------------------------------------------------------------------ fig 0
def fig_overview():
    s = Svg(1100, 470, "图 0  系统总览：体感手套 → 六足机器人",
            "手指弯成的手势选模式，手掌的倾斜角度定速度；手套和机器人都是 ESP32，用 ESP-NOW 直连（不需要路由器）")
    s.box(40, 100, 300, 170, "体感手套", "XIAO ESP32S3（带锂电池充电）\nMPU6050 陀螺仪：手掌倾角\n5 × 弯曲传感器：手指弯曲\n"
          "按键：解锁 / 锁定、长按标定\n振动马达：反馈\n3.7 V 锂电池 + 开关", fill=C["hi"], title_dy=30)
    s.box(760, 100, 300, 170, "六足机器人（套件）", "ESP32 DevKit：步态 + 逆运动学\n舵机扩展板 + 开关\n18 × MG90S（每条腿 3 个）\n"
          "2 × 18650（7.4 V）\n电压检测模块：电量\n可选：HC-SR04 避障", fill="#E3F1F4", title_dy=30)
    s.arrow(345, 170, 752, 170, C["sig"], 3)
    s.text(548, 158, "ESP-NOW · 50 次 / 秒", 14, "middle", "700", C["sig"])
    s.text(548, 194, "模式（走 / 横移 / 机身 / 停）+ 速度 + 事件（起立 / 换步态）", 12, "middle", color=C["mute"])
    s.wire([(752, 240), (350, 240)], C["rf"], 2.5, True)
    s.arrow(360, 240, 345, 240, C["rf"], 2.5)
    s.text(548, 230, "状态 · 5 次 / 秒：电量低、前方有障碍 → 手套振动", 12, "middle", color=C["mute"])
    s.box(430, 262, 240, 60, "电脑（可选）", "USB 串口 115200：标定、测试指令", dashed=True, title_dy=24)
    s.wire([(670, 292), (820, 292), (820, 270)], C["sig"], 2.5, True)

    y = 370
    steps = [("手指弯曲", "5 个 ADC"), ("手势", "张开 / 食指 / 剪刀手…"), ("模式", "走 / 横移 / 机身"),
             ("手掌倾角", "死区 + 指数曲线"), ("步态", "三角 / 涟漪 / 波浪"), ("逆运动学", "每条腿 3 个角度"),
             ("18 路舵机", "PCA9685 或 GPIO")]
    x = 40
    for i, (a, b) in enumerate(steps):
        fill = C["hi"] if i < 4 else "#E3F1F4"
        s.box(x, y, 130, 70, a, b, fill=fill)
        if i < len(steps) - 1:
            s.arrow(x + 130, y + 35, x + 150, y + 35, C["ink"], 2)
        x += 150
    s.text(40, y - 12, "手套上算", 12, weight="700", color=C["mute"])
    s.text(640, y - 12, "机器人上算", 12, weight="700", color=C["mute"])
    s.save("fig0-overview.svg")


# ------------------------------------------------------------------------------------------ fig 1
def fig_glove():
    s = Svg(1100, 680, "图 1  手套接线（XIAO ESP32S3）",
            "标签颜色相同 = 接在一起。全部用杜邦线 + 迷你面包板，只有锂电池那两根线要焊在 XIAO 背面")
    x0, y0, w, h = 430, 110, 240, 330
    s.parts.append(f'<rect x="{x0}" y="{y0}" width="{w}" height="{h}" rx="12" fill="#E3F1F4" stroke="{C["boxline"]}" stroke-width="1.6"/>')
    s.parts.append(f'<rect x="{x0 + 95}" y="{y0 - 10}" width="50" height="22" rx="4" fill="#B8C4C8" stroke="{C["boxline"]}"/>')
    s.text(x0 + 120, y0 + 5, "USB-C", 10, "middle", "700")
    s.text(x0 + w / 2, y0 + 150, "XIAO ESP32S3", 16, "middle", "700")
    s.text(x0 + w / 2, y0 + 172, "正面朝上看", 12, "middle", color=C["mute"])
    s.text(x0 + w / 2, y0 + 300, "背面：BAT+ / BAT−", 12, "middle", "700", C["bat"])
    left = [("D0", "拇指", C["flex"]), ("D1", "食指", C["flex"]), ("D2", "中指", C["flex"]), ("D3", "无名指", C["flex"]),
            ("D4", "SDA", C["i2c"]), ("D5", "SCL", C["scl"] if "scl" in C else "#0E8C9A"), ("D6", None, None)]
    right = [("5V", None, None), ("GND", "GND", C["gnd"]), ("3V3", "3V3", C["v33"]), ("D10", "振动 IN", C["sig"]),
             ("D9", "按键", C["sig"]), ("D8", "小指", C["flex"]), ("D7", None, None)]
    for i, (pin, tag, col) in enumerate(left):
        y = y0 + 40 + i * 40
        s.pin(x0, y, pin, "left")
        if tag:
            stub(s, x0, y, -60, tag, col, "end")
    for i, (pin, tag, col) in enumerate(right):
        y = y0 + 40 + i * 40
        s.pin(x0 + w, y, pin, "right")
        if tag:
            stub(s, x0 + w, y, 60, tag, col, "start")
    s.text(x0 - 70, y0 + 40 + 6 * 40 + 4, "（不接）", 11, "end", color=C["mute"])
    s.text(x0 + w + 70, y0 + 40 + 6 * 40 + 4, "（不接）", 11, color=C["mute"])
    s.text(x0 + w + 70, y0 + 44, "（不接）", 11, color=C["mute"])

    # flex divider
    bx, by = 40, 490
    s.box(bx, by, 520, 170, "", fill="#FFFFFF")
    s.text(bx + 16, by + 26, "弯曲传感器分压（每根手指一样，共 5 组）", 14, weight="700")
    s.tag(bx + 20, by + 80, "3V3", C["v33"])
    s.wire([(bx + 62, by + 80), (bx + 100, by + 80)], C["v33"], 2.5)
    s.part(bx + 100, by + 66, 120, 28, "弯曲传感器", fill="#F6E3EC")
    s.wire([(bx + 220, by + 80), (bx + 300, by + 80)], C["flex"], 2.5)
    s.dot(bx + 300, by + 80, C["flex"])
    s.wire([(bx + 300, by + 80), (bx + 370, by + 80)], C["flex"], 2.5)
    s.tag(bx + 374, by + 80, "拇指 / 食指 …", C["flex"])
    s.wire([(bx + 300, by + 80), (bx + 300, by + 100)], C["flex"], 2.5)
    s.part(bx + 290, by + 100, 20, 36, "47 kΩ（或 2 × 100 kΩ 并联）", vertical=True)
    s.wire([(bx + 300, by + 136), (bx + 300, by + 150)], C["gnd"], 2.5)
    s.tag(bx + 282, by + 158, "GND", C["gnd"])
    s.text(bx + 16, by + 128, "弯曲 → 电阻变大", 12, color=C["mute"])
    s.text(bx + 16, by + 146, "→ 引脚电压变低", 12, color=C["mute"])

    # MPU6050
    mx, my = 790, 110
    s.box(mx, my, 270, 190, "MPU6050（GY-521）", "贴在手背，X 箭头指向手指，\n元件面朝外", title_dy=24)
    for i, (pin, tag, col) in enumerate([("VCC", "3V3", C["v33"]), ("GND", "GND", C["gnd"]), ("SCL", "SCL", "#0E8C9A"),
                                         ("SDA", "SDA", C["i2c"])]):
        y = my + 88 + i * 26
        s.text(mx + 16, y + 4, pin, 11.5, weight="600")
        s.tag(mx + 60, y, tag, col)
    s.text(mx + 150, my + 140, "XDA XCL AD0 INT", 11, color=C["mute"])
    s.text(mx + 150, my + 158, "不接", 11, color=C["mute"])
    # vibration
    vx, vy = 790, 320
    s.box(vx, vy, 270, 90, "振动马达模块", "", title_dy=24)
    for i, (pin, tag, col) in enumerate([("VCC", "3V3", C["v33"]), ("GND", "GND", C["gnd"]), ("IN", "振动 IN", C["sig"])]):
        s.text(vx + 16 + i * 86, vy + 64, pin, 11.5, weight="600")
        s.tag(vx + 16 + i * 86, vy + 80, tag, col)
    # button
    ux, uy = 790, 430
    s.box(ux, uy, 270, 80, "按键（元件包里的轻触开关）", "", title_dy=24)
    s.tag(ux + 30, uy + 56, "按键", C["sig"])
    s.text(ux + 120, uy + 60, "对角两脚", 11.5, "middle", color=C["mute"])
    s.tag(ux + 170, uy + 56, "GND", C["gnd"])
    # battery
    tx, ty = 620, 540
    s.box(tx, ty, 440, 120, "电源", "", title_dy=24)
    s.part(tx + 20, ty + 50, 130, 34, "3.7 V 锂电池", fill="#FBE3E0")
    s.wire([(tx + 150, ty + 60), (tx + 200, ty + 60)], C["bat"], 3)
    s.text(tx + 175, ty + 52, "红 +", 11, "middle", "700", C["bat"])
    s.part(tx + 200, ty + 46, 90, 28, "拨动开关")
    s.wire([(tx + 290, ty + 60), (tx + 340, ty + 60)], C["bat"], 3)
    s.text(tx + 346, ty + 64, "BAT+", 12, weight="700", color=C["bat"])
    s.wire([(tx + 150, ty + 76), (tx + 340, ty + 76), (tx + 340, ty + 96)], C["gnd"], 3)
    s.text(tx + 346, ty + 100, "BAT−", 12, weight="700")
    s.text(tx + 20, ty + 110, "插 USB-C 就给电池充电（XIAO 自带充电芯片）", 11.5, color=C["mute"])
    s.save("fig1-glove-wiring.svg")


# ------------------------------------------------------------------------------------------ fig 2
def fig_robot():
    s = Svg(1100, 700, "图 2  机器人接线（套件的 ESP32 + 舵机扩展板）",
            "舵机、电池照套件说明插好；本项目只加两样：电压检测模块（必做）和 HC-SR04（可选）")
    s.box(40, 110, 210, 120, "2 × 18650 电池盒", "套件自带 · 满电 8.4 V\n标称 7.4 V")
    s.box(330, 90, 400, 330, "舵机扩展板（套件）", "", fill="#E3F1F4")
    s.box(420, 140, 220, 90, "ESP32 DevKit", "插在扩展板中间\nUSB-C 接电脑", fill="#FFFFFF")
    s.text(530, 262, "18 路舵机插座（左右两排）", 13, "middle", "700")
    s.text(530, 282, "舵机线：棕 = GND · 红 = V+ · 橙 = 信号", 12, "middle", color=C["mute"])
    s.text(530, 312, "A 型：板上有 PCA9685 芯片（SCAN 看到 0x40）", 12, "middle", color=C["i2c"])
    s.text(530, 332, "B 型：插座直连 ESP32 引脚（SCAN 什么都没有）", 12, "middle", color=C["sig"])
    s.text(530, 362, "电源开关 · 板载降压给舵机（先用万用表量！）", 12, "middle", color=C["mute"])
    s.wire([(250, 150), (330, 150)], C["bat"], 3)
    s.wire([(250, 190), (330, 190)], C["gnd"], 3)
    s.text(290, 142, "+", 14, "middle", "700", C["bat"])
    s.text(290, 182, "−", 14, "middle", "700")
    s.text(145, 256, "电源开关后面并一路出来 →", 11.5, "middle", color=C["mute"])
    s.tag(150, 280, "VBAT+", C["bat"], "end")
    s.tag(160, 280, "GND", C["gnd"])

    # voltage module
    vx, vy = 800, 100
    s.box(vx, vy, 260, 150, "电压检测模块（学习套件）", "0–25 V · 5 : 1 分压", title_dy=24)
    s.text(vx + 16, vy + 76, "端子 VCC", 11.5, weight="600")
    s.tag(vx + 90, vy + 72, "VBAT+", C["bat"])
    s.text(vx + 16, vy + 100, "端子 GND", 11.5, weight="600")
    s.tag(vx + 90, vy + 96, "GND", C["gnd"])
    s.text(vx + 16, vy + 124, "S → GPIO34", 11.5, weight="600", color=C["sig"])
    s.text(vx + 120, vy + 124, "− → GND · + 不接", 11.5, color=C["mute"])
    s.text(vx + 16, vy + 142, "8.4 V → 1.68 V（ESP32 最高 3.3 V）", 11, color=C["mute"])

    # HC-SR04
    hx_, hy = 800, 280
    s.box(hx_, hy, 260, 200, "HC-SR04（可选，学习套件）", "装在套件的超声波支架上", title_dy=24)
    s.text(hx_ + 16, hy + 74, "VCC → 5V（VIN）  GND → GND", 11.5, weight="600")
    s.text(hx_ + 16, hy + 96, "TRIG → 空闲引脚（如 GPIO25）", 11.5, weight="600")
    s.text(hx_ + 16, hy + 118, "ECHO →", 11.5, weight="600")
    s.part(hx_ + 70, hy + 106, 60, 22, "1 kΩ")
    s.wire([(hx_ + 130, hy + 117), (hx_ + 160, hy + 117)], C["sig"], 2.5)
    s.dot(hx_ + 160, hy + 117, C["sig"])
    s.text(hx_ + 168, hy + 121, "GPIO26", 11.5, weight="700", color=C["sig"])
    s.wire([(hx_ + 160, hy + 117), (hx_ + 160, hy + 138)], C["sig"], 2.5)
    s.part(hx_ + 130, hy + 138, 60, 22, "2 kΩ")
    s.text(hx_ + 200, hy + 154, "→ GND", 11.5, weight="600")
    s.text(hx_ + 16, hy + 186, "ECHO 是 5 V，必须分压到 3.3 V", 11.5, weight="700", color=C["v12"])

    # channel table
    ty = 470
    s.text(40, ty, "默认舵机通道（CHMAP）：A 型板 0x40 = 0–15、0x41 = 16–31；B 型板写 GPIO 号", 14, weight="700")
    cols = ["腿", "髋 coxa", "大腿 femur", "小腿 tibia"]
    for j, c in enumerate(cols):
        s.text(60 + j * 150, ty + 32, c, 13, weight="700")
    for i in range(6):
        y = ty + 58 + i * 26
        s.text(60, y, f"{i} {LEGS[i]} {LEG_CN[i]}", 12.5, weight="600")
        for j in range(3):
            sidx = i * 3 + j
            ch = sidx if sidx < 9 else 16 + sidx - 9
            s.text(60 + (j + 1) * 150, y, str(ch), 12.5)
    s.text(700, ty + 60, "舵机编号 s = 腿 × 3 + 关节", 12.5, color=C["mute"])
    s.text(700, ty + 86, "SCAN 看板子是哪一型，", 12.5, color=C["mute"])
    s.text(700, ty + 112, "再按板上丝印用 CHMAP 一次写 18 个", 12.5, color=C["mute"])
    s.save("fig2-robot-wiring.svg")


# ------------------------------------------------------------------------------------------ fig 3
def hand(s, x, y, fingers, thumb, color="#F4D9C0"):
    """Back of a right hand, fingers up. fingers = [index, middle, ring, little] True = straight."""
    line = C["boxline"]
    lens = [52, 60, 54, 42]
    for k, st in enumerate(fingers):
        fx = x + 4 + k * 18
        L = lens[k] if st else 14
        s.raw(f'<rect x="{fx}" y="{y - L}" width="15" height="{L + 10}" rx="7" fill="{color}" stroke="{line}" stroke-width="1.4"/>')
    s.raw(f'<rect x="{x}" y="{y}" width="76" height="70" rx="12" fill="{color}" stroke="{line}" stroke-width="1.4"/>')
    if thumb:
        s.raw(f'<rect x="{x - 40}" y="{y + 16}" width="48" height="15" rx="7" fill="{color}" stroke="{line}" '
              f'stroke-width="1.4" transform="rotate(-35 {x + 4} {y + 24})"/>')
    else:
        s.raw(f'<rect x="{x - 8}" y="{y + 30}" width="30" height="15" rx="7" fill="{color}" stroke="{line}" stroke-width="1.4"/>')


def fig_gestures():
    s = Svg(1100, 600, "图 3  手势表（右手，手背朝上看）",
            "手指只分“直”和“弯”。走 / 横移 / 机身三种要保持 0.15 秒才生效；握拳立刻停")
    cards = [
        ("张开手", "行走 WALK", "前倾 = 前进 · 后仰 = 后退\n左右翻 = 左转 / 右转", [1, 1, 1, 1], True, C["legA"]),
        ("伸食指", "横移 CRAB", "前倾 = 前进 · 后仰 = 后退\n左右翻 = 向左 / 向右平移", [1, 0, 0, 0], False, C["legA"]),
        ("剪刀手", "机身 BODY", "脚不动，机身跟着手掌\n前后、左右倾斜\n（最多 10°）", [1, 1, 0, 0], False, C["legA"]),
        ("握拳", "停 STOP", "立刻停下站好\n（手套锁定时也一样）", [0, 0, 0, 0], False, C["v12"]),
        ("竖大拇指 1 秒", "起立 / 趴下", "趴着 → 站起来\n站着 → 趴下、舵机断电\n不装拇指传感器时：\n改成只伸小指", [0, 0, 0, 0], True, C["sig"]),
        ("摇滚手势 0.6 秒", "换步态", "三角 → 涟漪 → 波浪\n→ 三角 …\n（停下来以后才换）", [1, 0, 0, 1], False, C["sig"]),
    ]
    for n, (name, act, desc, f, th, col) in enumerate(cards):
        cx = 40 + (n % 3) * 350
        cy = 100 + (n // 3) * 240
        s.box(cx, cy, 330, 220, "", fill="#FFFFFF")
        s.raw(f'<rect x="{cx}" y="{cy}" width="8" height="220" rx="4" fill="{col}"/>')
        hand(s, cx + 70, cy + 110, [bool(v) for v in f], th)
        s.text(cx + 180, cy + 50, name, 16, weight="700")
        s.text(cx + 180, cy + 78, act, 15, weight="700", color=col)
        for i, line in enumerate(desc.split("\n")):
            s.text(cx + 180, cy + 108 + i * 20, line, 12, color=C["mute"])
    s.save("fig3-gestures.svg")


# ------------------------------------------------------------------------------------------ fig 4
def robot_top(s, cx, cy, k=0.45, color="#DDE6EA"):
    pts = []
    for (mx, my, a) in MOUNT:
        pts.append((cx - my * k * 1.3, cy - mx * k * 1.3))
    order = [0, 3, 4, 5, 2, 1]
    d = " ".join(f"{'M' if i == 0 else 'L'}{pts[j][0]:.1f},{pts[j][1]:.1f}" for i, j in enumerate(order)) + " Z"
    for (mx, my, a) in MOUNT:
        ex = mx + 60 * math.cos(math.radians(a))
        ey = my + 60 * math.sin(math.radians(a))
        s.wire([(cx - my * k * 1.3, cy - mx * k * 1.3), (cx - ey * k * 1.3, cy - ex * k * 1.3)], C["boxline"], 3)
    s.raw(f'<path d="{d}" fill="{color}" stroke="{C["boxline"]}" stroke-width="1.6"/>')
    s.raw(f'<path d="M{cx},{cy - 22} L{cx - 7},{cy - 10} L{cx + 7},{cy - 10} Z" fill="{C["ink"]}"/>')


def curved(s, cx, cy, r, cw, color):
    a0, a1 = (-150, -30) if cw else (-30, -150)
    x0, y0 = cx + r * math.cos(math.radians(a0)), cy + r * math.sin(math.radians(a0))
    x1, y1 = cx + r * math.cos(math.radians(a1)), cy + r * math.sin(math.radians(a1))
    s.raw(f'<path d="M{x0:.1f},{y0:.1f} A{r},{r} 0 0 {1 if cw else 0} {x1:.1f},{y1:.1f}" fill="none" stroke="{color}" stroke-width="3"/>')
    a = math.radians(a1 + (90 if cw else -90))
    p1 = (x1 - 10 * math.cos(a - 0.45), y1 - 10 * math.sin(a - 0.45))
    p2 = (x1 - 10 * math.cos(a + 0.45), y1 - 10 * math.sin(a + 0.45))
    s.raw(f'<path d="M{x1:.1f},{y1:.1f} L{p1[0]:.1f},{p1[1]:.1f} L{p2[0]:.1f},{p2[1]:.1f} Z" fill="{color}"/>')


def side_hand(s, x, y, ang, label):
    """Hand seen from the thumb side, tilted by ang (deg, + = fingertips down)."""
    s.raw(f'<g transform="rotate({ang} {x} {y})"><rect x="{x - 45}" y="{y - 9}" width="90" height="18" rx="9" '
          f'fill="#F4D9C0" stroke="{C["boxline"]}" stroke-width="1.4"/><circle cx="{x - 20}" cy="{y - 13}" r="5" fill="{C["i2c"]}"/></g>')
    s.text(x, y + 44, label, 11.5, "middle", color=C["mute"])


def back_hand(s, x, y, ang, label):
    """Hand seen from behind the wrist, rolled by ang (deg, + = right side / little finger down)."""
    s.raw(f'<g transform="rotate({ang} {x} {y})"><rect x="{x - 40}" y="{y - 8}" width="80" height="16" rx="8" '
          f'fill="#F4D9C0" stroke="{C["boxline"]}" stroke-width="1.4"/></g>')
    s.text(x, y + 44, label, 11.5, "middle", color=C["mute"])


def fig_tilt():
    s = Svg(1100, 620, "图 4  手掌倾角 → 机器人动作",
            "倾角从标定时“手掌放平”的姿态算起；小于 8° 不动（死区），35° 最快，中间是平滑的指数曲线")
    x0 = 40
    s.text(x0 + 160, 100, "前后倾（从拇指一侧看，手指朝右）", 13, "middle", "700")
    side_hand(s, x0 + 70, 160, 25, "指尖朝下 = 前倾")
    side_hand(s, x0 + 250, 160, -25, "指尖朝上 = 后仰")
    s.text(x0 + 160, 290, "左右翻（从手腕往前看）", 13, "middle", "700")
    back_hand(s, x0 + 70, 340, -25, "拇指侧朝下 = 左翻")
    back_hand(s, x0 + 250, 340, 25, "小指侧朝下 = 右翻")

    cols = [("张开手 · 行走", "walk"), ("伸食指 · 横移", "crab"), ("剪刀手 · 机身", "body")]
    for n, (title, kind) in enumerate(cols):
        bx = 400 + n * 230
        s.box(bx, 80, 210, 520, title, fill="#FFFFFF", title_dy=26)
        rows = [("前倾", 150), ("后仰", 270), ("右翻", 390), ("左翻", 510)]
        for lab, y in rows:
            s.text(bx + 14, y + 4, lab, 12.5, weight="700")
            cx, cy = bx + 120, y
            if kind == "body":
                if lab in ("前倾", "后仰"):
                    tilt = 12 if lab == "前倾" else -12
                    s.raw(f'<g transform="rotate({tilt} {cx} {cy})"><rect x="{cx - 50}" y="{cy - 8}" width="100" height="16" rx="5" '
                          f'fill="#DDE6EA" stroke="{C["boxline"]}" stroke-width="1.4"/></g>')
                    s.text(cx, cy + 40, "机头（右）低下" if lab == "前倾" else "机头（右）抬起", 11.5, "middle", color=C["mute"])
                else:
                    tilt = 12 if lab == "右翻" else -12
                    s.raw(f'<g transform="rotate({tilt} {cx} {cy})"><rect x="{cx - 40}" y="{cy - 8}" width="80" height="16" rx="5" '
                          f'fill="#DDE6EA" stroke="{C["boxline"]}" stroke-width="1.4"/></g>')
                    s.text(cx, cy + 40, "右侧低（后视）" if lab == "右翻" else "左侧低（后视）", 11.5, "middle", color=C["mute"])
                continue
            robot_top(s, cx, cy, 0.32)
            if lab == "前倾":
                s.arrow(cx + 55, cy + 15, cx + 55, cy - 30, C["legA"], 3)
                s.text(cx, cy + 50, "前进", 11.5, "middle", color=C["mute"])
            elif lab == "后仰":
                s.arrow(cx + 55, cy - 15, cx + 55, cy + 30, C["legA"], 3)
                s.text(cx, cy + 50, "后退", 11.5, "middle", color=C["mute"])
            elif kind == "walk":
                curved(s, cx, cy, 44, lab == "右翻", C["legA"])
                s.text(cx, cy + 50, "原地右转（顺时针）" if lab == "右翻" else "原地左转（逆时针）", 11.5, "middle", color=C["mute"])
            else:
                d = 1 if lab == "右翻" else -1
                s.arrow(cx + d * 30, cy - 34, cx + d * 75, cy - 34, C["legA"], 3)
                s.text(cx, cy + 50, "向右平移" if lab == "右翻" else "向左平移", 11.5, "middle", color=C["mute"])
    s.text(40, 470, "前倾 + 右翻一起做：", 13, weight="700")
    s.text(40, 492, "行走模式下就是边走边右转（走弧线）", 12.5, color=C["mute"])
    s.text(40, 530, "方向反了？", 13, weight="700")
    s.text(40, 552, "手套串口输入 AXIS -1 1（前后反）", 12.5, color=C["mute"])
    s.text(40, 574, "或 AXIS 1 -1（左右反），再 SAVE", 12.5, color=C["mute"])
    s.save("fig4-tilt.svg")


# ------------------------------------------------------------------------------------------ fig 5
def fig_leg():
    s = Svg(1100, 560, "图 5  一条腿：尺寸、角度方向、标定姿态",
            "标定姿态（全部关节 = 0°）：髋关节垂直于机身侧边，大腿水平，小腿竖直向下")
    # side view
    bx, by = 90, 200          # coxa axis top
    k = 2.2
    L1, L2, L3 = 28 * k, 45 * k, 75 * k
    s.raw(f'<rect x="{bx - 60}" y="{by - 20}" width="60" height="40" rx="4" fill="#DDE6EA" stroke="{C["boxline"]}" stroke-width="1.6"/>')
    s.text(bx - 30, by + 40, "机身", 12, "middle", color=C["mute"])
    s.wire([(bx, by - 45), (bx, by + 45)], C["mute"], 1.5, True)
    s.text(bx, by - 52, "髋轴（竖直）", 11.5, "middle", color=C["mute"])
    fx = bx + L1
    kx = fx + L2
    s.wire([(bx, by), (fx, by)], C["legB"], 9)
    s.wire([(fx, by), (kx, by)], C["legA"], 9)
    s.wire([(kx, by), (kx, by + L3)], C["i2c"], 9)
    for (x, y) in [(bx, by), (fx, by), (kx, by)]:
        s.raw(f'<circle cx="{x}" cy="{y}" r="8" fill="#FFFFFF" stroke="{C["ink"]}" stroke-width="2.5"/>')
    s.raw(f'<circle cx="{kx}" cy="{by + L3}" r="6" fill="{C["ink"]}"/>')
    s.wire([(bx - 70, by + L3 + 8), (kx + 160, by + L3 + 8)], C["gnd"], 2)
    s.text(bx - 60, by + L3 + 28, "地面", 12, color=C["mute"])
    s.text((bx + fx) / 2, by - 16, "L1 髋", 12.5, "middle", "700", C["legB"])
    s.text((fx + kx) / 2, by - 16, "L2 大腿", 12.5, "middle", "700", C["legA"])
    s.text(kx + 14, by + L3 / 2, "L3 小腿", 12.5, weight="700", color=C["i2c"])
    s.text(kx + 14, by + L3 / 2 + 20, "（量到脚尖）", 11.5, color=C["mute"])
    # angle arrows
    s.arrow(fx + 60, by - 8, fx + 55, by - 40, C["legA"], 2.2)
    s.text(fx + 64, by - 44, "q1 大腿 +：抬起", 12, weight="700", color=C["legA"])
    s.arrow(kx + 8, by + L3 - 30, kx + 40, by + L3 - 40, C["i2c"], 2.2)
    s.text(kx + 44, by + L3 - 40, "q2 小腿 +：脚往外张", 12, weight="700", color=C["i2c"])
    s.text(bx - 60, by + L3 + 70, "height = 髋平面离地高度（站立 55 mm）", 12.5, color=C["mute"])
    s.text(bx - 60, by + L3 + 92, "sit_height = 趴下时（机身贴地）", 12.5, color=C["mute"])
    s.wire([(bx - 45, by), (bx - 45, by + L3 + 8)], C["mute"], 1.5, True)
    s.text(bx - 50, by + L3 / 2 + 40, "height", 11.5, "end", color=C["mute"])

    # top view
    tx, ty = 760, 330
    s.raw(f'<rect x="{tx - 120}" y="{ty - 150}" width="80" height="300" rx="8" fill="#DDE6EA" stroke="{C["boxline"]}" stroke-width="1.6"/>')
    s.text(tx - 80, ty + 175, "机身（俯视，左侧）", 12, "middle", color=C["mute"])
    s.wire([(tx - 40, ty), (tx + 230, ty)], C["mute"], 1.5, True)
    s.text(tx + 234, ty + 4, "安装方向（q0 = 0）", 11.5, color=C["mute"])
    a = math.radians(-25)
    ex, ey = tx - 40 + 200 * math.cos(a), ty + 200 * math.sin(a)
    s.wire([(tx - 40, ty), (ex, ey)], C["legB"], 8)
    s.raw(f'<circle cx="{tx - 40}" cy="{ty}" r="8" fill="#FFFFFF" stroke="{C["ink"]}" stroke-width="2.5"/>')
    s.raw(f'<circle cx="{ex:.1f}" cy="{ey:.1f}" r="6" fill="{C["ink"]}"/>')
    s.raw(f'<path d="M{tx + 90},{ty} A130,130 0 0 0 {tx - 40 + 130 * math.cos(a):.1f},{ty + 130 * math.sin(a):.1f}" fill="none" stroke="{C["legB"]}" stroke-width="2.2"/>')
    s.text(tx + 100, ty - 22, "q0 髋 +：逆时针（俯视）", 12, weight="700", color=C["legB"])
    s.text(tx + 20, ty + 40, "reach = 脚尖到髋轴的水平距离（默认 75 mm）", 12, color=C["mute"])
    s.text(640, 120, "舵机脉宽 = 1500 + trim + dir × 11.1 µs/° × 角度", 13.5, weight="700")
    s.text(640, 142, "trim：标定姿态下对准；dir：转反了就改成 −1", 12.5, color=C["mute"])
    s.save("fig5-leg.svg")


# ------------------------------------------------------------------------------------------ fig 6
def fig_body():
    s = Svg(1100, 640, "图 6  腿的编号、安装位置和三角步态分组（俯视，机头朝上）",
            "橙色三条腿一起抬、蓝色三条腿一起抬；任何时候都有 3 只脚撑成三角形")
    cx, cy, k = 400, 350, 1.8
    def P(x, y):
        return cx - y * k, cy - x * k
    order = [0, 3, 4, 5, 2, 1]
    d = " ".join(f"{'M' if i == 0 else 'L'}{P(MOUNT[j][0], MOUNT[j][1])[0]:.1f},{P(MOUNT[j][0], MOUNT[j][1])[1]:.1f}"
                 for i, j in enumerate(order)) + " Z"
    for i, (mx, my, a) in enumerate(MOUNT):
        col = C["legA"] if i in TRIPOD_A else C["legB"]
        fx = mx + 75 * math.cos(math.radians(a))
        fy = my + 75 * math.sin(math.radians(a))
        s.wire([P(mx, my), P(fx, fy)], col, 8)
        px, py = P(fx, fy)
        s.raw(f'<circle cx="{px:.1f}" cy="{py:.1f}" r="9" fill="{col}"/>')
        lx, ly = P(mx + 100 * math.cos(math.radians(a)), my + 100 * math.sin(math.radians(a)))
        s.text(lx, ly + 5, f"{i} {LEGS[i]}", 14, "middle", "700", col)
        s.text(lx, ly + 23, f"({mx}, {my}, {a}°)", 11, "middle", color=C["mute"])
    s.raw(f'<path d="{d}" fill="#DDE6EA" stroke="{C["boxline"]}" stroke-width="1.8"/>')
    for (mx, my, a) in MOUNT:
        px, py = P(mx, my)
        s.raw(f'<circle cx="{px:.1f}" cy="{py:.1f}" r="7" fill="#FFFFFF" stroke="{C["ink"]}" stroke-width="2.2"/>')
    s.arrow(cx, cy, cx, cy - 90, C["v12"], 3)
    s.text(cx + 8, cy - 80, "x 前", 13, weight="700", color=C["v12"])
    s.arrow(cx, cy, cx - 90, cy, C["i2c"], 3)
    s.text(cx - 88, cy - 10, "y 左", 13, weight="700", color=C["i2c"])
    s.text(cx + 8, cy + 20, "原点：机身中心", 11.5, color=C["mute"])
    x0 = 740
    s.text(x0, 120, "MOUNT 腿号 x y 角度", 14, weight="700")
    lines = ["x, y：髋轴在机身上的位置（mm）", "角度：这条腿伸出去的方向",
             "（0° = 正前，90° = 正左，−90° = 正右）", "", "量法（组装指南第 5 步）：",
             "1. 找机身中心（对角线交点）", "2. 量每个髋轴到中心的前后距离 x", "   和左右距离 y（左正右负）",
             "3. 角度：看套件结构，一般前后腿 ±45°、", "   ±135°，中间腿 ±90°", "",
             "括号里是默认值，按你的套件改。", "差 5 mm 以内问题不大。"]
    for i, t in enumerate(lines):
        s.text(x0, 150 + i * 24, t, 12.5, color=C["mute"] if i else C["ink"])
    s.save("fig6-body.svg")


# ------------------------------------------------------------------------------------------ fig 7
def fig_gaits():
    s = Svg(1100, 640, "图 7  三种步态的时序（两个周期，色块 = 这条腿在空中）",
            "三角最快、最不稳；波浪最慢、最稳（5 只脚着地）。默认周期：三角 0.9 s、涟漪 1.2 s、波浪 2.0 s")
    x0, w = 160, 860
    for n, (name, duty, off) in enumerate(GAITS):
        y0 = 95 + n * 172
        s.text(40, y0 + 14, name, 14, weight="700")
        s.text(40, y0 + 34, f"着地比例 β = {duty:.2f}", 12, color=C["mute"])
        for i in range(6):
            y = y0 + 46 + i * 18
            s.text(x0 - 12, y + 12, LEGS[i], 11.5, "end", "600")
            s.raw(f'<rect x="{x0}" y="{y}" width="{w}" height="14" fill="#F3F6F5" stroke="#C9D3D6" stroke-width="0.8"/>')
            col = C["legA"] if i in TRIPOD_A else C["legB"]
            for cyc in range(-1, 2):
                a = cyc + ((duty - off[i]) % 1.0)
                b = a + (1 - duty)
                a2, b2 = max(a, 0), min(b, 2)
                if b2 > a2:
                    s.raw(f'<rect x="{x0 + a2 * w / 2:.1f}" y="{y}" width="{(b2 - a2) * w / 2:.1f}" height="14" fill="{col}"/>')
        for t in range(0, 5):
            x = x0 + t * w / 4
            s.wire([(x, y0 + 42), (x, y0 + 46 + 6 * 18)], "#9AA8AC", 1)
            s.text(x, y0 + 46 + 6 * 18 + 14, f"{t / 2:g}", 11, "middle", color=C["mute"])
    s.text(x0 + w / 2, 625, "时间（周期）", 12, "middle", color=C["mute"])
    s.save("fig7-gaits.svg")


if __name__ == "__main__":
    fig_overview()
    fig_glove()
    fig_robot()
    fig_gestures()
    fig_tilt()
    fig_leg()
    fig_body()
    fig_gaits()
    print("ok")
