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




C["scl"] = "#0E8C9A"

UNO_TOP = ["SCL", "SDA", "AREF", "GND", "D13", "D12", "D11", "D10", "D9", "D8", None,
           "D7", "D6", "D5", "D4", "D3", "D2", "D1", "D0"]
UNO_BOT = ["", "IOREF", "RST", "3V3", "5V", "GND", "GND", "VIN", None, None, None,
           "A0", "A1", "A2", "A3", "A4", "A5"]
SHIELD = {"D9", "D10", "D11", "D12", "D13"}


def uno(s, x, y, used):
    """Arduino Uno R3 seen from the top, USB-B and DC jack on the left, with the real header
    order. Pins used by the USB Host Shield are light grey. Returns {name: (x, y)}."""
    w, h, step = 540, 250, 24
    s.parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="10" fill="#E3F1F4" stroke="{C["boxline"]}" '
                   f'stroke-width="1.6"/>')
    s.text(x + w / 2, y + h / 2 - 6, "Arduino Uno R3", 17, "middle", "700")
    s.text(x + w / 2, y + h / 2 + 16, "（上面叠插 USB Host Shield，引脚一一对应）", 11.5, "middle", color=C["mute"])
    s.parts.append(f'<rect x="{x - 14}" y="{y + 40}" width="46" height="50" rx="4" fill="#B9C4C1"/>')
    s.text(x + 9, y + 70, "USB-B", 9.5, "middle", "700")
    s.parts.append(f'<rect x="{x - 14}" y="{y + 160}" width="46" height="40" rx="4" fill="#555"/>')
    s.text(x + 9, y + 184, "DC", 9.5, "middle", "700", "#FFFFFF")
    pins = {}
    x0 = x + 70
    for i, n in enumerate(UNO_TOP):
        if n is None:
            continue
        px, py = x0 + i * step, y
        col = "#C8CDD0" if n in SHIELD else (C["sig"] if n in used else "#9AA5A8")
        s.parts.append(f'<circle cx="{px}" cy="{py}" r="5" fill="{col}"/>')
        s.text(px, py + 20, n, 9.5, "middle", "700")
        pins[n] = (px, py)
    for i, n in enumerate(UNO_BOT):
        if not n:
            continue
        px, py = x0 + 40 + i * step, y + h
        col = C["sig"] if n in used else "#9AA5A8"
        s.parts.append(f'<circle cx="{px}" cy="{py}" r="5" fill="{col}"/>')
        s.text(px, py - 12, n, 9.5, "middle", "700")
        key = n if n != "GND" or "GND_B" in pins else "GND_B"
        pins[key] = (px, py)
    return pins


