#!/usr/bin/env python3
"""Generate the robot-arm diagrams (SVG) used by docs/assembly-guide.md and docs/build-plan.md.

    python docs/img/make_diagrams.py

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

    def box(self, x, y, w, h, title, sub="", fill=None, dashed=False):
        dash = ' stroke-dasharray="6 4"' if dashed else ""
        self.parts.append(
            f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="8" fill="{fill or C["box"]}" '
            f'stroke="{C["boxline"]}" stroke-width="1.6"{dash}/>')
        self.text(x + w / 2, y + 22, title, 14, "middle", "700")
        if sub:
            for i, line in enumerate(sub.split("\n")):
                self.text(x + w / 2, y + 40 + i * 16, line, 11.5, "middle", color=C["mute"])

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




C["scl"] = "#0E8C9A"

DEVKIT_L = ["3V3", "EN", "36", "39", "34", "35", "32", "33", "25", "26", "27", "14", "12", "GND",
            "13", "SD2", "SD3", "CMD", "5V"]
DEVKIT_R = ["GND", "23", "22", "TX0", "RX0", "21", "GND", "19", "18", "5", "17", "16", "4", "0",
            "2", "15", "SD1", "SD0", "CLK"]


def devkit(s, x, y, used):
    """ESP32 DevKit (38 pin, ESP32-WROOM-32E) seen from the top, antenna up, USB down,
    with the real pin order. Returns {name: (x, y)}; the left GND is 'GND_L'."""
    w, step = 200, 22
    h = 70 + step * 19 + 20
    s.box(x, y, w, h, "ESP32 DevKit", "WROOM-32E · 俯视")
    s.parts.append(f'<rect x="{x + 60}" y="{y + 52}" width="80" height="10" rx="2" fill="#B9C4C1"/>')
    s.parts.append(f'<rect x="{x + w / 2 - 22}" y="{y + h - 6}" width="44" height="14" rx="4" fill="#B9C4C1"/>')
    s.text(x + w / 2, y + h + 5, "USB", 9.5, "middle", "700")
    pins = {}
    for i, n in enumerate(DEVKIT_L):
        py = y + 80 + i * step
        key = "GND_L" if n == "GND" else n
        s.pin(x, py, n, "left", C["sig"] if key in used else "#9AA5A8")
        pins[key] = (x, py)
    for i, n in enumerate(DEVKIT_R):
        py = y + 80 + i * step
        key = n if n != "GND" or "GND" not in pins else "GND_R2"
        s.pin(x + w, py, n, "right", C["sig"] if key in used else "#9AA5A8")
        pins[key] = (x + w, py)
    return pins


# ---------------------------------------------------------------------------
def fig_overview():
    s = Svg(1100, 500, "图 0  系统总览",
            "手柄通过蓝牙直接连到机械臂上的 ESP32；手机、电脑、语音模块发文字指令；PCA9685 输出 6 路舵机信号")
    s.box(40, 120, 190, 100, "盖世小鸡 G7 Pro", "模式开关：蓝牙\nXbox 布局按键")
    s.box(340, 100, 240, 160, "主控 ESP32 DevKit", "Bluepad32 读手柄\n逆运动学 · 平滑 · 示教\nWi-Fi 热点 RobotArm",
          fill=C["hi"])
    s.box(680, 90, 190, 80, "PCA9685", "16 路舵机驱动 · 0x41")
    s.box(680, 200, 190, 80, "INA226", "舵机电流 / 电压 · 0x40")
    s.box(930, 90, 140, 190, "机械臂", "J1 底座\nJ2 大臂\nJ3 小臂\nJ4 手腕俯仰\nJ5 手腕旋转\nJ6 夹爪")
    s.box(680, 330, 190, 110, "电源", "12 V 适配器\n→ 6 V 舵机 / 5 V 主控\n急停 + 继电器")
    s.box(340, 330, 240, 70, "手机浏览器", "192.168.4.1 状态 + 按钮")
    s.box(40, 290, 190, 70, "可选：语音模块", "离线识别 → 串口文字", dashed=True)
    s.box(40, 390, 190, 70, "电脑 / AI", "USB 串口或 Wi-Fi", dashed=True)
    s.wire([(230, 170), (340, 170)], C["rf"], 3, True)
    s.text(285, 160, "蓝牙", 13, "middle", "700")
    s.wire([(580, 130), (680, 130)], C["i2c"], 3)
    s.wire([(580, 240), (680, 240)], C["i2c"], 3)
    s.text(630, 120, "I²C", 12, "middle", "700", C["i2c"])
    s.text(630, 230, "I²C", 12, "middle", "700", C["i2c"])
    s.arrow(870, 130, 928, 130, C["sig"])
    s.text(899, 120, "PWM×6", 11, "middle", "700", C["sig"])
    s.arrow(900, 385, 1000, 385, C["v6"])
    s.wire([(1000, 385), (1000, 282)], C["v6"], 2.5)
    s.text(935, 375, "6 V", 12, "middle", "700", C["v6"])
    s.wire([(775, 330), (775, 280)], C["v6"], 2.5)
    s.text(785, 312, "测电流", 11.5, color=C["mute"])
    s.wire([(680, 385), (620, 385), (620, 250), (580, 250)], C["v5"], 2.5)
    s.text(628, 300, "5 V", 12, weight="700", color=C["v5"])
    s.wire([(460, 260), (460, 330)], C["rf"], 3, True)
    s.text(470, 300, "Wi-Fi", 12.5, weight="700")
    s.wire([(230, 325), (290, 325), (290, 230), (340, 230)], C["sig"], 2.5)
    s.text(250, 318, "UART", 11.5, weight="700", color=C["sig"])
    s.wire([(230, 425), (300, 425), (300, 250), (340, 250)], C["rf"], 2.5, True)
    s.legend(40, 485, [("I²C", C["i2c"], False), ("舵机信号", C["sig"], False), ("6 V 舵机电", C["v6"], False),
                       ("5 V", C["v5"], False), ("无线", C["rf"], True)])
    s.save("fig0-overview.svg")


def fig_power():
    s = Svg(1100, 720, "图 1  电源：12 V 输入 → 6 V 舵机电（急停 + 继电器 + 电流计）/ 5 V 主控电",
            "舵机电和主控电分开降压；大电流只走粗线，不经过 ESP32")
    # row 1: input chain
    s.box(40, 100, 180, 100, "电源适配器", "12 V 5 A（≥ 60 W）\nDC 5.5×2.1 插头")
    s.box(290, 100, 180, 100, "DC 母座 + 保险丝", "带接线端子\n刀片保险 7.5 A")
    s.box(540, 100, 160, 100, "船型开关", "KCD4 · 16 A")
    s.wire([(220, 150), (290, 150)], C["v12"], 4)
    s.wire([(470, 150), (540, 150)], C["v12"], 4)
    s.wire([(700, 150), (840, 150), (840, 260)], C["v12"], 4)
    s.dot(760, 150, C["v12"])
    s.wire([(760, 150), (760, 225), (455, 225), (455, 260)], C["v12"], 3)
    s.tag(710, 125, "12 V", C["v12"])
    # row 2: converters
    s.box(740, 260, 200, 110, "大电流降压模块", "20 A / 300 W · CC/CV\n12 V → 6.0 V", fill=C["hi"])
    s.box(360, 260, 190, 110, "MP1584EN 降压", "3 A\n12 V → 5.0 V")
    s.part(250, 300, 70, 30, "SS14 ▶")
    s.wire([(360, 315), (320, 315)], C["v5"], 3)
    s.wire([(250, 315), (210, 315)], C["v5"], 3)
    s.box(40, 265, 170, 100, "ESP32 DevKit", "5V 引脚\n（逻辑电，见图 2）")
    s.text(285, 290, "防 USB 倒灌", 11, "middle", color=C["mute"])
    # row 3: servo chain (right -> left)
    s.box(900, 440, 170, 110, "急停开关", "常闭 NC · 22 mm\n蘑菇头 · 10 A")
    s.box(660, 440, 180, 110, "继电器模块", "COM → NO · 10 A\nIN ← GPIO25（图 2）")
    s.box(420, 440, 180, 110, "INA226 模块", "VIN+ → VIN−\n采样电阻 0.01 Ω")
    s.box(40, 440, 320, 110, "舵机电源母线 → PCA9685 V+", "+ 2 × 2200 µF 16 V 电解电容\n6 个舵机都从这里取电（图 3）",
          fill=C["hi"])
    s.wire([(790, 370), (790, 428), (985, 428), (985, 440)], C["v6"], 4)
    s.pin(790, 370, "V+ 出", "bottom", C["v6"])
    s.tag(800, 400, "6.0 V", C["v6"])
    s.wire([(900, 525), (840, 525)], C["v6"], 4)
    s.wire([(660, 525), (600, 525)], C["v6"], 4)
    s.wire([(420, 525), (360, 525)], C["v6"], 4)
    s.pin(900, 525, "", "left", C["v6"])
    s.pin(840, 525, "COM", "right", C["v6"])
    s.pin(660, 525, "NO", "left", C["v6"])
    s.pin(600, 525, "VIN+", "right", C["v6"])
    s.pin(420, 525, "VIN−", "left", C["v6"])
    s.pin(360, 525, "V+", "right", C["v6"])
    # grounds as net tags
    for gx, gy in ((130, 200), (380, 200), (890, 370), (455, 370), (125, 365), (200, 550), (510, 550)):
        s.wire([(gx, gy), (gx, gy + 22)], C["gnd"], 2.5)
        s.tag(gx - 22, gy + 33, "GND", C["gnd"])
    s.text(955, 605, "所有 GND 汇到大降压模块的 GND 端子（星形接地）", 12, "end", color=C["mute"])
    # notes
    s.note(40, 640, 1, "先调电压：大降压模块空载调到 6.0 V，MP1584 调到 5.0 V，调好再接负载")
    s.note(40, 670, 2, "急停串在 6 V 舵机线上：按下 = 舵机断电，机械臂会软下来（手要离开下方）")
    s.note(560, 640, 3, "继电器由 GPIO25 控制：开机默认断开，按 Menu 才给舵机上电")
    s.note(560, 670, 4, "6 V 和 GND 大电流线用 18 AWG 硅胶线，其余用 22 AWG")
    s.legend(40, 705, [("12 V", C["v12"], False), ("6 V 舵机电", C["v6"], False), ("5 V", C["v5"], False),
                       ("GND", C["gnd"], False)])
    s.save("fig1-power.svg")


def fig_signals():
    used = {"3V3", "GND_L", "25", "26", "5V", "22", "21", "17", "16", "GND", "GND_R2", "2"}
    s = Svg(1100, 760, "图 2  信号接线：ESP32 ↔ PCA9685 / INA226（I²C）· 继电器 · 蜂鸣器 · 语音模块",
            "彩色标签表示“接到同名的线”；I²C 两个设备并联在同一对线上")
    p = devkit(s, 450, 100, used)
    # right side: I2C devices
    s.box(830, 100, 230, 190, "PCA9685 舵机驱动", "地址 0x41：把 A0 焊盘\n用锡短接\nV+ 端子 ← 6 V（图 1）")
    for n, (lab, yy, col) in enumerate((("GND", 150, C["gnd"]), ("VCC", 180, C["v33"]), ("SDA", 210, C["i2c"]),
                                        ("SCL", 240, C["scl"]))):
        s.pin(830, yy, lab, "left", col)
    s.box(830, 330, 230, 170, "INA226 电流计", "地址 0x40（默认）\nVIN+ / VIN− 串在\n6 V 线上（图 1）")
    for lab, yy, col in (("VCC", 380, C["v33"]), ("GND", 410, C["gnd"]), ("SDA", 440, C["i2c"]),
                         ("SCL", 470, C["scl"])):
        s.pin(830, yy, lab, "left", col)
    s.box(830, 540, 230, 150, "可选：离线语音模块", "CI-03T / ASR-PRO 等\n串口输出英文指令行\n9600 bps", dashed=True)
    for lab, yy, col in (("TX", 590, C["sig"]), ("RX", 620, C["sig"]), ("5V", 650, C["v5"]), ("GND", 675, C["gnd"])):
        s.pin(830, yy, lab, "left", col)
    # I2C wiring
    sda, scl = p["21"], p["22"]
    s.wire([sda, (740, sda[1]), (740, 440), (830, 440)], C["i2c"], 3)
    s.wire([(740, 210), (830, 210)], C["i2c"], 3)
    s.wire([(740, sda[1]), (740, 210)], C["i2c"], 3)
    s.dot(740, sda[1], C["i2c"])
    s.wire([scl, (710, scl[1]), (710, 470), (830, 470)], C["scl"], 3)
    s.wire([(710, 240), (830, 240)], C["scl"], 3)
    s.dot(710, 240, C["scl"])
    s.text(700, 330, "SCL", 12, "end", "700", C["scl"])
    s.text(750, 330, "SDA", 12, "start", "700", C["i2c"])
    # voice UART: module TX -> GPIO16 (RX2), module RX <- GPIO17 (TX2)
    s.wire([p["16"], (680, p["16"][1]), (680, 590), (830, 590)], C["sig"], 2.5)
    s.wire([p["17"], (660, p["17"][1]), (660, 620), (830, 620)], C["sig"], 2.5)
    s.text(672, 700, "交叉：TX→16，RX←17", 11.5, "middle", color=C["mute"])
    # power / GND tags on the right devices
    for yy in (180, 380):
        s.tag(826, yy, "3V3", C["v33"], "end")
    for yy in (150, 410, 675):
        s.tag(826, yy, "GND", C["gnd"], "end")
    s.tag(826, 650, "5V", C["v5"], "end")
    # left side: relay, buzzer
    s.box(40, 250, 250, 170, "继电器模块（1 路）", "5 V 线圈 · 光耦隔离\n跳线设为“高电平触发”\n触点见图 1")
    for lab, yy, col in (("IN", 356, C["sig"]), ("VCC", 382, C["v5"]), ("GND", 406, C["gnd"])):
        s.pin(290, yy, lab, "right", col)
    s.wire([(290, 356), p["25"]], C["sig"], 3)
    s.box(40, 435, 250, 120, "有源蜂鸣器模块", "高电平响 · 3.3–5 V")
    for lab, yy, col in (("I/O", 480, C["sig"]), ("VCC", 505, C["v33"]), ("GND", 530, C["gnd"])):
        s.pin(290, yy, lab, "right", col)
    s.wire([(290, 480), (360, 480), (360, p["26"][1]), p["26"]], C["sig"], 3)
    s.tag(295, 382, "5V", C["v5"])
    s.tag(295, 406, "GND", C["gnd"])
    s.tag(295, 505, "3V3", C["v33"])
    s.tag(295, 530, "GND", C["gnd"])
    # DevKit power pins
    s.tag(444, p["3V3"][1], "3V3", C["v33"], "end")
    s.tag(444, p["GND_L"][1], "GND", C["gnd"], "end")
    s.tag(444, p["5V"][1], "5V ← MP1584 经 SS14（图 1）", C["v5"], "end")
    s.tag(656, p["GND"][1], "GND", C["gnd"])
    s.legend(40, 745, [("SDA", C["i2c"], False), ("SCL", C["scl"], False), ("信号", C["sig"], False),
                       ("3.3 V", C["v33"], False), ("5 V", C["v5"], False)])
    s.save("fig2-signals.svg")


SERVOS = [
    ("J1 底座旋转", "DS3218MG · 20 kg·cm · 180°"),
    ("J2 大臂（肩）", "DS3225MG · 25 kg·cm · 180°"),
    ("J3 小臂（肘）", "DS3218MG · 20 kg·cm · 180°"),
    ("J4 手腕俯仰", "MG996R · 10 kg·cm · 180°"),
    ("J5 手腕旋转", "MG996R · 10 kg·cm · 180°"),
    ("J6 夹爪", "MG996R · 10 kg·cm · 180°"),
]


def fig_servos():
    s = Svg(1100, 620, "图 3  舵机接线：PCA9685 通道 0–5 → J1–J6",
            "舵机插头直接插在 PCA9685 的 3 针排针上：棕 = GND，红 = V+，橙 = 信号（PWM）")
    s.box(40, 100, 330, 470, "PCA9685 板", "")
    s.text(205, 150, "每个通道 3 针（图中横着画）：PWM · V+ · GND", 11.5, "middle", color=C["mute"])
    s.parts.append(f'<rect x="60" y="480" width="120" height="60" rx="4" fill="#2E7D32"/>')
    s.text(120, 505, "V+  GND", 12, "middle", "700", "#FFFFFF")
    s.text(120, 525, "螺丝端子", 11, "middle", color="#E8F5E9")
    s.tag(190, 510, "← 6 V 母线（图 1）", C["v6"])
    rows = []
    for i in range(6):
        cy = 190 + i * 48
        s.text(70, cy + 5, f"通道 {i}", 13, "start", "700")
        for k, col in enumerate(("#E3A600", C["v12"], C["gnd"])):
            s.parts.append(f'<rect x="{150 + k * 24}" y="{cy - 9}" width="18" height="18" rx="2" fill="{col}"/>')
        rows.append(cy)
    s.text(159, 175, "PWM", 9.5, "middle", "700")
    s.text(183, 175, "V+", 9.5, "middle", "700")
    s.text(207, 175, "G", 9.5, "middle", "700")
    for i, (name, model) in enumerate(SERVOS):
        cy = rows[i]
        bx, by = 560, 100 + i * 78
        s.box(bx, by, 500, 64, name, model, fill=C["hi"] if i == 1 else None)
        s.wire([(222, cy), (300, cy), (470, by + 32), (560, by + 32)], "#E3A600", 3)
        s.text(465, by + 26, "延长线" if i >= 3 else "", 11, "end", color=C["mute"])
    s.note(40, 600, 1, "插反（棕线不在 G 那一侧）舵机不会转，但一般不会坏；插之前核对颜色")
    s.note(620, 600, 2, "J4–J6 离底座远，用 30–50 cm 舵机延长线（22 AWG 粗线）")
    s.save("fig3-servos.svg")


def fig_kinematics():
    import math
    s = Svg(1100, 640, "图 4  关节编号、尺寸和角度约定（侧视 + 俯视）",
            "逆运动学用到的 4 个尺寸：d1、L2、L3、L4（用尺子量，单位 mm）；角度的正方向见箭头")
    k = 1.35
    table_y, bx = 540, 180
    s.wire([(60, table_y), (660, table_y)], C["mute"], 3)
    s.text(70, table_y + 22, "桌面 z = 0", 12, color=C["mute"])
    sh = (bx, table_y - 75 * k)
    t2, t3, t4 = 60, -70, -50
    a23, phi = t2 + t3, t2 + t3 + t4

    def step(p, length, ang):
        return (p[0] + length * k * math.cos(math.radians(ang)), p[1] - length * k * math.sin(math.radians(ang)))

    el = step(sh, 105, t2)
    wr = step(el, 100, a23)
    tp = step(wr, 120, phi)
    # base
    s.parts.append(f'<rect x="{bx - 45}" y="{table_y - 30}" width="90" height="30" rx="4" fill="{C["arm"]}" '
                   f'stroke="{C["boxline"]}"/>')
    s.wire([(bx, table_y - 30), sh], "#8FA3AA", 16)
    for a, b in ((sh, el), (el, wr), (wr, tp)):
        s.wire([a, b], "#8FA3AA", 14)
    # gripper fingers
    for off in (-12, 12):
        ang = math.radians(phi)
        nx, ny = -math.sin(ang) * off, -math.cos(ang) * off
        s.wire([(tp[0] + nx * 0.8, tp[1] + ny * 0.8), step((tp[0] + nx, tp[1] + ny), 18, phi)], C["boxline"], 4)
    for (px, py), lab in ((sh, "J2"), (el, "J3"), (wr, "J4")):
        s.parts.append(f'<circle cx="{px:.1f}" cy="{py:.1f}" r="13" fill="#FFFFFF" stroke="{C["boxline"]}" '
                       f'stroke-width="2.5"/>')
        s.text(px + 18, py - 14, lab, 14, weight="700")
    s.dot(tp[0], tp[1], C["v12"])
    s.text(tp[0] + 14, tp[1] + 6, "工具点（两指中间）", 12.5, weight="700", color=C["v12"])
    s.text(bx + 52, table_y - 8, "J1", 14, "start", "700")
    # dimensions
    s.wire([(bx - 70, table_y), (bx - 70, sh[1])], C["sig"], 1.5)
    s.text(bx - 76, (table_y + sh[1]) / 2, "d1", 14, "end", "700", C["sig"])
    for a, b, lab, dx, dy in ((sh, el, "L2", -30, 0), (el, wr, "L3", 0, -18), (wr, tp, "L4", 26, 0)):
        s.text((a[0] + b[0]) / 2 + dx, (a[1] + b[1]) / 2 + dy, lab, 15, "middle", "700", C["sig"])
    # angles
    s.wire([sh, (sh[0] + 90, sh[1])], C["mute"], 1.5, True)
    s.text(sh[0] + 40, sh[1] - 12, "θ2", 14, weight="700", color=C["v6"])
    ext = step(el, 60, t2)
    s.wire([el, ext], C["mute"], 1.5, True)
    s.text(el[0] + 48, el[1] - 52, "θ3 < 0", 13, weight="700", color=C["v6"])
    s.wire([wr, (wr[0] + 80, wr[1])], C["mute"], 1.5, True)
    s.text(wr[0] + 64, wr[1] + 40, "φ（工具俯仰）", 13, weight="700", color=C["v6"])
    s.text((wr[0] + tp[0]) / 2 - 22, (wr[1] + tp[1]) / 2 + 30, "J5 绕这根轴转", 12, "end", color=C["mute"])
    # top view
    cx, cy = 870, 330
    s.text(870, 120, "俯视", 15, "middle", "700")
    s.parts.append(f'<circle cx="{cx}" cy="{cy}" r="46" fill="{C["arm"]}" stroke="{C["boxline"]}"/>')
    s.arrow(cx, cy, cx + 170, cy, C["ink"], 2)
    s.text(cx + 172, cy + 20, "x（正前方）", 13, "end", "700")
    s.arrow(cx, cy, cx, cy - 170, C["ink"], 2)
    s.text(cx + 8, cy - 160, "y（机械臂的左边）", 13, weight="700")
    ang = math.radians(35)
    s.wire([(cx, cy), (cx + 150 * math.cos(ang), cy - 150 * math.sin(ang))], "#8FA3AA", 12)
    s.parts.append(f'<path d="M{cx + 80},{cy} A80,80 0 0 0 {cx + 80 * math.cos(ang):.1f},{cy - 80 * math.sin(ang):.1f}" '
                   f'fill="none" stroke="{C["v6"]}" stroke-width="2.5"/>')
    s.text(cx + 92, cy - 22, "θ1 > 0（左转）", 13, weight="700", color=C["v6"])
    s.text(cx, cy + 5, "J1", 13, "middle", "700")
    # conventions
    for i, line in enumerate(("θ1：0 = 正前方，往左转为正",
                              "θ2：大臂和水平面的夹角，90° = 竖直",
                              "θ3：小臂相对大臂的弯角，0 = 伸直，往下弯为负",
                              "θ4：手腕相对小臂的弯角；φ = θ2 + θ3 + θ4",
                              "φ：工具俯仰，0 = 水平，−90° = 竖直朝下")):
        s.text(700, 470 + i * 24, line, 12.5, color=C["mute"])
    s.save("fig4-kinematics.svg")


def fig_gamepad():
    s = Svg(1100, 620, "图 5  G7 Pro 按键功能（关节模式 / XYZ 模式）",
            "View 键切换两种模式；RT / LT 控制夹爪；Menu 上电 / 停放断电")
    # controller silhouette
    s.parts.append('<path d="M380,210 Q380,170 430,165 L670,165 Q720,170 720,210 L760,420 Q765,470 715,470 '
                   'Q680,470 650,420 L620,380 L480,380 L450,420 Q420,470 385,470 Q335,470 340,420 Z" '
                   f'fill="{C["box"]}" stroke="{C["boxline"]}" stroke-width="2"/>')
    for x0 in (400, 640):
        s.parts.append(f'<rect x="{x0}" y="140" width="60" height="18" rx="8" fill="#C9D3D6" stroke="{C["boxline"]}"/>')
        s.parts.append(f'<rect x="{x0 + 5}" y="112" width="50" height="24" rx="10" fill="#E3E9EB" stroke="{C["boxline"]}"/>')
    s.text(430, 153, "LB", 11, "middle", "700")
    s.text(670, 153, "RB", 11, "middle", "700")
    s.text(430, 129, "LT", 11, "middle", "700")
    s.text(670, 129, "RT", 11, "middle", "700")
    for (x0, y0) in ((450, 230), (600, 320)):
        s.parts.append(f'<circle cx="{x0}" cy="{y0}" r="30" fill="#C9D3D6" stroke="{C["boxline"]}" stroke-width="2"/>')
        s.parts.append(f'<circle cx="{x0}" cy="{y0}" r="14" fill="#9FAFB4"/>')
    s.parts.append(f'<path d="M490,310 h14 v-14 h14 v14 h14 v14 h-14 v14 h-14 v-14 h-14 Z" fill="#9FAFB4"/>')
    for (x0, y0, lab, col) in ((650, 205, "Y", "#E0A800"), (675, 230, "B", "#C62828"), (650, 255, "A", "#2E7D32"),
                               (625, 230, "X", "#1565C0")):
        s.parts.append(f'<circle cx="{x0}" cy="{y0}" r="12" fill="{col}"/>')
        s.text(x0, y0 + 4.5, lab, 12, "middle", "700", "#FFFFFF")
    s.parts.append(f'<rect x="522" y="222" width="18" height="12" rx="3" fill="#9FAFB4"/>')
    s.parts.append(f'<rect x="562" y="222" width="18" height="12" rx="3" fill="#9FAFB4"/>')
    s.text(531, 250, "View", 10, "middle", "700")
    s.text(571, 250, "Menu", 10, "middle", "700")

    def call(px, py, tx, ty, lines, anchor):
        s.wire([(px, py), (tx + (8 if anchor == "start" else -8), ty - 5)], C["mute"], 1.2)
        for i, ln in enumerate(lines):
            s.text(tx + (12 if anchor == "start" else -12), ty + i * 18, ln, 12.5 if i else 13.5, anchor,
                   "700" if i == 0 else "400", None if i == 0 else C["mute"])

    call(430, 124, 330, 100, ["LT  张开夹爪（按得越深越快）"], "end")
    call(430, 150, 330, 135, ["LB  速度 −（慢 / 中 / 快）"], "end")
    call(450, 230, 330, 200, ["左摇杆", "关节：左右 J1 底座 · 上下 J2 大臂", "XYZ：左右 = 左右移 · 上下 = 前后移"], "end")
    call(505, 318, 330, 290, ["十字键", "← → J5 手腕旋转（两种模式）", "↓ 删除最后一个路点 · ↑ 长按 1 秒保存路点"],
         "end")
    call(531, 228, 330, 380, ["View  切换 关节 / XYZ 模式"], "end")
    call(670, 124, 770, 100, ["RT  闭合夹爪（夹到东西会自动停）"], "start")
    call(670, 150, 770, 135, ["RB  速度 +"], "start")
    call(650, 205, 770, 180, ["Y  回原位（HOME 姿态）"], "start")
    call(675, 230, 770, 210, ["B  停止（过载后按 B 恢复）"], "start")
    call(650, 255, 770, 240, ["A  记录路点 · 长按 2 秒清空全部"], "start")
    call(625, 230, 770, 270, ["X  播放一次 · 长按 1 秒循环播放"], "start")
    call(600, 320, 770, 320, ["右摇杆", "关节：上下 J3 小臂 · 左右 J4 手腕俯仰", "XYZ：上下 = 升降 · 左右 = 工具俯仰"],
         "start")
    call(571, 228, 770, 400, ["Menu  给舵机上电 / 停放后断电"], "start")
    s.note(40, 520, 1, "播放或执行文字指令时，动一下摇杆 / 扳机 / 十字键 ← →，控制权马上回到手柄")
    s.note(40, 550, 2, "手柄断开时机械臂停在原地保持姿态；正在播放的路点会继续播放完（B 或文字 STOP 停止）")
    s.note(40, 580, 3, "手柄震动：短 = 确认 · 长 = 到达极限 / 过载 / 急停")
    s.save("fig5-gamepad.svg")


def fig_layout():
    s = Svg(1100, 600, "图 6  桌面布局（俯视）：底板、机械臂、电控盒、急停",
            "机械臂伸出去有力矩，底板必须夹在桌子上；虚线是最大伸展范围，里面不要放手和杂物")
    ox, oy, bw, bh = 120, 120, 400, 300
    s.parts.insert(2, '<defs><clipPath id="clip"><rect x="0" y="80" width="690" height="500"/></clipPath></defs>')
    s.parts.append(f'<rect x="{ox}" y="{oy}" width="{bw}" height="{bh}" rx="6" fill="#EFE6D6" '
                   f'stroke="{C["boxline"]}" stroke-width="2"/>')
    s.text(ox + 8, oy + bh - 10, "底板 400 × 300 mm（12 mm 多层板 / 5 mm 铝板）", 12.5, weight="700")
    ax, ay = ox + 170, oy + 150
    s.parts.append(f'<path d="M{ax},{ay - 325} A325,325 0 0 1 {ax},{ay + 325}" fill="none" stroke="{C["v12"]}" '
                   f'stroke-width="1.8" stroke-dasharray="8 6" clip-path="url(#clip)"/>')
    s.text(ax + 330, ay + 5, "最大伸展", 12.5, weight="700", color=C["v12"])
    s.text(ax + 330, ay + 23, "≈ 325 mm", 12.5, weight="700", color=C["v12"])
    s.parts.append(f'<circle cx="{ax}" cy="{ay}" r="50" fill="{C["arm"]}" stroke="{C["boxline"]}" stroke-width="2"/>')
    s.text(ax, ay + 5, "机械臂底座", 12, "middle", "700")
    s.arrow(ax + 50, ay, ax + 170, ay, C["ink"], 2)
    s.text(ax + 172, ay - 10, "x 正前方（工作区）", 12, "middle", "700")
    ex, ey = ox + 15, oy + 95
    s.parts.append(f'<rect x="{ex}" y="{ey}" width="90" height="110" rx="6" fill="{C["hi"]}" '
                   f'stroke="{C["boxline"]}" stroke-width="1.6"/>')
    s.text(ex + 45, ey + 40, "电控盒", 13, "middle", "700")
    s.text(ex + 45, ey + 60, "在底座后方", 11, "middle", color=C["mute"])
    s.text(ex + 45, ey + 76, "手臂够不到", 11, "middle", color=C["mute"])
    s.wire([(ex + 90, ay - 20), (ax - 50, ay - 20)], C["sig"], 2.5, True)
    s.parts.append(f'<rect x="{ox - 26}" y="{oy + 30}" width="26" height="22" rx="3" fill="#555"/>')
    s.text(ox - 30, oy + 46, "DC 12 V + 开关", 12, "end", "700")
    for cy in (oy + 10, oy + bh - 60):
        s.parts.append(f'<rect x="{ox - 18}" y="{cy}" width="26" height="46" rx="4" fill="#607D8B"/>')
    s.text(ox - 30, oy + bh - 30, "F 型夹 × 2", 12, "end", "700", "#455A64")
    s.text(ox - 30, oy + bh - 14, "夹在桌边", 11.5, "end", color="#455A64")
    s.parts.append(f'<rect x="585" y="455" width="70" height="60" rx="6" fill="#FFD54F" stroke="#7F6000" stroke-width="2"/>')
    s.parts.append('<circle cx="620" cy="485" r="20" fill="#C62828" stroke="#7F0000" stroke-width="3"/>')
    s.text(620, 535, "急停盒", 13, "middle", "700", "#C62828")
    s.wire([(ex + 45, ey + 110), (ex + 45, 540), (560, 540), (585, 500)], C["v6"], 2.5, True)
    s.text(330, 556, "急停线（6 V 回路，18 AWG）", 11.5, "middle", color=C["v6"])
    for i, line in enumerate(("① 底座用 4 颗 M4 螺丝固定在底板上，不要只靠胶",
                              "② 电控盒放在底座正后方：J1 只转 ±90°，手臂够不到",
                              "③ 急停单独装小盒，放在伸展范围外、右手边",
                              "④ 底板后边夹在桌边，两把 F 型夹",
                              "⑤ 第一次运行把速度调到“慢”（LB）")):
        s.text(720, 170 + i * 30, line, 13)
    s.save("fig6-layout.svg")


def fig_calibration():
    s = Svg(1100, 600, "图 7  两点标定：让“角度”和舵机脉宽一一对应",
            "每个关节在两个已知姿态各 MARK 一次，固件自动算出 us0 和 usdeg；公式：脉宽 = us0 + usdeg × 角度")
    x0, y0, w, h = 90, 110, 430, 330
    s.wire([(x0, y0 + h), (x0 + w, y0 + h)], C["ink"], 2)
    s.wire([(x0, y0 + h), (x0, y0)], C["ink"], 2)
    s.text(x0 + w, y0 + h + 30, "关节角度（°）", 12.5, "end", "700")
    s.text(x0 - 10, y0 - 10, "脉宽（µs）", 12.5, "start", "700")
    for v, lab in ((0, "500"), (0.5, "1500"), (1, "2500")):
        yy = y0 + h - v * h
        s.wire([(x0 - 6, yy), (x0, yy)], C["ink"], 2)
        s.text(x0 - 10, yy + 4, lab, 11.5, "end")
    pa, pb = (x0 + 110, y0 + h - 0.14 * h), (x0 + 330, y0 + h - 0.64 * h)
    slope = (pb[1] - pa[1]) / (pb[0] - pa[0])
    s.wire([(x0 + 52, pa[1] + slope * (x0 + 52 - pa[0])), (x0 + 410, pa[1] + slope * (x0 + 410 - pa[0]))],
           C["sig"], 3)
    for (px, py), lab in ((pa, "MARK 2 0"), (pb, "MARK 2 90")):
        s.parts.append(f'<circle cx="{px}" cy="{py}" r="8" fill="{C["v12"]}"/>')
        s.text(px + 14, py + 18, lab, 13, weight="700", color=C["v12"])
    s.text(x0 + 110, y0 + h + 18, "0°", 11.5, "middle")
    s.text(x0 + 330, y0 + h + 18, "90°", 11.5, "middle")
    s.text(x0 + 200, y0 + 70, "斜率 = usdeg（正负 = 方向）", 12.5, color=C["sig"], weight="700")
    # poses for J2
    bx = 620
    s.text(bx, 125, "例：J2 大臂", 15, weight="700")
    for i, (ang, title) in enumerate(((0, "① 大臂水平 → MARK 2 0"), (90, "② 大臂竖直 → MARK 2 90"))):
        yy = 250 + i * 190
        s.wire([(bx, yy + 40), (bx + 180, yy + 40)], C["mute"], 2)
        s.parts.append(f'<circle cx="{bx + 40}" cy="{yy}" r="10" fill="#FFFFFF" stroke="{C["boxline"]}" stroke-width="2.5"/>')
        end = (bx + 150, yy) if ang == 0 else (bx + 40, yy - 105)
        s.wire([(bx + 40, yy), end], "#8FA3AA", 12)
        s.text(bx + 200, yy - 30 if ang else yy + 5, title, 13, weight="700")
        s.text(bx + 200, (yy - 10) if ang else yy + 25, "用手机水平仪 App 贴在大臂上对准", 11.5, color=C["mute"])
    s.note(40, 540, 1, "PULSE 2 1500 让舵机先回中；再用 PULSE 2 <µs> 一点点调，调到姿态①")
    s.note(40, 570, 2, "每个关节都做一遍（夹爪：全开 MARK 6 0，夹紧 MARK 6 100），最后 PULSE OFF、SAVE PARAMS")
    s.save("fig7-calibration.svg")


def fig_assembly():
    s = Svg(1100, 520, "图 8  机械结构装配顺序（从底座往上）",
            "黄金规则：每装一个舵盘前，舵机先通电回中（1500 µs），舵盘按下图的“中位姿态”对准再拧螺丝")
    steps = [
        ("① 底座", "底板打孔装轴承转盘\nJ1 舵机竖直装在底座里\n金属舵盘连转盘上层"),
        ("② 肩部 J2", "J1 转盘上装 J2 舵机支架\nJ2 用扭力最大的 DS3225\n中位时大臂竖直"),
        ("③ 大臂", "两块 U 型支架 + 杯士轴承\n做大臂（长 L2）\n另一侧装轴承，不要悬臂"),
        ("④ 肘部 J3", "J3 舵机装在大臂顶端\n中位时小臂水平朝前"),
        ("⑤ 手腕 J4 / J5", "J4 俯仰：中位时手腕伸直\nJ5 旋转：中位时夹爪\n两指左右张开"),
        ("⑥ 夹爪 J6", "夹爪舵机中位 = 半开\n装好后标定全开 / 夹紧"),
    ]
    for i, (title, body) in enumerate(steps):
        col, row = i % 3, i // 3
        x, y = 40 + col * 350, 100 + row * 190
        s.box(x, y, 300, 130, title, body, fill=C["hi"] if i in (1, 4) else None)
        if col < 2:
            s.arrow(x + 300, y + 65, x + 348, y + 65, C["ink"], 2)
    s.wire([(890, 230), (890, 260), (190, 260), (190, 280)], C["ink"], 2)
    s.arrow(190, 276, 190, 289, C["ink"], 2)
    s.note(40, 460, 1, "所有舵盘螺丝点一滴蓝色螺丝胶；支架螺丝用 M3，带弹垫或防松螺母")
    s.note(40, 490, 2, "每装完一节，用手转一转：只能在舵机转动方向上动，其他方向不能晃")
    s.save("fig8-assembly.svg")


if __name__ == "__main__":
    fig_overview()
    fig_power()
    fig_signals()
    fig_servos()
    fig_kinematics()
    fig_gamepad()
    fig_layout()
    fig_calibration()
    fig_assembly()
