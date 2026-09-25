#!/usr/bin/env python3
"""Generate the wiring / layout diagrams (SVG) used by docs/assembly-guide.md.

    python docs/img/make_diagrams.py

Pure standard library; edit the drawing functions below and re-run.
"""
import os
from xml.sax.saxutils import escape

OUT = os.path.dirname(os.path.abspath(__file__))
FONT = "'PingFang SC','Microsoft YaHei','Noto Sans CJK SC','WenQuanYi Zen Hei',sans-serif"

C = {
    "bat": "#D23B2F",    # battery / SYS+ (7.4-8.4 V)
    "v5": "#E07B00",     # 5 V
    "v33": "#7A4CC2",    # 3.3 V
    "gnd": "#222222",    # ground
    "sig": "#1F6FB5",    # signals
    "spi": "#1E8A5A",    # SPI bus
    "bal": "#8A8A8A",    # balance lead
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
        self.parts.append(f'<circle cx="{x}" cy="{y}" r="11" fill="{C["bat"]}" stroke="#FFFFFF" stroke-width="2"/>')
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


def xiao(s, x, y, title="XIAO ESP32S3", sub="飞控（俯视，USB-C 朝上）"):
    """Seeed XIAO ESP32S3 drawn with its real pin order.
    Left column top->bottom: D0..D6, right column: 5V, GND, 3V3, D10, D9, D8, D7."""
    w, h = 170, 330
    s.box(x, y, w, h, title, sub)
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
    s = Svg(1100, 430, "图 0  系统总览", "谁和谁通信：G7 Pro 手柄 → 地面站 → 蝴蝶；手机通过地面站的 Wi-Fi 查看位置和画面")
    s.box(40, 130, 180, 110, "盖世小鸡 G7 Pro", "模式开关：蓝牙\nXbox 布局按键")
    s.box(380, 110, 220, 150, "地面站", "FireBeetle 2 ESP32-E\n+ 1S 锂电池\nWi-Fi 热点 Butterfly-GS")
    s.box(820, 110, 240, 150, "蝴蝶", "XIAO ESP32S3 飞控\n陀螺仪 · 气压计 · GPS\n2 × 舵机", fill=C["hi"])
    s.box(380, 320, 220, 80, "手机浏览器", "192.168.4.1：位置 / 轨迹 / 画面")
    s.box(820, 320, 240, 80, "可选：摄像头节点", "XIAO ESP32S3 Sense", dashed=True)
    s.box(40, 320, 180, 80, "可选：语音模块", "ASRPRO / CI1302", dashed=True)
    s.wire([(220, 185), (380, 185)], C["rf"], 3, True)
    s.text(300, 175, "蓝牙", 13, "middle", "700")
    s.wire([(600, 185), (820, 185)], C["rf"], 3, True)
    s.text(710, 175, "ESP-NOW 2.4 GHz", 13, "middle", "700")
    s.text(710, 205, "控制指令 50 Hz → / ← 遥测 20 Hz", 11.5, "middle", color=C["mute"])
    s.wire([(490, 260), (490, 320)], C["rf"], 3, True)
    s.text(500, 295, "Wi-Fi", 13, weight="700")
    s.wire([(820, 360), (600, 300), (600, 260)], C["rf"], 3, True)
    s.text(735, 318, "Wi-Fi 视频流", 12, "middle", color=C["mute"])
    s.wire([(220, 360), (300, 360), (300, 240), (380, 240)], C["sig"], 3)
    s.text(310, 300, "UART", 12, weight="700", color=C["sig"])
    s.save("fig0-overview.svg")


def fig_power_in():
    s = Svg(1100, 620, "图 1  机上电源输入：Type-C 充电 · 保护板 · 内置电池 · 电源开关",
            "电池内置不拆；充电模块接在开关前面，所以关机也能充电")
    # charger
    s.text(40, 100, "⇩ 手机充电器 5 V（USB-C）", 12, color=C["mute"])
    s.box(40, 110, 210, 160, "Type-C 充电模块", "IP2326 · 2S 升压充电\n充电电流 ≤ 180 mA")
    s.pin(250, 160, "BAT+", "right", C["bat"])
    s.pin(250, 230, "BAT−", "right", C["gnd"])
    s.pin(80, 270, "VBUS 焊点", "bottom", C["v5"])
    s.wire([(80, 270), (80, 299)], C["v5"])
    s.tag(40, 310, "VBUS → 充电检测（图 2）", C["v5"])
    # BMS
    s.box(390, 110, 210, 160, "2S 保护板（带均衡）", "过流保护 ≥ 5 A")
    s.pin(390, 160, "P+", "left", C["bat"])
    s.pin(390, 230, "P−", "left", C["gnd"])
    s.pin(600, 140, "B+", "right", C["bat"])
    s.pin(600, 190, "BM", "right", C["bal"])
    s.pin(600, 240, "B−", "right", C["gnd"])
    s.wire([(250, 160), (390, 160)], C["bat"])
    s.wire([(250, 230), (390, 230)], C["gnd"])
    # battery
    s.box(740, 90, 170, 200, "内置电池 2S", "7.4 V 180 mAh")
    for i, cy in enumerate((165, 230)):
        s.parts.append(f'<rect x="790" y="{cy - 16}" width="80" height="34" rx="4" fill="#FFFFFF" '
                       f'stroke="{C["boxline"]}"/>')
        s.text(830, cy + 5, f"电芯 {i + 1}", 11.5, "middle")
    s.pin(740, 140, "+", "left", C["bat"])
    s.pin(740, 190, "中", "left", C["bal"])
    s.pin(740, 240, "−", "left", C["gnd"])
    s.wire([(600, 140), (740, 140)], C["bat"])
    s.wire([(600, 190), (740, 190)], C["bal"], 2.5)
    s.wire([(600, 240), (740, 240)], C["gnd"])
    # outputs of the power stage
    s.dot(320, 160, C["bat"])
    s.dot(340, 230, C["gnd"])
    s.wire([(340, 230), (340, 330)], C["gnd"])
    s.tag(347, 330, "GND → 图 2", C["gnd"])
    # switch
    s.box(40, 380, 640, 190, "电源开关（P-MOS 软开关，只走微小电流的拨动开关控制大电流）", "")
    s.wire([(320, 160), (320, 355), (110, 355), (110, 470), (150, 470)], C["bat"])   # P+ -> S
    s.part(150, 445, 130, 50, "AO3401A")
    s.text(160, 440, "S(2)", 11, "start", "700")
    s.text(270, 440, "D(3)", 11, "end", "700")
    s.text(215, 512, "G(1)", 11, "middle", "700")
    s.wire([(280, 470), (560, 470), (560, 330), (700, 330)], C["bat"])              # D -> SYS+
    s.tag(707, 330, "SYS+ → 图 2（开关后的电源）", C["bat"])
    s.wire([(215, 495), (215, 535)], C["sig"], 2)                                  # G node
    s.dot(215, 535, C["sig"])
    s.wire([(110, 470), (110, 535), (130, 535)], C["bat"], 2)
    s.part(130, 523, 60, 24, "100kΩ")
    s.wire([(190, 535), (215, 535), (330, 535)], C["sig"], 2)
    s.part(330, 520, 90, 30, "拨动开关")
    s.wire([(420, 535), (460, 535)], C["gnd"], 2)
    s.tag(460, 535, "GND", C["gnd"])
    s.dot(110, 470, C["bat"])
    # notes
    s.note(740, 400, 1, "充电电流设为 ≤ 1C（180 mAh 电池 ≤ 180 mA）")
    s.note(740, 430, 2, "焊电池一次只接一根线：B− → BM → B+")
    s.note(740, 460, 3, "拨动开关闭合 = G 拉到 GND = 开机")
    s.note(740, 490, 4, "AO3401A 单脚是 3(D)，另一排左 1(G) 右 2(S)")
    s.note(740, 520, 5, "通电前先量 SYS+ 与 GND 不短路")
    s.legend(40, 600, [("电池 +", C["bat"], False), ("GND", C["gnd"], False), ("平衡线", C["bal"], False),
                       ("5 V", C["v5"], False), ("控制", C["sig"], False)])
    s.save("fig1-power-input.svg")


def fig_power_dist():
    s = Svg(1100, 660, "图 2  机上电源分配：舵机 · 降压 5 V · 飞控 · 电压 / 充电检测",
            "彩色标签表示“接到同名的线”（SYS+ 来自图 1 开关输出，GND 来自图 1 P−）")
    p = xiao(s, 470, 150)
    # rails
    s.wire([(40, 90), (1060, 90)], C["bat"], 4)
    s.tag(40, 70, "SYS+ 7.4–8.4 V", C["bat"])
    s.wire([(40, 610), (1060, 610)], C["gnd"], 4)
    s.tag(40, 632, "GND", C["gnd"])
    # servos
    for i, (bx, name, pin) in enumerate(((60, "左舵机", "D1"), (230, "右舵机", "D2"))):
        s.box(bx, 350, 140, 80, name, "红 + · 棕 − · 橙 信号")
        s.pin(bx + 25, 350, "+", "top", C["bat"])
        s.pin(bx + 115, 350, "信号", "top", C["sig"])
        s.pin(bx + 70, 430, "−", "bottom", C["gnd"])
        s.wire([(bx + 25, 350), (bx + 25, 90)], C["bat"])
        s.dot(bx + 25, 90, C["bat"])
        s.wire([(bx + 70, 430), (bx + 70, 610)], C["gnd"])
        s.dot(bx + 70, 610, C["gnd"])
        s.wire([(bx + 115, 350), (bx + 115, p[pin][1]), p[pin]], C["sig"])
    # 470 uF: + straight up to SYS+, - straight down to GND
    s.part(415, 500, 30, 50, "")
    s.text(408, 518, "470µF", 11.5, "end", "700")
    s.text(408, 534, "+ 朝上", 11, "end", color=C["mute"])
    s.wire([(430, 500), (430, 90)], C["bat"])
    s.dot(430, 90, C["bat"])
    s.wire([(430, 550), (430, 610)], C["gnd"])
    s.dot(430, 610, C["gnd"])
    # battery voltage divider -> D0
    s.part(300, 140, 70, 24, "200kΩ")
    s.part(300, 200, 70, 24, "100kΩ")
    s.wire([(335, 90), (335, 140)], C["bat"], 2)
    s.dot(335, 90, C["bat"])
    s.wire([(335, 164), (335, 200)], C["sig"], 2)
    s.wire([(335, 182), (452, 182), (452, p["D0"][1]), p["D0"]], C["sig"], 2)
    s.dot(335, 182, C["sig"])
    s.wire([(335, 224), (335, 250), (290, 250)], C["gnd"], 2)
    s.tag(290, 250, "GND", C["gnd"], "end")
    s.text(245, 160, "电池电压 → D0", 12, "end", "700", C["sig"])
    # charge detect -> D6 (node away from the servo ground wires)
    y6 = p["D6"][1]
    s.tag(40, y6, "VBUS（图 1）", C["v5"])
    s.wire([(150, y6), (190, y6)], C["v5"], 2)
    s.part(190, y6 - 12, 70, 24, "100kΩ")
    s.wire([(260, y6), p["D6"]], C["sig"], 2)
    s.dot(360, y6, C["sig"])
    s.wire([(360, y6), (360, 560)], C["sig"], 2)
    s.part(325, 560, 70, 24, "200kΩ")
    s.wire([(360, 584), (360, 610)], C["gnd"], 2)
    s.dot(360, 610, C["gnd"])
    s.text(190, y6 - 20, "充电检测 → D6", 12, "start", "700", C["sig"])
    # buck + diode -> 5V
    s.box(830, 140, 200, 110, "Mini-360 降压", "先调到 5.0 V 再接！")
    s.pin(870, 140, "IN+", "top", C["bat"])
    s.pin(990, 250, "IN− / OUT−", "bottom", C["gnd"])
    s.pin(830, 190, "OUT+", "left", C["v5"])
    s.wire([(870, 140), (870, 90)], C["bat"])
    s.dot(870, 90, C["bat"])
    s.wire([(990, 250), (990, 610)], C["gnd"])
    s.dot(990, 610, C["gnd"])
    s.part(700, 216, 80, 28, "SS14 ▶|")
    s.wire([(830, 190), (800, 190), (800, 230), (780, 230)], C["v5"])
    s.wire([(700, 230), p["5V"]], C["v5"])
    s.text(740, 266, "条纹端朝 XIAO", 11, "middle", color=C["mute"])
    s.wire([p["GND"], (760, p["GND"][1]), (760, 610)], C["gnd"])
    s.dot(760, 610, C["gnd"])
    # notes
    s.note(680, 470, 1, "Mini-360 先单独通电调到 5.0 V，再接 XIAO")
    s.note(680, 500, 2, "SS14 带条纹的一端（阴极）朝 XIAO 5V")
    s.note(680, 530, 3, "470 µF 贴近舵机插头，长脚 / 无条纹为 +")
    s.note(680, 560, 4, "高压舵机直接接 SYS+；6 V 舵机不能直接接")
    s.legend(40, 650, [("电池 +", C["bat"], False), ("GND", C["gnd"], False), ("5 V", C["v5"], False),
                       ("信号", C["sig"], False)])
    s.save("fig2-power-distribution.svg")


def fig_signals():
    s = Svg(1100, 700, "图 3  飞控信号接线：陀螺仪 · 气压计 · GPS",
            "陀螺仪和气压计共用 3.3 V、GND 和一组 SPI 线（绿色）；片选 CS 各自单独接（看同名标签）")
    p = xiao(s, 300, 170)
    s.text(385, 530, "5V、GND 的供电见图 2", 12, "middle", color=C["mute"])
    # left-side pins
    for n, lab in (("D0", "电池电压（图 2）"), ("D1", "左舵机信号（图 2）"), ("D2", "右舵机信号（图 2）"),
                   ("D6", "充电检测（图 2）")):
        x, y = p[n]
        s.wire([(x, y), (x - 30, y)], C["sig"], 2)
        s.text(x - 36, y + 4, lab, 12, "end", color=C["mute"])
    for n, lab in (("D3", "陀螺仪 CS"), ("D4", "气压计 CSB"), ("D5", "GPS RX（可不接）")):
        x, y = p[n]
        s.wire([(x, y), (x - 30, y)], C["sig"], 2.5)
        s.tag(x - 30, y, lab, C["sig"], "end")
    # buses: 3V3, GND, MOSI(D10), MISO(D9), SCK(D8)
    bus = {"3V3": (560, C["v33"]), "GND": (580, C["gnd"]), "D10": (600, C["spi"]), "D9": (620, C["spi"]),
           "D8": (640, C["spi"])}
    for n, (bx, col) in bus.items():
        s.wire([p[n], (bx, p[n][1])], col, 3)
        s.dot(bx, p[n][1], col)
    s.text(620, 548, "SPI 总线", 12, "middle", "700", C["spi"])
    # devices
    s.box(760, 90, 280, 220, "ICM-42688-P 陀螺仪", "SPI · 3.3 V · X 箭头朝机头")
    imu = {"VCC": 150, "GND": 180, "SCLK": 210, "SDO": 240, "SDI": 270}
    s.box(760, 340, 280, 210, "BMP280 气压计", "GY-BMP280-3.3 · SPI · 盖开孔海绵")
    baro = {"VCC": 400, "GND": 430, "SCL": 460, "SDO": 490, "SDA": 520}
    s.box(760, 580, 280, 100, "GPS（可选）", "M10 迷你 · 天线朝上")
    gps = {"VCC": 620, "GND": 650}
    net = {"VCC": "3V3", "GND": "GND", "SCLK": "D8", "SCL": "D8", "SDO": "D9", "SDI": "D10", "SDA": "D10"}
    rows = {k: [p[k][1]] for k in bus}
    for pins in (imu, baro, gps):
        for n, py in pins.items():
            bx, col = bus[net[n]]
            s.pin(760, py, n, "left", col)
            s.wire([(bx, py), (760, py)], col, 2.5)
            s.dot(bx, py, col)
            rows[net[n]].append(py)
    for n, (bx, col) in bus.items():
        s.wire([(bx, min(rows[n])), (bx, max(rows[n]))], col, 3)
    # chip selects and GPS UART
    s.pin(1040, 120, "CS", "right", C["sig"])
    s.wire([(1040, 120), (1060, 120)], C["sig"], 2.5)
    s.tag(1060, 120, "D3", C["sig"])
    s.pin(1040, 370, "CSB", "right", C["sig"])
    s.wire([(1040, 370), (1060, 370)], C["sig"], 2.5)
    s.tag(1060, 370, "D4", C["sig"])
    s.pin(1040, 620, "TX", "right", C["sig"])
    s.pin(1040, 650, "RX", "right", C["sig"])
    s.wire([(1040, 650), (1060, 650)], C["sig"], 2)
    s.tag(1060, 650, "D5", C["sig"])
    s.wire([(1040, 620), (1075, 620), (1075, 565), (680, 565), (680, p["D7"][1]), p["D7"]], C["sig"], 2.5)
    s.text(870, 560, "GPS TX → D7", 12, "middle", "700", C["sig"])
    # notes
    s.note(30, 600, 1, "SPI 线尽量短（≤ 10 cm），信号线不要和舵机电源线捆在一起")
    s.note(30, 630, 2, "三个模块都用 3.3 V；GPS 如果标注 5 V 供电，就改接 5 V")
    s.note(30, 660, 3, "同名标签相连：CS → D3，CSB → D4，GPS RX → D5")
    s.legend(30, 110, [("3.3 V", C["v33"], False), ("GND", C["gnd"], False), ("SPI", C["spi"], False),
                       ("信号", C["sig"], False)])
    s.save("fig3-signals.svg")


def fig_ground():
    s = Svg(1100, 520, "图 4  地面站：FireBeetle 2 ESP32-E + G7 Pro",
            "地面站只需要插电池；手柄和手机都是无线连接")
    s.box(420, 120, 250, 250, "FireBeetle 2 ESP32-E", "原版 ESP32（经典蓝牙 + BLE）\n地面站", fill=C["hi"])
    s.parts.append('<rect x="523" y="110" width="44" height="14" rx="4" fill="#B9C4C1"/>')
    s.text(545, 121, "USB-C", 9.5, "middle", "700")
    s.text(545, 92, "USB-C：烧录程序 / 给电池充电", 12, "middle", color=C["mute"])
    s.pin(545, 370, "PH2.0 电池座", "bottom", C["bat"])
    s.box(460, 420, 170, 70, "1S 锂电池", "3.7 V 500–1000 mAh")
    s.wire([(545, 370), (545, 420)], C["bat"])
    s.pin(420, 250, "GPIO16", "left", C["sig"])
    s.pin(420, 290, "3V3", "left", C["v33"])
    s.pin(420, 330, "GND", "left", C["gnd"])
    s.box(90, 220, 200, 130, "语音模块（可选）", "ASRPRO / CI1302\nUART 115200", dashed=True)
    s.pin(290, 250, "TX", "right", C["sig"])
    s.pin(290, 290, "VCC", "right", C["v33"])
    s.pin(290, 330, "GND", "right", C["gnd"])
    s.wire([(290, 250), (420, 250)], C["sig"], 2.5)
    s.wire([(290, 290), (420, 290)], C["v33"], 2.5)
    s.wire([(290, 330), (420, 330)], C["gnd"], 2.5)
    s.box(90, 100, 200, 90, "盖世小鸡 G7 Pro", "背面模式开关 → 蓝牙")
    s.wire([(290, 145), (420, 170)], C["rf"], 3, True)
    s.text(355, 145, "蓝牙", 13, "middle", "700")
    s.box(820, 110, 220, 90, "手机", "Wi-Fi：Butterfly-GS\n浏览器 192.168.4.1")
    s.wire([(670, 170), (820, 155)], C["rf"], 3, True)
    s.text(745, 150, "Wi-Fi", 13, "middle", "700")
    s.box(820, 260, 220, 90, "蝴蝶", "ESP-NOW 信道 1", fill=C["hi"])
    s.wire([(670, 290), (820, 300)], C["rf"], 3, True)
    s.text(745, 285, "ESP-NOW", 13, "middle", "700")
    s.note(700, 420, 1, "配对：模式开关拨蓝牙 → 短按 Xbox 键 → 长按配对键")
    s.note(700, 450, 2, "换手柄：串口输入 PAIR 后重新配对")
    s.note(700, 480, 3, "地面站必须是原版 ESP32（S3 / C3 不行）")
    s.save("fig4-ground-station.svg")


def fig_camera():
    s = Svg(1100, 400, "图 5  可选：摄像头供电接线",
            "摄像头从 Mini-360 的 5 V 输出取电；二选一")
    s.box(40, 120, 200, 110, "Mini-360（图 2）", "OUT+ 5.0 V")
    s.pin(240, 160, "OUT+", "right", C["v5"])
    s.pin(240, 200, "GND", "right", C["gnd"])
    s.box(460, 90, 270, 120, "方案 B：XIAO ESP32S3 Sense", "摄像头节点 · 带外置天线")
    s.pin(460, 130, "5V", "left", C["v5"])
    s.pin(460, 170, "GND", "left", C["gnd"])
    s.part(330, 116, 80, 28, "SS14 ▶|")
    s.wire([(240, 160), (290, 160), (290, 130), (330, 130)], C["v5"])
    s.wire([(410, 130), (460, 130)], C["v5"])
    s.wire([(240, 200), (300, 200), (300, 170), (460, 170)], C["gnd"])
    s.box(460, 250, 270, 100, "方案 A：5.8 GHz 一体摄像头", "AIO · 25 mW · 3.3–5 V")
    s.pin(460, 285, "VCC", "left", C["v5"])
    s.pin(460, 320, "GND", "left", C["gnd"])
    s.wire([(290, 160), (290, 285), (460, 285)], C["v5"])
    s.dot(290, 160, C["v5"])
    s.wire([(300, 200), (300, 320), (460, 320)], C["gnd"])
    s.dot(300, 200, C["gnd"])
    s.note(770, 130, 1, "B：画面在手机网页 192.168.4.1")
    s.note(770, 160, 2, "B：程序 camera_node，选 OPI PSRAM")
    s.note(770, 280, 3, "A：Android 手机 + OTG 接收器")
    s.note(770, 310, 4, "装在机头，下倾 10–20°，垫软胶")
    s.save("fig5-camera.svg")


def fig_layout():
    s = Svg(1200, 700, "图 6  机身布局（俯视）", "重的东西靠近重心；传感器远离舵机；左右严格对称")
    cx = 550
    # wings
    wing = C["hi"]
    for sgn in (-1, 1):
        s.parts.append(
            f'<path d="M{cx + sgn * 40},230 C{cx + sgn * 200},110 {cx + sgn * 380},120 {cx + sgn * 400},240 '
            f'C{cx + sgn * 330},330 {cx + sgn * 150},330 {cx + sgn * 40},300 Z" fill="{wing}" '
            f'stroke="#C9A55A" stroke-width="2"/>')
        s.parts.append(
            f'<path d="M{cx + sgn * 40},320 C{cx + sgn * 180},330 {cx + sgn * 300},420 {cx + sgn * 250},520 '
            f'C{cx + sgn * 170},560 {cx + sgn * 80},470 {cx + sgn * 40},380 Z" fill="{wing}" '
            f'stroke="#C9A55A" stroke-width="2"/>')
        s.wire([(cx + sgn * 40, 232), (cx + sgn * 400, 238)], "#555555", 3)
    s.text(cx - 330, 200, "前缘碳杆 Ø2 mm", 12, "middle", color=C["mute"])
    # body
    s.parts.append(f'<rect x="{cx - 40}" y="150" width="80" height="420" rx="30" fill="#E8EEEC" '
                   f'stroke="{C["boxline"]}" stroke-width="1.8"/>')
    s.text(cx, 140, "▲ 机头（陀螺仪 X 轴朝这边）", 13, "middle", "700")
    items = [
        (175, "摄像头（可选）", "#B9C4C1"),
        (225, "左 / 右舵机", "#9EC5E8"),
        (285, "XIAO 飞控", "#C9E4D4"),
        (335, "陀螺仪 ⊕ 重心", "#F2C9C4"),
        (385, "电池（前后可调）", "#F6D7A7"),
        (440, "降压 · 气压计（海绵）", "#E3D5F2"),
        (495, "GPS（天线朝上）", "#D6E4F0"),
        (545, "充电板 Type-C 口", "#F7E3B0"),
    ]
    for y, label, col in items:
        s.parts.append(f'<rect x="{cx - 34}" y="{y - 16}" width="68" height="32" rx="6" fill="{col}" '
                       f'stroke="{C["boxline"]}" stroke-width="1"/>')
        s.wire([(cx + 34, y), (cx + 420, y)], "#9AA7AB", 1.2)
        s.text(cx + 426, y + 4, label, 13, weight="600")
    s.parts.append(f'<circle cx="{cx}" cy="335" r="9" fill="none" stroke="{C["bat"]}" stroke-width="2.5"/>')
    s.wire([(cx - 9, 335), (cx + 9, 335)], C["bat"], 2)
    s.wire([(cx, 326), (cx, 344)], C["bat"], 2)
    s.note(40, 610, 1, "两个舵机轴心对称，舵机臂在中位时装上翅膀")
    s.note(40, 638, 2, "陀螺仪贴在重心附近，用 VHB 泡棉胶，远离舵机")
    s.note(40, 666, 3, "GPS 离 XIAO 天线 ≥ 3 cm；Type-C 口朝外方便插线")
    s.save("fig6-layout.svg")


def fig_body():
    s = Svg(1200, 660, "图 7  机身结构与材料（侧视）",
            "碳纤维管做主梁（又轻又硬），3D 打印件夹持舵机，薄托板承载电子件；机身总重目标 8–12 g")
    y0 = 330
    # keel tube
    s.parts.append(f'<rect x="170" y="{y0 - 6}" width="780" height="12" rx="6" fill="#3A3F42"/>')
    s.text(200, y0 + 34, "① 主梁：碳纤维方管 4×4 mm（或圆管 Ø4/Ø3 mm），长 130–150 mm，约 1.5 g",
           13, "start", "700")
    # servo mount (front)
    s.parts.append(f'<rect x="190" y="{y0 - 70}" width="150" height="64" rx="8" fill="#9EC5E8" '
                   f'stroke="{C["boxline"]}" stroke-width="1.5"/>')
    s.text(265, y0 - 44, "舵机 ×2", 13, "middle", "700")
    s.text(265, y0 - 26, "（左右各一，背靠背）", 11, "middle", color=C["mute"])
    s.parts.append(f'<path d="M180,{y0 - 80} h170 v94 h-170 z" fill="none" stroke="#C97A1E" '
                   f'stroke-width="3" stroke-dasharray="8 4"/>')
    s.wire([(265, y0 - 80), (265, 150)], "#C97A1E", 1.5)
    s.text(40, 142, "② 舵机座：3D 打印 PETG，夹紧主梁，约 3–4 g", 13, "start", "700", "#A5620F")
    # wing root rods from servo horns
    s.wire([(300, y0 - 70), (420, 120)], "#555555", 4)
    s.text(430, 118, "翅膀前缘 Ø2 mm 碳杆 → 舵机臂", 12, color=C["mute"])
    # deck with electronics
    s.parts.append(f'<rect x="400" y="{y0 - 22}" width="330" height="10" rx="3" fill="#6B6F72"/>')
    comps = [(410, 58, "XIAO", "#C9E4D4"), (472, 58, "陀螺仪", "#F2C9C4"), (534, 106, "电池 2S", "#F6D7A7"),
             (644, 82, "降压 / 气压", "#E3D5F2")]
    for x, w, lab, col in comps:
        s.parts.append(f'<rect x="{x}" y="{y0 - 60}" width="{w}" height="38" rx="5" fill="{col}" '
                       f'stroke="{C["boxline"]}"/>')
        s.text(x + w / 2, y0 - 36, lab, 11.5, "middle", "600")
    s.wire([(715, y0 - 12), (715, 440)], "#6B6F72", 1.5)
    s.text(715, 458, "③ 电子托板：0.5 mm 碳纤维板 或 1.5 mm 轻木板，约 60×18 mm，1–2 g", 13, "middle", "700")
    s.text(715, 478, "碳板导电：先贴一层 Kapton 胶带或双面胶再放电子件", 12, "middle", color=C["bat"])
    # tail: charger + GPS
    s.parts.append(f'<rect x="800" y="{y0 - 50}" width="110" height="38" rx="5" fill="#F7E3B0" '
                   f'stroke="{C["boxline"]}"/>')
    s.text(855, y0 - 26, "充电板 Type-C", 11.5, "middle", "600")
    s.parts.append(f'<rect x="800" y="{y0 - 90}" width="110" height="34" rx="5" fill="#D6E4F0" '
                   f'stroke="{C["boxline"]}"/>')
    s.text(855, y0 - 68, "GPS（可选）", 11, "middle", "600")
    # head / tail pieces
    s.parts.append(f'<ellipse cx="150" cy="{y0}" rx="40" ry="26" fill="#FFF3D6" stroke="#C9A55A" stroke-width="2"/>')
    s.text(150, y0 + 4, "头", 12, "middle", "700")
    s.parts.append(f'<path d="M950,{y0 - 14} L1040,{y0} L950,{y0 + 14} Z" fill="#FFF3D6" stroke="#C9A55A" '
                   f'stroke-width="2"/>')
    s.wire([(150, y0 + 26), (150, 400)], "#9AA7AB", 1.2)
    s.text(40, 418, "④ 头 / 尾：3D 打印薄壳或轻木（可选）", 12, "start", color=C["mute"])
    s.text(40, y0 - 40, "◀ 机头", 13, "start", "700")
    # joints
    s.wire([(690, y0 + 6), (690, 400)], "#9AA7AB", 1.2)
    s.text(680, 404, "托板与主梁：细线缠绕 + 502，或 2 mm 扎带", 12, "end", color=C["mute"])
    # notes
    s.note(40, 540, 1, "主梁选“碳纤维管”：同样重量下比轻木硬很多，扑翼时机身不会扭，控制更准")
    s.note(40, 570, 2, "XIAO 的天线不要被碳板包住（碳纤维会挡无线信号），天线露在托板边缘外")
    s.note(40, 600, 3, "切碳管 / 碳板：笔刀绕圈划断或细锯锯断；戴口罩，边缘用砂纸磨圆")
    s.note(40, 630, 4, "连接处：细线缠 5–6 圈 + 502 渗透，比只用胶结实得多")
    s.save("fig7-body-structure.svg")


def fig_wing_structure():
    s = Svg(1200, 720, "图 8  翅膀结构（左侧一片，俯视）",
            "前翅和后翅在翼根连在一起，由同一个舵机带动；尺寸按 Mech-Butterfly 的 A3 模板，下面数字仅供参考")
    film = "#FFF3D6"
    edge = "#C9A55A"
    # membrane (forewing + hindwing)
    s.parts.append(f'<path d="M820,228 C640,120 380,110 240,150 C190,190 200,250 260,285 C420,320 640,315 820,300 Z" '
                   f'fill="{film}" stroke="{edge}" stroke-width="2"/>')
    s.parts.append(f'<path d="M820,318 C700,330 560,400 520,500 C500,560 560,590 620,570 C720,520 780,430 820,360 Z" '
                   f'fill="{film}" stroke="{edge}" stroke-width="2"/>')
    # rods
    s.wire([(822, 230), (242, 152)], "#2F3437", 6)                      # leading edge Ø2
    for tx, ty in ((262, 282), (430, 312), (620, 312)):                 # forewing veins Ø1.5
        s.wire([(815, 250), (tx, ty)], "#4A5054", 3.5)
    for tx, ty in ((540, 520), (660, 520)):                             # hindwing veins Ø1.5
        s.wire([(815, 335), (tx, ty)], "#4A5054", 3.5)
    s.wire([(815, 320), (522, 498)], "#2F3437", 4.5)                    # hindwing leading rod
    for a, b in (((330, 205), (360, 293)), ((470, 190), (520, 303)), ((640, 210), (690, 300)),
                 ((600, 430), (680, 450)), ((580, 470), (640, 510))):   # cross veins Ø1.0
        s.wire([a, b], "#7A8084", 2)
    # root joint
    s.parts.append(f'<rect x="812" y="215" width="46" height="160" rx="8" fill="#9EC5E8" stroke="{C["boxline"]}" '
                   f'stroke-width="1.5"/>')
    s.text(835, 395, "翼根座", 12, "middle", "700")
    s.text(835, 411, "→ 舵机臂", 11, "middle", color=C["mute"])
    # dimensions
    s.wire([(242, 110), (822, 110)], C["mute"], 1.2)
    s.wire([(242, 102), (242, 118)], C["mute"], 1.2)
    s.wire([(822, 102), (822, 118)], C["mute"], 1.2)
    s.text(532, 102, "半翼展约 380–420 mm（两侧加机身约 85 cm）", 12.5, "middle", "600")
    s.wire([(210, 150), (210, 285)], C["mute"], 1.2)
    s.text(200, 222, "前翅宽", 12, "end", "600")
    s.text(200, 238, "约 150–180 mm", 11.5, "end", color=C["mute"])
    # numbered markers on the drawing
    for n, (mx, my) in enumerate(((420, 176), (560, 290), (655, 250), (680, 408), (330, 240), (835, 300)), 1):
        s.marker(mx, my, n)
    # legend column
    items = [
        ("前缘：Ø2.0 mm 碳纤维实心杆", "最粗、受力最大，从翼根一直通到翼尖"),
        ("翅脉：Ø1.5 mm 碳杆", "从翼根呈扇形散开，撑住翼面"),
        ("横脉：Ø1.0 mm 碳杆", "把相邻的翅脉连起来，防止翼面扭曲"),
        ("后翅杆：Ø1.5 mm 碳杆", "后翅的主杆，和前翅插在同一个翼根座上"),
        ("翼膜：聚酯薄膜 12–15 µm", "绷平贴在碳杆上，越薄越轻"),
        ("翼根座：3D 打印 PETG", "所有碳杆都插进它的孔里，再装到舵机臂上"),
    ]
    for i, (t1, t2) in enumerate(items):
        yy = 150 + i * 52
        s.marker(915, yy - 4, i + 1)
        s.text(935, yy, t1, 13, weight="700")
        s.text(935, yy + 18, t2, 11.5, color=C["mute"])
    # inset A: cross-section
    s.box(40, 480, 330, 200, "剖面 A：膜和碳杆怎么贴", "")
    s.parts.append(f'<circle cx="205" cy="590" r="16" fill="#2F3437"/>')
    s.wire([(70, 572), (340, 572)], edge, 3)
    for gx in (192, 205, 218):
        s.parts.append(f'<circle cx="{gx}" cy="575" r="3" fill="{C["v5"]}"/>')
    s.text(205, 630, "膜在碳杆上面，胶只涂在碳杆上", 12, "middle", "600")
    s.text(205, 648, "（整张膜都涂胶会变重、起皱）", 11.5, "middle", color=C["mute"])
    s.text(344, 568, "膜", 11.5, "end", color=C["mute"])
    s.text(230, 596, "碳杆", 11.5, color="#FFFFFF")
    # inset B: root joint
    s.box(420, 590, 740, 110, "", "")
    s.text(440, 616, "剖面 B：翼根连接", 14, weight="700")
    s.parts.append(f'<rect x="700" y="620" width="120" height="40" rx="6" fill="#9EC5E8" stroke="{C["boxline"]}"/>')
    s.wire([(560, 640), (760, 640)], "#2F3437", 6)
    for i in range(8):
        x = 690 + i * 5
        s.wire([(x, 628), (x + 6, 652)], "#C0392B", 1.4)
    s.text(620, 675, "碳杆插进孔里 15 mm", 11.5, "middle", color=C["mute"])
    s.text(840, 636, "① 孔径比杆大 0.1–0.2 mm", 12)
    s.text(840, 656, "② 插入前杆头用砂纸打毛", 12)
    s.text(840, 676, "③ 孔口细线缠 6–8 圈 + 502 渗透", 12)
    s.save("fig8-wing-structure.svg")


def fig_wing_steps():
    s = Svg(1200, 760, "图 9  翅膀制作 6 步", "每一只翅膀都按这 6 步做；左右两只最后要称重配对")
    film, edge, rod = "#FFF3D6", "#C9A55A", "#2F3437"
    panels = [
        ("打印模板", ["A3 纸 1:1 打印模板（不要缩放）", "下面垫切割垫，上面盖一层保鲜膜防粘"]),
        ("摆放碳杆", ["沿轮廓摆：前缘 Ø2，翅脉 Ø1.5 / Ø1.0", "交叉处各点一小滴 502，用胶带临时压住"]),
        ("喷胶贴膜", ["只在碳杆上喷 3M 77（先用纸遮住别处）", "膜从中间向四周抹平、轻轻绷紧"]),
        ("修边", ["沿碳杆外侧留 2 mm 剪下多余的膜", "边缘可以向下包住碳杆再点胶，更结实"]),
        ("称重配对", ["两只翅膀重量差 ≤ 0.5 g", "重的那只修掉一点边缘薄膜"]),
        ("装到舵机", ["先 servo 0 0 让舵机回中位，再装", "扑动全程不能碰到机身和电线"]),
    ]
    pw, ph = 370, 330
    for i, (title, lines) in enumerate(panels):
        col, row = i % 3, i // 3
        x, y = 30 + col * (pw + 25), 80 + row * (ph + 20)
        s.parts.append(f'<rect x="{x}" y="{y}" width="{pw}" height="{ph}" rx="10" fill="{C["box"]}" '
                       f'stroke="{C["boxline"]}" stroke-width="1.4"/>')
        s.note(x + 12, y + 30, i + 1, "")
        s.text(x + 44, y + 31, title, 15, weight="700")
        for j, line in enumerate(lines):
            s.text(x + 16, y + ph - 40 + j * 20, line, 12.5, color=C["ink"] if j == 0 else C["mute"])
        ox, oy = x + 45, y + 60          # drawing area ~280 x 190
        outline = (f'M{ox + 250},{oy + 50} C{ox + 170},{oy} {ox + 60},{oy} {ox + 20},{oy + 25} '
                   f'C{ox},{oy + 60} {ox + 10},{oy + 100} {ox + 50},{oy + 115} '
                   f'C{ox + 120},{oy + 135} {ox + 200},{oy + 120} {ox + 250},{oy + 105} Z')
        rods = [((ox + 250, oy + 52), (ox + 22, oy + 26), 5), ((ox + 245, oy + 70), (ox + 55, oy + 112), 3),
                ((ox + 245, oy + 70), (ox + 150, oy + 125), 3), ((ox + 100, oy + 22), (ox + 110, oy + 122), 2)]
        if i == 0:   # template on paper
            s.parts.append(f'<rect x="{ox - 15}" y="{oy - 10}" width="300" height="180" fill="#FFFFFF" '
                           f'stroke="#B9C4C1"/>')
            s.text(ox + 270, oy + 160, "A3", 12, "end", "700", C["mute"])
            s.parts.append(f'<path d="{outline}" fill="none" stroke="{C["mute"]}" stroke-width="2" '
                           f'stroke-dasharray="6 4"/>')
        elif i == 1:  # rods on template
            s.parts.append(f'<path d="{outline}" fill="none" stroke="{C["mute"]}" stroke-width="1.5" '
                           f'stroke-dasharray="6 4"/>')
            for a, b, w in rods:
                s.wire([a, b], rod, w)
            for dx, dy in ((100, 22), (104, 72), (245, 70)):
                s.parts.append(f'<circle cx="{ox + dx}" cy="{oy + dy}" r="5" fill="{C["v5"]}"/>')
            s.text(ox + 120, oy + 160, "● = 一小滴 502", 12, "middle", color=C["v5"])
        elif i == 2:  # film over rods with arrows
            s.parts.append(f'<rect x="{ox - 15}" y="{oy - 15}" width="295" height="160" fill="{film}" '
                           f'fill-opacity="0.8" stroke="{edge}" stroke-dasharray="4 3"/>')
            for a, b, w in rods:
                s.wire([a, b], rod, w)
            for dx, dy in ((-70, -40), (70, -40), (-70, 40), (70, 40), (-95, 0), (95, 0)):
                s.arrow(ox + 135 + dx * 0.25, oy + 65 + dy * 0.25, ox + 135 + dx, oy + 65 + dy, C["sig"], 2.5)
            s.text(ox + 135, oy + 168, "从中间向外抹平", 12, "middle", color=C["sig"])
        elif i == 3:  # trim
            s.parts.append(f'<path d="{outline}" fill="{film}" stroke="{edge}" stroke-width="2"/>')
            for a, b, w in rods:
                s.wire([a, b], rod, w)
            s.parts.append(f'<path d="{outline}" fill="none" stroke="{C["bat"]}" stroke-width="1.5" '
                           f'stroke-dasharray="5 4" transform="translate({ox + 135},{oy + 65}) scale(1.08) '
                           f'translate({-(ox + 135)},{-(oy + 65)})"/>')
            s.text(ox + 20, oy + 160, "✂ 红虚线 = 剪切线（留 2 mm）", 12, color=C["bat"])
        elif i == 4:  # scale with two wings
            s.parts.append(f'<rect x="{ox + 20}" y="{oy + 120}" width="240" height="40" rx="6" fill="#DDE3E1" '
                           f'stroke="{C["boxline"]}"/>')
            s.parts.append(f'<rect x="{ox + 95}" y="{oy + 128}" width="90" height="24" rx="3" fill="#1B2A2F"/>')
            s.text(ox + 140, oy + 145, "7.84 g", 13, "middle", "700", "#7CF2A0")
            for dx in (40, 150):
                s.parts.append(f'<path d="M{ox + dx + 90},{oy + 60} C{ox + dx + 60},{oy + 20} {ox + dx + 20},{oy + 20} '
                               f'{ox + dx},{oy + 40} C{ox + dx + 10},{oy + 90} {ox + dx + 60},{oy + 100} '
                               f'{ox + dx + 90},{oy + 90} Z" fill="{film}" stroke="{edge}" stroke-width="2"/>')
            s.text(ox + 85, oy + 18, "左 7.84 g", 12, "middle", "600")
            s.text(ox + 195, oy + 18, "右 7.61 g", 12, "middle", "600")
            s.text(ox + 140, oy + 180, "差 0.23 g ✓", 12, "middle", "700", "#2E7D52")
        else:  # attach to servo
            s.parts.append(f'<rect x="{ox + 190}" y="{oy + 60}" width="70" height="60" rx="6" fill="#9EC5E8" '
                           f'stroke="{C["boxline"]}"/>')
            s.text(ox + 225, oy + 95, "舵机", 12, "middle", "700")
            s.parts.append(f'<rect x="{ox + 180}" y="{oy + 40}" width="46" height="18" rx="4" fill="#FFFFFF" '
                           f'stroke="{C["boxline"]}"/>')
            s.text(ox + 203, oy + 35, "舵机臂", 11, "middle", color=C["mute"])
            s.wire([(ox + 200, oy + 48), (ox + 10, oy + 8)], rod, 5)
            s.parts.append(f'<path d="M{ox + 190},{oy + 46} C{ox + 120},{oy - 5} {ox + 40},{oy} {ox + 10},{oy + 10} '
                           f'C{ox + 40},{oy + 60} {ox + 120},{oy + 70} {ox + 190},{oy + 56} Z" fill="{film}" '
                           f'fill-opacity="0.7" stroke="{edge}" stroke-width="1.5"/>')
            s.wire([(ox + 60, oy + 150), (ox + 60, oy + 110)], C["sig"], 2)
            s.wire([(ox + 60, oy + 110), (ox + 52, oy + 122)], C["sig"], 2)
            s.wire([(ox + 60, oy + 110), (ox + 68, oy + 122)], C["sig"], 2)
            s.text(ox + 76, oy + 150, "中位时翅膀略向上（center 10°）", 11.5, color=C["sig"])
    s.save("fig9-wing-steps.svg")


if __name__ == "__main__":
    fig_overview()
    fig_power_in()
    fig_power_dist()
    fig_signals()
    fig_ground()
    fig_camera()
    fig_layout()
    fig_body()
    fig_wing_structure()
    fig_wing_steps()
    print("diagrams written to", OUT)