# ---------------------------------------------------------------------------
def fig_overview():
    s = Svg(1100, 500, "图 0  系统总览（Arduino Uno R3 版，全部用现成模块）",
            "G7 Pro 插在 USB Host Shield 上；Uno 通过 I2C 让 PCA9685 驱动 6 个舵机；电脑 / AI / 语音走串口")
    s.box(40, 120, 190, 100, "盖世小鸡 G7 Pro", "PC / XInput 模式\nUSB-C 线（或 2.4G 接收器）")
    s.box(340, 90, 240, 70, "USB Host Shield 2.0", "MAX3421E · 叠插在 Uno 上")
    s.box(340, 170, 240, 120, "Arduino Uno R3", "ATmega328P · 32 KB / 2 KB\n逆运动学 · 平滑 · 示教\n路点存 EEPROM",
          fill=C["hi"])
    s.box(690, 110, 180, 120, "PCA9685 舵机驱动板", "16 路（用通道 0–5）\n6 V 从蓝色端子进\n+ 2200 µF")
    s.box(930, 90, 140, 190, "机械臂", "J1 底座\nJ2 大臂\nJ3 小臂\nJ4 手腕俯仰\nJ5 手腕旋转\nJ6 夹爪")
    s.box(690, 330, 180, 110, "舵机电源", "6 V 10 A 适配器\n急停开关（兼总开关）\n电压检测模块 → A3")
    s.box(340, 360, 240, 80, "充电宝 / 手机充电头", "USB 5 V → Uno")
    s.box(40, 290, 190, 70, "电脑 / AI", "USB 串口 9600", dashed=True)
    s.box(40, 390, 190, 70, "可选：语音模块", "TX → D0（RX）", dashed=True)
    s.wire([(230, 170), (290, 170), (290, 125), (340, 125)], C["sig"], 3)
    s.text(260, 160, "USB", 13, "middle", "700", C["sig"])
    s.wire([(580, 230), (690, 230)], C["i2c"], 3)
    s.text(635, 220, "I2C", 12, "middle", "700", C["i2c"])
    s.text(635, 250, "A4 · A5", 11, "middle", color=C["mute"])
    s.arrow(870, 170, 928, 170, C["v6"], 3)
    s.wire([(780, 330), (780, 230)], C["v6"], 3)
    s.text(790, 290, "6 V", 12, weight="700", color=C["v6"])
    s.wire([(460, 360), (460, 290)], C["v5"], 2.5)
    s.text(468, 332, "5 V", 12, weight="700", color=C["v5"])
    s.wire([(230, 325), (300, 325), (300, 250), (340, 250)], C["sig"], 2.5)
    s.wire([(230, 425), (310, 425), (310, 270), (340, 270)], C["sig"], 2.5, True)
    s.text(250, 318, "USB", 11.5, weight="700", color=C["sig"])
    s.legend(40, 485, [("信号 / USB", C["sig"], False), ("I2C", C["i2c"], False), ("6 V 舵机电", C["v6"], False),
                       ("5 V", C["v5"], False), ("可选", C["sig"], True)])
    s.save("fig0-overview.svg")


def fig_power():
    s = Svg(1100, 720, "图 1  电源：6 V 适配器 → 急停 → 舵机；Uno 用 USB 供电",
            "全部是现成模块，接头用 WAGO 或螺丝端子，不用焊；舵机大电流不经过 Uno")
    s.box(40, 100, 180, 100, "电源适配器", "6 V 10 A（≥ 60 W）\nDC 5.5 插头")
    s.box(270, 100, 170, 100, "DC 母座转接线端子", "插适配器\n+ / − 拧螺丝")
    s.box(490, 100, 160, 100, "带线保险丝座", "刀片保险丝 10 A")
    s.box(700, 100, 170, 100, "急停开关", "常闭 NC · 10 A\n兼总开关", fill=C["hi"])
    s.box(920, 100, 150, 100, "WAGO 分线", "221-413\n6 V 分两路")
    for x1, x2 in ((220, 270), (440, 490), (650, 700), (870, 920)):
        s.wire([(x1, 150), (x2, 150)], C["v6"], 4)
    s.tag(226, 125, "6 V", C["v6"])
    s.tag(876, 125, "急停后", C["v6"])
    s.box(700, 300, 370, 140, "PCA9685 舵机驱动板", "蓝色接线端子 V+ / GND ← 6 V\n+ 2200 µF 电容（白条纹接 GND）\n6 个舵机插通道 0–5（图 3）",
          fill=C["hi"], title_dy=46)
    s.wire([(1020, 200), (1020, 300)], C["v6"], 4)
    s.pin(1020, 300, "V+", "top", C["v6"])
    s.box(380, 300, 260, 140, "电压检测模块", "学习套件里有（0–25 V，5 : 1）\n6 V 时 S 输出 1.2 V\n拍下急停 → 0 V", title_dy=46)
    s.wire([(960, 200), (960, 250), (510, 250), (510, 300)], C["v6"], 2.5)
    s.pin(510, 300, "VCC", "top", C["v6"])
    s.pin(380, 400, "S", "left", C["sig"])
    s.wire([(380, 400), (340, 400)], C["sig"], 2.5)
    s.tag(336, 400, "→ Uno A3", C["sig"], "end")
    s.box(40, 480, 280, 100, "Arduino Uno R3", "只用 USB 口供电\n（USB Host Shield 和手柄也从这里取电）")
    s.box(40, 620, 280, 60, "充电宝 / 手机充电头", "5 V ≥ 1 A · 学习套件的 USB 线")
    s.wire([(180, 620), (180, 580)], C["v5"], 3)
    s.text(190, 606, "USB 5 V", 12, weight="700", color=C["v5"])
    for gx, gy in ((355, 200), (560, 440), (900, 440)):
        s.wire([(gx, gy), (gx, gy + 22)], C["gnd"], 2.5)
        s.tag(gx - 22, gy + 33, "GND", C["gnd"])
    s.wire([(320, 530), (360, 530)], C["gnd"], 2.5)
    s.tag(364, 530, "GND ← PCA9685 排针 GND（图 2）", C["gnd"])
    s.text(1060, 500, "所有 GND 用 WAGO 接在一起：适配器 −、PCA9685 端子 GND、电压检测模块 GND", 12, "end", color=C["mute"])
    s.note(380, 580, 1, "适配器插上就是 6 V，不用调电位器")
    s.note(380, 610, 2, "急停串在 6 V 线上：拍下 = 舵机断电；旋开 = 通电")
    s.note(380, 640, 3, "电压检测模块接在急停后面，A3 才能发现急停")
    s.note(760, 580, 4, "6 V / GND 线用 18 AWG")
    s.note(760, 610, 5, "舵机电不要接 Uno 的 5V / VIN")
    s.legend(380, 700, [("6 V 舵机电", C["v6"], False), ("5 V", C["v5"], False), ("GND", C["gnd"], False),
                        ("信号", C["sig"], False)])
    s.save("fig1-power.svg")


