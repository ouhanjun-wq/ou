#!/usr/bin/env python3
"""Generate the wiring / layout diagrams (SVG) used by the hexapod docs.

    python hexapod/docs/img/make_diagrams.py

Pure standard library; edit the drawing functions below and re-run.
"""
import math
import os
from xml.sax.saxutils import escape

OUT = os.path.dirname(os.path.abspath(__file__))
FONT = "'PingFang SC','Microsoft YaHei','Noto Sans CJK SC','WenQuanYi Zen Hei',sans-serif"

C = {
    "bat": "#D23B2F",    # battery / VIN (9.9-12.6 V)
    "v5": "#E07B00",     # 5 V servo rail
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

    def box(self, x, y, w, h, title, sub="", fill=None, dashed=False, align="middle"):
        dash = ' stroke-dasharray="6 4"' if dashed else ""
        self.parts.append(
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="8" fill="{fill or C["box"]}" '
            f'stroke="{C["boxline"]}" stroke-width="1.6"{dash}/>')
        self.text(x + w / 2, y + 22, title, 14, "middle", "700")
        if sub:
            tx = x + w / 2 if align == "middle" else x + 12
            for i, line in enumerate(sub.split("\n")):
                self.text(tx, y + 42 + i * 17, line, 11.5, align, color=C["mute"])

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
        self.wire([(x1, y1), (x2, y2)], color, width)
        a = math.atan2(y2 - y1, x2 - x1)
        p1 = (x2 - 10 * math.cos(a - 0.45), y2 - 10 * math.sin(a - 0.45))
        p2 = (x2 - 10 * math.cos(a + 0.45), y2 - 10 * math.sin(a + 0.45))
        self.parts.append(f'<path d="M{x2},{y2} L{p1[0]:.1f},{p1[1]:.1f} L{p2[0]:.1f},{p2[1]:.1f} Z" fill="{color}"/>')

    def dot(self, x, y, color):
        self.parts.append(f'<circle cx="{x}" cy="{y}" r="5" fill="{color}"/>')

    def tag(self, x, y, label, color, anchor="start"):
        """Net label: a coloured pill meaning 'connect to the net of this name'."""
        w = 10 + 7.0 * sum(2 if ord(ch) > 127 and ch not in "²·→←Ω" else 1 for ch in label)
        rx = x if anchor == "start" else x - w
        self.parts.append(f'<rect x="{rx}" y="{y - 11}" width="{w}" height="22" rx="11" fill="{color}"/>')
        self.text(rx + w / 2, y + 4.5, label, 11.5, "middle", "700", "#FFFFFF")

    def part(self, x, y, w, h, label, fill="#FFFFFF"):
        """Small component (resistor, capacitor, diode, fuse) drawn as a labelled block."""
        self.parts.append(
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="3" fill="{fill}" stroke="{C["boxline"]}" '
            f'stroke-width="1.4"/>')
        self.text(x + w / 2, y + h / 2 + 4, label, 11, "middle", "600")

    def note(self, x, y, n, s):
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


def xiao(s, x, y, title="XIAO ESP32S3 Plus", sub="俯视，USB-C 朝上"):
    """Seeed XIAO ESP32S3 drawn with its real pin order.
    Left column top->bottom: D0..D6, right column: 5V, GND, 3V3, D10, D9, D8, D7."""
    w, h = 170, 330
    s.box(x, y, w, h, title, sub, fill=C["hi"])
    s.parts.append(f'<rect x="{x + w / 2 - 22}" y="{y - 10}" width="44" height="14" rx="4" fill="#B9C4C1"/>')
    s.text(x + w / 2, y + 1, "USB-C", 9.5, "middle", "700")
    left = ["D0", "D1", "D2", "D3", "D4", "D5", "D6"]
    right = ["5V", "GND", "3V3", "D10", "D9", "D8", "D7"]
    pins = {}
    for i, n in enumerate(left):
        py = y + 80 + i * 38
        s.pin(x, py, n, "left")
        pins[n] = (x, py)
    for i, n in enumerate(right):
        py = y + 80 + i * 38
        s.pin(x + w, py, n, "right")
        pins[n] = (x + w, py)
    return pins


# ---------------------------------------------------------------------------
def fig_overview():
    s = Svg(1100, 560, "图 0  系统总览",
            "电脑上跑 ROS 2（建图、导航、识别），机器人上的 XIAO ESP32S3 Plus 通过 Wi-Fi 用 micro-ROS 连过来")
    s.box(40, 100, 330, 130, "电脑：Ubuntu 24.04 + Docker",
          "ROS 2 Jazzy（Seeker 的 Docker 容器）\nmicro-ROS Agent  UDP 8888\nSLAM Toolbox 建图 · Nav2 导航\nYOLO 识别 · 语音指令（可选）")
    s.box(460, 120, 170, 90, "Wi-Fi 路由器", "2.4 GHz，同一网段")
    s.box(720, 100, 340, 130, "XIAO ESP32S3 Plus（机器人上）",
          "micro-ROS 客户端（Wi-Fi）\n步态：/cmd_vel → 12 个舵机角度\n雷达 · IMU · 电池电压\n摄像头：可选卫星板（Sense / ESP32-CAM）", fill=C["hi"])
    s.wire([(370, 165), (460, 165)], C["rf"], 3, True)
    s.wire([(630, 165), (720, 165)], C["rf"], 3, True)
    s.text(545, 108, "micro-ROS UDP 8888 + HTTP", 12, "middle", "700")
    for i, t in enumerate(("↓ /cmd_vel  /mcu/hexapod_cmd", "↑ /mcu/imu  /mcu/scan", "↑ /mcu/battery_voltage  /mcu/heartbeat")):
        s.text(205, 256 + i * 18, t, 12, "middle", color=C["mute"])
    mods = [
        (40, "PCA9685", "16 路舵机驱动\nI²C 0x40", "12 × MG90S 舵机"),
        (220, "BNO085", "9 轴姿态\nI²C 0x4B · INT", ""),
        (400, "LD14P 雷达", "360° 8 m\nUART 230400", ""),
        (580, "OLED 0.96″", "SSD1306\nI²C 0x3C", ""),
        (760, "MAX98357A", "I²S 功放\n+ 3 W 喇叭", ""),
        (940, "WS2812", "RGB 灯\n（可选）", ""),
    ]
    for x, t, sub, extra in mods:
        s.box(x, 400, 140, 90, t, sub)
        s.wire([(x + 70, 400), (x + 70, 360), (890, 360), (890, 230)], C["sig"], 2)
        if extra:
            s.text(x + 70, 512, extra, 12, "middle", "700", C["v5"])
    s.dot(890, 360, C["sig"])
    s.text(900, 300, "机器人内部接线（见图 1、图 2）", 12, "start", "700", C["sig"])
    s.legend(40, 545, [("无线", C["rf"], True), ("有线信号", C["sig"], False)])
    s.save("fig0-overview.svg")


def fig_power():
    s = Svg(1100, 800, "图 1  电源：电池 → 保险丝 → 开关 → 5 V 降压 → 舵机 / 雷达 / 主控",
            "彩色标签表示“接到同名的线”。舵机电流大，红线和黑线用 20 AWG 以上的硅胶线")
    # battery
    s.box(40, 100, 150, 120, "3S 锂电池", "11.1 V 1000–1300 mAh\nXT30 插头")
    s.pin(190, 140, "+", "right", C["bat"])
    s.pin(190, 190, "−", "right", C["gnd"])
    s.part(215, 128, 60, 24, "XT30")
    s.part(300, 128, 90, 24, "保险丝 5 A")
    s.part(415, 128, 80, 24, "船型开关")
    s.wire([(190, 140), (215, 140)], C["bat"])
    s.wire([(275, 140), (300, 140)], C["bat"])
    s.wire([(390, 140), (415, 140)], C["bat"])
    s.wire([(495, 140), (1060, 140)], C["bat"], 4)
    s.tag(1060, 116, "VIN 9.9–12.6 V", C["bat"], "end")
    s.wire([(190, 190), (240, 190), (240, 640)], C["gnd"])
    s.dot(240, 640, C["gnd"])
    s.wire([(40, 640), (1060, 640)], C["gnd"], 4)
    s.tag(1060, 664, "GND（所有模块共地）", C["gnd"], "end")
    # battery divider -> D3
    s.dot(540, 140, C["bat"])
    s.wire([(540, 140), (540, 170)], C["bat"], 2)
    s.part(500, 170, 80, 24, "30 kΩ")
    s.wire([(540, 194), (540, 240)], C["sig"], 2)
    s.dot(540, 215, C["sig"])
    s.wire([(540, 215), (490, 215)], C["sig"], 2)
    s.tag(490, 215, "电压检测模块 S → XIAO D3", C["sig"], "end")
    s.part(500, 240, 80, 24, "7.5 kΩ")
    s.wire([(540, 264), (540, 290)], C["gnd"], 2)
    s.tag(530, 290, "GND", C["gnd"], "end")
    # buck
    s.box(640, 190, 220, 110, "5 V 降压模块", "12 V → 5 V 5 A 固定输出\n先量 5.0–5.2 V 再接！")
    s.pin(680, 190, "IN+", "top", C["bat"])
    s.pin(820, 190, "IN−", "top", C["gnd"])
    s.pin(700, 300, "OUT+", "bottom", C["v5"])
    s.pin(820, 300, "OUT−", "bottom", C["gnd"])
    s.wire([(680, 190), (680, 140)], C["bat"])
    s.dot(680, 140, C["bat"])
    s.wire([(820, 190), (820, 170), (880, 170), (880, 640)], C["gnd"])
    s.dot(880, 640, C["gnd"])
    s.wire([(820, 300), (820, 330), (880, 330)], C["gnd"])
    s.dot(880, 330, C["gnd"])
    s.wire([(700, 300), (700, 380)], C["v5"])
    s.wire([(40, 380), (1060, 380)], C["v5"], 4)
    s.dot(700, 380, C["v5"])
    s.tag(40, 356, "5V_SV 舵机电源 5.0–5.2 V", C["v5"])
    # loads on the 5 V rail
    loads = [
        (40, "PCA9685 V+", "舵机电源端子（绿色螺丝端子）\n旁边并一个 1000 µF 电容"),
        (250, "LD14P 雷达", "5V 脚（约 300 mA）"),
        (430, "MAX98357A", "VIN 脚"),
        (610, "WS2812 灯", "5V 脚（可选）"),
    ]
    for x, t, sub in loads:
        w = 190 if x == 40 else 160
        s.box(x, 440, w, 100, t, sub)
        s.pin(x + 30, 440, "+", "top", C["v5"])
        s.pin(x + w - 30, 540, "−", "bottom", C["gnd"])
        s.wire([(x + 30, 440), (x + 30, 380)], C["v5"])
        s.dot(x + 30, 380, C["v5"])
        s.wire([(x + w - 30, 540), (x + w - 30, 640)], C["gnd"])
        s.dot(x + w - 30, 640, C["gnd"])
    # XIAO via Schottky diode
    s.wire([(925, 380), (925, 405)], C["v5"])
    s.dot(925, 380, C["v5"])
    s.part(900, 405, 80, 24, "1N5819 ▶|")
    s.text(990, 421, "条纹朝下", 11, "start", color=C["mute"])
    s.wire([(925, 429), (925, 460)], C["v5"])
    s.box(900, 460, 170, 110, "", "")
    s.text(1000, 488, "XIAO ESP32S3", 14, "middle", "700")
    s.text(1000, 510, "5V 脚进电", 11.5, "middle", color=C["mute"])
    s.text(1000, 527, "3V3 脚出电", 11.5, "middle", color=C["mute"])
    s.pin(925, 460, "5V", "top", C["v5"])
    s.pin(1040, 570, "GND", "bottom", C["gnd"])
    s.wire([(1040, 570), (1040, 640)], C["gnd"])
    s.dot(1040, 640, C["gnd"])
    s.pin(920, 570, "3V3", "bottom", C["v33"])
    s.wire([(920, 570), (920, 600)], C["v33"], 2)
    s.tag(915, 600, "3V3 → PCA9685 VCC · BNO085 VIN · OLED VCC", C["v33"], "end")
    # notes
    s.note(40, 700, 1, "降压模块先单独通电，万用表确认 5.0–5.2 V，再接任何负载")
    s.note(40, 728, 2, "1N5819 防止插着 USB 时电流倒灌进舵机电源（Seeker 原版 PCB 也是这样做的）")
    s.note(40, 756, 3, "PCA9685 的 VCC 接 3V3（逻辑电），V+ 接 5V_SV（舵机电），两个不要接反")
    s.note(600, 700, 4, "电压检测模块：VIN × 7.5 / 37.5 → 12.6 V 时 D3 = 2.52 V")
    s.note(600, 728, 5, "通电前先量 VIN、5V_SV 和 GND 之间不短路")
    s.legend(600, 758, [("VIN", C["bat"], False), ("5 V", C["v5"], False), ("3.3 V", C["v33"], False),
                        ("GND", C["gnd"], False)])
    s.save("fig1-power.svg")


def fig_signals():
    s = Svg(1100, 760, "图 2  XIAO ESP32S3 Plus 引脚接线（D0–D10 和 Sense 相同，对应固件 ENV_ESP32S3SENSE）",
            "每个引脚右边 / 左边的标签 = 这根线另一头接到哪里；I²C 三个模块并联在同一对 SDA / SCL 上")
    p = xiao(s, 465, 110)
    left = {
        "D0": ("PCA9685 OE（10 kΩ 上拉到 3V3）", C["sig"]),
        "D1": ("BNO085 INT", C["sig"]),
        "D2": ("WS2812 DIN（串 330 Ω，可选）", C["sig"]),
        "D3": ("电压检测模块 S 脚（30k / 7.5k 分压）", C["sig"]),
        "D4": ("SDA：PCA9685 · BNO085 · OLED", C["i2c"]),
        "D5": ("SCL：PCA9685 · BNO085 · OLED", C["i2c"]),
        "D6": ("TX → LD14P 的 RX", C["sig"]),
    }
    right = {
        "5V": ("5V_SV 经 1N5819（图 1）", C["v5"]),
        "GND": ("公共地", C["gnd"]),
        "3V3": ("PCA9685 VCC · BNO085 VIN · OLED VCC", C["v33"]),
        "D10": ("MAX98357A DIN", C["sig"]),
        "D9": ("MAX98357A LRC", C["sig"]),
        "D8": ("MAX98357A BCLK", C["sig"]),
        "D7": ("RX ← LD14P 的 TX", C["sig"]),
    }
    for n, (lab, col) in left.items():
        x, y = p[n]
        s.wire([(x, y), (x - 40, y)], col, 2.5)
        s.tag(x - 40, y, lab, col, "end")
    for n, (lab, col) in right.items():
        x, y = p[n]
        s.wire([(x, y), (x + 40, y)], col, 2.5)
        s.tag(x + 40, y, lab, col)
    s.text(550, 470, "背面 D11–D19 焊盘不接；BAT± 焊盘不接", 11.5, "middle", color=C["mute"])
    s.text(550, 488, "棒状天线一定要插上（U.FL）", 11.5, "middle", "700", C["bat"])
    boxes = [
        (40, "PCA9685 模块", "VCC → 3V3\nGND → GND\nSDA → D4 · SCL → D5\nOE → D0（加 10 kΩ 到 3V3）\nV+ → 5V_SV"),
        (250, "BNO085 模块", "VIN → 3V3 · GND → GND\nSDA → D4 · SCL → D5\nINT → D1\nDI(ADR) → 3V3 = 0x4B\nRST 悬空（板上有上拉）"),
        (460, "LD14P 雷达", "5V → 5V_SV · GND → GND\n雷达 RX ← D6\n雷达 TX → D7\n线序看雷达丝印 / 手册"),
        (670, "MAX98357A", "VIN → 5V_SV · GND\nBCLK ← D8 · LRC ← D9\nDIN ← D10\nSD 悬空 = 左右混音\n喇叭接 + / −"),
        (880, "OLED 0.96″", "VCC → 3V3 · GND\nSCL → D5\nSDA → D4\n地址 0x3C"),
    ]
    for x, t, sub in boxes:
        s.box(x, 540, 190, 160, t, sub, align="start")
    s.legend(40, 735, [("信号", C["sig"], False), ("I²C", C["i2c"], False), ("5 V", C["v5"], False),
                       ("3.3 V", C["v33"], False), ("GND", C["gnd"], False)])
    s.save("fig2-signals.svg")


def fig_servo_map():
    s = Svg(1100, 860, "图 3  12 个舵机插在 PCA9685 哪个通道（俯视，机头朝右）",
            "M 编号 = Seeker PCB 丝印；用 PCA9685 模块时按“通道”插。舵机插头：棕 = GND（靠板边）· 红 = V+ · 橙 = 信号")
    cx, cy, k = 540, 420, 1.9
    legs = [
        ("FL 左前", 60, 40, 60, "M1 → ch 6", "M8 → ch 13"),
        ("FR 右前", 60, -40, -60, "M6 → ch 1", "M12 → ch 9"),
        ("ML 左中", 0, 40, 90, "M2 → ch 5", "M9 → ch 12"),
        ("MR 右中", 0, -40, -90, "M5 → ch 2", "M11 → ch 10"),
        ("RL 左后", -60, 40, 120, "M3 → ch 4", "M10 → ch 11"),
        ("RR 右后", -60, -40, -120, "M4 → ch 3", "M7 → ch 0"),
    ]
    # body plate outline (approx.)
    s.parts.append(f'<rect x="{cx - 72 * k}" y="{cy - 55 * k}" width="{145 * k}" height="{111 * k}" rx="18" '
                   f'fill="{C["box"]}" stroke="{C["boxline"]}" stroke-width="1.6"/>')
    s.text(cx, cy - 8, "底板（俯视）", 14, "middle", "700")
    s.text(cx, cy + 12, "X+ = 前，Y+ = 左", 12, "middle", color=C["mute"])
    s.arrow(cx + 40, cy + 40, cx + 110, cy + 40, C["bat"], 3)
    s.text(cx + 75, cy + 62, "前进方向", 12, "middle", "700", C["bat"])
    for name, x, y, a, hip, knee in legs:
        hx, hy = cx + x * k, cy - y * k
        ux, uy = math.cos(math.radians(a)), -math.sin(math.radians(a))
        kx, ky = hx + 45 * k * ux, hy + 45 * k * uy
        fx, fy = hx + 91 * k * ux, hy + 91 * k * uy
        s.wire([(hx, hy), (kx, ky)], C["sig"], 7)
        s.wire([(kx, ky), (fx, fy)], C["v5"], 5)
        s.parts.append(f'<circle cx="{hx}" cy="{hy}" r="9" fill="#FFFFFF" stroke="{C["ink"]}" stroke-width="3"/>')
        s.parts.append(f'<circle cx="{kx}" cy="{ky}" r="7" fill="#FFFFFF" stroke="{C["ink"]}" stroke-width="3"/>')
        lx = fx + (18 if ux > 0.1 else (-18 if ux < -0.1 else 0))
        ly = fy + (22 if uy > 0.3 else (-50 if uy < -0.3 else 0))
        anchor = "start" if ux > 0.1 else ("end" if ux < -0.1 else "middle")
        s.text(lx, ly, name, 15, anchor, "700")
        s.text(lx, ly + 20, "髋 " + hip, 13, anchor, "600", C["sig"])
        s.text(lx, ly + 38, "膝 " + knee, 13, anchor, "600", C["v5"])
    s.note(40, 800, 1, "粗蓝线 = 大腿（髋舵机带动，左右摆）；橙线 = 小腿（膝舵机带动，上下抬）")
    s.note(40, 828, 2, "通道和固件 HexapodConfig.h 的 kServo* 一致；插错了改固件里的 mPort(N)，或者换插头")
    s.save("fig3-servo-map.svg")


if __name__ == "__main__":
    fig_overview()
    fig_power()
    fig_signals()
    fig_servo_map()
    print("diagrams written to", OUT)