def fig_signals():
    used = {"A3", "A4", "A5", "D0", "5V", "GND"}
    s = Svg(1100, 720, "图 2  信号接线：Arduino Uno R3 引脚",
            "浅灰 = USB Host Shield 占用（D9–D13）；I2C：A4 = SDA、A5 = SCL 接 PCA9685；A3 测舵机电压；D0 接语音模块")
    p = uno(s, 280, 110, used)
    s.box(40, 110, 190, 150, "USB Host Shield 2.0", "直接叠插在 Uno 上\n占用 D9–D13\nUSB-A 口插 G7 Pro\n线插在它上面的排母")
    s.box(900, 20, 170, 80, "可选：语音模块", "CI-03T / ASR-PRO · 9600", dashed=True)
    px, py = p["D0"]
    s.pin(900, 80, "TX", "left", C["sig"])
    s.wire([(900, 80), (px, 80), (px, py)], C["sig"], 2.5, True)
    # PCA9685 header drawn rotated so SDA / SCL sit right under A4 / A5
    s.box(560, 480, 510, 140, "PCA9685 舵机驱动板", "只接 4 根杜邦线（公对母）\n排针上的 OE、V+ 不接", title_dy=60)
    hdr = [("V+", 702), ("VCC", 726), ("SDA", p["A4"][0]), ("SCL", p["A5"][0]), ("OE", 798), ("GND", 822)]
    for name, x in hdr:
        col = C["i2c"] if name in ("SDA", "SCL") else (C["v5"] if name == "VCC" else
                                                       (C["gnd"] if name == "GND" else "#B0B8BA"))
        s.pin(x, 480, name, "top", col)
    for pin in ("A4", "A5"):
        s.wire([p[pin], (p[pin][0], 480)], C["i2c"], 3)
    s.text(p["A5"][0] + 12, 440, "SDA ← A4 · SCL ← A5", 12, weight="700", color=C["i2c"])
    s.wire([(726, 480), (726, 462)], C["v5"], 2)
    s.tag(720, 452, "Uno 5V", C["v5"], "end")
    s.wire([(822, 480), (822, 462)], C["gnd"], 2)
    s.tag(830, 460, "Uno GND", C["gnd"])
    s.text(702, 632, "V+ / OE 不接", 11.5, "middle", "700", C["v12"])
    s.box(240, 480, 280, 140, "电压检测模块", "左边端子接 6 V（急停后，图 1）\n“+” 针不接", title_dy=60)
    for name, x, col in (("S", 320, C["sig"]), ("+", 380, "#B0B8BA"), ("−", 440, C["gnd"])):
        s.pin(x, 480, name, "top", col)
    s.wire([(320, 480), (320, 462)], C["sig"], 2)
    s.tag(314, 452, "Uno A3", C["sig"], "end")
    s.wire([(440, 480), (440, 462)], C["gnd"], 2)
    s.tag(448, 452, "Uno GND", C["gnd"])
    s.wire([p["A3"], (p["A3"][0], 382)], C["sig"], 2)
    s.tag(p["A3"][0] + 6, 392, "A3", C["sig"], "end")
    s.wire([p["5V"], (p["5V"][0], 382)], C["v5"], 2)
    s.tag(p["5V"][0] + 6, 392, "5V", C["v5"], "end")
    s.wire([p["GND"], (p["GND"][0], 424), (p["GND"][0] + 12, 424)], C["gnd"], 2)
    s.tag(p["GND"][0] + 12, 424, "GND（PCA9685 + 电压模块）", C["gnd"])
    s.note(40, 660, 1, "同名标签连在一起：例如 “Uno 5V” 接到 Uno 的 5V 排母")
    s.note(40, 690, 2, "上传程序时拔掉语音模块的 TX 线（D0 也是下载口）")
    s.note(620, 660, 3, "Uno 的 GND 经过 PCA9685 和舵机电源 GND 相连")
    s.legend(620, 695, [("I2C", C["i2c"], False), ("信号", C["sig"], False), ("5 V", C["v5"], False),
                        ("GND", C["gnd"], False)])
    s.save("fig2-signals.svg")


SERVOS = [
    ("J1 底座旋转", "MG996R（套件自带）· 通道 0"),
    ("J2 大臂（肩）", "MG996R（套件自带）· 通道 1 · 可升级 DS3225MG"),
    ("J3 小臂（肘）", "MG996R（套件自带）· 通道 2"),
    ("J4 手腕俯仰", "MG996R（套件自带）· 通道 3"),
    ("J5 手腕旋转", "MG996R（套件自带）· 通道 4"),
    ("J6 夹爪", "MG996R（套件自带）· 通道 5"),
]


def fig_servos():
    s = Svg(1100, 620, "图 3  舵机插到 PCA9685 舵机驱动板",
            "6 V 从蓝色端子进；6 个舵机插头直接插在通道 0–5 的三针排针上（不用焊）")
    s.box(40, 100, 420, 470, "PCA9685 16 路舵机驱动板", "")
    s.parts.append(f'<rect x="60" y="150" width="130" height="50" rx="4" fill="#2E6DB4" stroke="{C["boxline"]}"/>')
    s.text(92, 181, "V+", 13, "middle", "700", "#FFFFFF")
    s.text(158, 181, "GND", 13, "middle", "700", "#FFFFFF")
    s.tag(60, 222, "← 6 V（急停后，图 1）", C["v6"])
    s.part(80, 250, 36, 50, "")
    s.text(124, 272, "2200 µF", 12, weight="700")
    s.text(124, 290, "白条纹 = −，接 GND", 11, color=C["mute"])
    s.text(60, 330, "板上没焊大电容的：", 11.5, color=C["mute"])
    s.text(60, 346, "把电容两只脚和电源线一起", 11.5, color=C["mute"])
    s.text(60, 362, "拧进蓝色端子", 11.5, color=C["mute"])
    s.text(60, 520, "排针 GND · OE · SCL · SDA · VCC · V+", 11, color=C["mute"])
    s.text(60, 536, "接 Uno（图 2）", 11, color=C["mute"])
    s.text(300, 138, "PWM", 10, "middle", "700")
    s.text(326, 138, "V+", 10, "middle", "700")
    s.text(352, 138, "GND", 10, "middle", "700")
    rows = []
    for i in range(16):
        cy = 150 + i * 24
        used = i < 6
        for k, col in enumerate(("#E3A600", C["v12"], "#5A4A42")):
            s.parts.append(f'<rect x="{290 + k * 26}" y="{cy - 8}" width="20" height="16" rx="2" fill="{col}" '
                           f'fill-opacity="{1 if used else 0.25}"/>')
        s.text(282, cy + 4, f"通道 {i}", 11, "end", "700" if used else "400", C["ink"] if used else C["mute"])
        rows.append(cy)
    for i, (name, model) in enumerate(SERVOS):
        cy = rows[i]
        bx, by = 560, 100 + i * 78
        s.box(bx, by, 500, 64, name, model, fill=C["hi"] if i == 1 else None)
        s.wire([(372, cy), (470, by + 32), (560, by + 32)], "#E3A600", 3)
    s.note(40, 600, 1, "舵机插头：棕 = GND、红 = V+、橙 = PWM；棕色对准 GND 那一排")
    s.note(620, 600, 2, "J4–J6 线不够长，用 22 AWG 舵机延长线")
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
            "View 键切换两种模式；RT / LT 控制夹爪；Menu 上电 / 停放断电；手柄用 USB 线插在 USB Host Shield 上")
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
    call(505, 318, 330, 290, ["十字键", "← → J5 手腕旋转（两种模式）", "↓ 删除最后一个路点（路点自动存进 EEPROM）"],
         "end")
    call(531, 228, 330, 380, ["View  切换 关节 / XYZ 模式"], "end")
    call(670, 124, 770, 100, ["RT  闭合夹爪（松开后自动退 3%，减轻堵转）"], "start")
    call(670, 150, 770, 135, ["RB  速度 +"], "start")
    call(650, 205, 770, 180, ["Y  回原位（HOME 姿态）"], "start")
    call(675, 230, 770, 210, ["B  停止（过载后按 B 恢复）"], "start")
    call(650, 255, 770, 240, ["A  记录路点 · 长按 2 秒清空全部"], "start")
    call(625, 230, 770, 270, ["X  播放一次 · 长按 1 秒循环播放"], "start")
    call(600, 320, 770, 320, ["右摇杆", "关节：上下 J3 小臂 · 左右 J4 手腕俯仰", "XYZ：上下 = 升降 · 左右 = 工具俯仰"],
         "start")
    call(571, 228, 770, 400, ["Menu  给舵机上电 / 停放后断电"], "start")
    s.note(40, 520, 1, "播放或执行文字指令时，动一下摇杆 / 扳机 / 十字键 ← →，控制权马上回到手柄")
    s.note(40, 550, 2, "手柄拔掉时机械臂停在原地保持姿态；正在播放的路点会继续播放完（B 或串口 STOP 停止）")
    s.note(40, 580, 3, "手柄震动：短 = 确认 · 长 = 到达极限 / 急停")
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
    s.text(ex + 45, ey + 40, "电控托板", 13, "middle", "700")
    s.text(ex + 45, ey + 60, "Uno · PCA9685", 11, "middle", color=C["mute"])
    s.text(ex + 45, ey + 76, "电压模块", 11, "middle", color=C["mute"])
    s.wire([(ex + 90, ay - 20), (ax - 50, ay - 20)], C["sig"], 2.5, True)
    s.parts.append(f'<rect x="{ox - 26}" y="{oy + 30}" width="26" height="22" rx="3" fill="#555"/>')
    s.text(ox - 30, oy + 46, "DC 6 V 进线", 12, "end", "700")
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
                              "② 电控托板放在底座正后方：J1 只转 ±90°，手臂够不到",
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
    s.note(40, 540, 1, "标定程序（SETUP_MODE 1）里先 CENTER、再 ON：所有舵机在 1500 µs；用 PULSE 2 <µs> 一点点调到姿态①")
    s.note(40, 570, 2, "每个关节都做一遍（夹爪：全开 MARK 6 0，刚好合拢 MARK 6 100），最后 PULSE OFF、SAVE")
    s.save("fig7-calibration.svg")


def fig_assembly():
    s = Svg(1100, 520, "图 8  机械结构装配顺序（从底座往上）",
            "黄金规则：每装一个舵盘前，舵机先通电回中（1500 µs），舵盘按下图的“中位姿态”对准再拧螺丝")
    steps = [
        ("① 底座", "两块横梁夹住 J1 舵机\n输出轴朝上\n底座螺丝固定在大底板上"),
        ("② 肩部 J2", "J1 舵盘上装异型支架 +\n多功能支架，J2 横装\n中位时大臂竖直"),
        ("③ 大臂", "两块长 U 支架 + 杯式轴承\n做大臂（长 L2）\n另一侧装轴承，不要悬臂"),
        ("④ 肘部 J3", "J3 装在大臂顶端\n法兰杆做小臂\n中位时小臂水平朝前"),
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


def fig_directions():
    import math
    s = Svg(1100, 560, "图 9  上下、左右、前后：XYZ 模式下夹爪怎么走",
            "View 键切到 XYZ 模式：左摇杆管水平面（前后、左右），右摇杆上下管高度；夹爪走直线，俯仰角不变")
    k = 1.2
    table_y, bx = 470, 150
    s.wire([(60, table_y), (520, table_y)], C["mute"], 3)
    sh = (bx, table_y - 75 * k)

    def step(p, length, ang):
        return (p[0] + length * k * math.cos(math.radians(ang)), p[1] - length * k * math.sin(math.radians(ang)))

    el = step(sh, 105, 70)
    wr = step(el, 100, -10)
    tp = step(wr, 120, -30)
    s.parts.append(f'<rect x="{bx - 40}" y="{table_y - 26}" width="80" height="26" rx="4" fill="{C["arm"]}" '
                   f'stroke="{C["boxline"]}"/>')
    for a, b in (((bx, table_y - 26), sh), (sh, el), (el, wr), (wr, tp)):
        s.wire([a, b], "#8FA3AA", 13)
    s.dot(tp[0], tp[1], C["v12"])
    tx, ty = tp
    s.arrow(tx, ty, tx, ty - 90, C["sig"], 3)
    s.arrow(tx, ty, tx, ty + 55, C["sig"], 3)
    s.arrow(tx, ty, tx + 110, ty, C["v6"], 3)
    s.arrow(tx, ty, tx - 90, ty, C["v6"], 3)
    s.text(tx + 8, ty - 96, "上  右摇杆 ↑", 13, weight="700", color=C["sig"])
    s.text(tx + 8, ty + 60, "下  右摇杆 ↓", 13, weight="700", color=C["sig"])
    s.text(tx + 114, ty + 5, "前", 13, weight="700", color=C["v6"])
    s.text(tx + 114, ty + 24, "左摇杆 ↑", 12, color=C["v6"])
    s.text(tx - 96, ty - 10, "后  左摇杆 ↓", 13, "end", "700", C["v6"])
    s.text(290, 120, "侧视", 15, "middle", "700")
    cx, cy = 800, 330
    s.text(800, 120, "俯视", 15, "middle", "700")
    s.parts.append(f'<circle cx="{cx}" cy="{cy}" r="40" fill="{C["arm"]}" stroke="{C["boxline"]}"/>')
    s.wire([(cx, cy), (cx + 170, cy)], "#8FA3AA", 12)
    gx, gy = cx + 170, cy
    s.dot(gx, gy, C["v12"])
    s.arrow(gx, gy, gx + 100, gy, C["v6"], 3)
    s.arrow(gx, gy, gx - 80, gy, C["v6"], 3)
    s.arrow(gx, gy, gx, gy - 100, C["i2c"], 3)
    s.arrow(gx, gy, gx, gy + 100, C["i2c"], 3)
    s.text(gx + 6, gy - 106, "左  左摇杆 ←", 13, weight="700", color=C["i2c"])
    s.text(gx + 6, gy + 118, "右  左摇杆 →", 13, weight="700", color=C["i2c"])
    s.text(gx + 104, gy + 5, "前", 13, weight="700", color=C["v6"])
    s.text(cx, cy + 5, "底座", 12, "middle", "700")
    s.note(40, 510, 1, "上下、左右、前后是 3 个移动方向轴（6 个方向）；另外还能调俯仰（右摇杆 ←→）、旋转（十字键 ←→）和夹爪")
    s.note(40, 540, 2, "左右移动由底座 J1 转动 + 手臂伸缩一起完成：固件的逆运动学自动计算，你只管推摇杆")
    s.save("fig9-directions.svg")


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
    fig_directions()
