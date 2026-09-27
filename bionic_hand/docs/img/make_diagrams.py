#!/usr/bin/env python3
"""Generate the bionic-hand diagrams (SVG) used by the docs in bionic_hand/docs/.

    python bionic_hand/docs/img/make_diagrams.py

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





import math
import sys

sys.path.insert(0, os.path.join(OUT, "..", "..", "tools"))
import finger_linkage as fl  # noqa: E402  (linkage numbers come from the design tool)

C["pot"] = "#1E8A5A"

CH = [("A0", "D3", "食指"), ("A1", "D5", "中指"), ("A2", "D6", "无名指"), ("A3", "D9", "小指"),
      ("A4", "D10", "拇指弯曲"), ("A5", "D11", "拇指转动")]
# RJ45 (T568B colours): pin -> (colour name, hex, net)
RJ = [(1, "白橙", "#F2A65A", "A0 食指"), (2, "橙", "#E07B00", "A1 中指"), (3, "白绿", "#8CCB8C", "A2 无名指"),
      (4, "蓝", "#1F4FB5", "5V"), (5, "白蓝", "#8FA8E0", "GND"), (6, "绿", "#1E8A5A", "A3 小指"),
      (7, "白棕", "#C9A27E", "A4 拇指弯曲"), (8, "棕", "#7A4A1E", "A5 拇指转动")]


def fig_overview():
    s = Svg(1100, 540, "图 0  系统总览：手套 → 网线 → Uno → 6 个舵机",
            "手套上 6 个电位器量你的手指；Uno 每 20 ms 读一次、换算、滤波，再让机械手的 6 个舵机转到对应位置")
    s.box(40, 110, 200, 150, "操控手套", "6 × WH148 B10K 电位器\n4 指 + 拇指弯曲 + 拇指转动\n迷你面包板汇 5V / GND")
    s.box(290, 150, 150, 70, "RJ45 转接板", "手套端 · 螺丝端子")
    s.box(490, 150, 150, 70, "RJ45 转接板", "机械手底座端")
    s.box(700, 110, 220, 170, "Arduino Uno R3", "+ Sensor Shield V5.0\n读 A0–A5 · 标定 · 滤波\n限位 · 限速 · 手势\n设置存 EEPROM", fill=C["hi"])
    s.box(970, 110, 110, 170, "机械手", "6 × MG90S\n食指 中指\n无名指 小指\n拇指弯曲\n拇指转动")
    s.box(700, 360, 220, 90, "舵机电源", "6 V ≥ 5 A 适配器\n（做过机械臂：直接用它的 6 V + 急停）")
    s.box(420, 360, 220, 90, "电脑 / 充电宝", "USB 5 V 给 Uno 供电\n电脑还能用串口命令调试")
    s.box(40, 360, 200, 90, "可选：按键模块", "D2 · 短按切模式\n长按 2 秒 = 引导标定", dashed=True)
    s.wire([(240, 185), (290, 185)], C["pot"], 3)
    s.wire([(440, 185), (490, 185)], C["pot"], 5)
    s.text(465, 172, "1 m 网线", 12, "middle", "700", C["pot"])
    s.text(465, 242, "8 芯：6 路信号 + 5V + GND", 11.5, "middle", color=C["mute"])
    s.wire([(640, 185), (700, 185)], C["pot"], 3)
    s.arrow(920, 195, 968, 195, C["sig"], 3)
    s.text(944, 185, "PWM", 11, "middle", "700", C["sig"])
    s.wire([(810, 360), (810, 280)], C["v6"], 3.5)
    s.text(820, 330, "6 V → 扩展板接线端子", 12, weight="700", color=C["v6"])
    s.wire([(640, 405), (670, 405), (670, 250), (700, 250)], C["v5"], 2.5)
    s.text(676, 330, "USB", 12, weight="700", color=C["v5"])
    s.wire([(240, 405), (320, 405), (320, 300), (720, 300), (720, 280)], C["sig"], 2.5, True)
    s.note(40, 495, 1, "全程不用焊：电位器买带线的，网线两头用 RJ45 螺丝端子转接板")
    s.note(560, 495, 2, "舵机大电流只走扩展板端子，不经过 Uno（SEL 跳帽拔掉）")
    s.legend(40, 528, [("手套信号 / 5V", C["pot"], False), ("舵机信号", C["sig"], False), ("6 V 舵机电", C["v6"], False),
                       ("USB 5 V", C["v5"], False), ("可选", C["sig"], True)])
    s.save("fig0-overview.svg")


def fig_hand_wiring():
    s = Svg(1100, 720, "图 1  机械手端接线：Uno + Sensor Shield V5.0",
            "扩展板直接插在 Uno 上；舵机插头、网线转接板都用杜邦线 / 螺丝端子，不用焊")
    s.box(360, 110, 380, 440, "Sensor Shield V5.0", "（插在 Uno R3 上）", fill=C["hi"], title_dy=26)
    # RJ45 board on the left
    s.box(40, 130, 200, 330, "RJ45 转接板（底座端）", "", title_dy=24)
    for i, (pin, cname, col, net) in enumerate(RJ):
        y = 175 + i * 34
        s.parts.append(f'<rect x="206" y="{y - 8}" width="26" height="16" rx="3" fill="{col}"/>')
        s.text(60, y + 4, f"{pin}  {cname}", 12, weight="600")
        target = net.split()[0]
        if target == "5V":
            ty, label = 470, "Uno 5V 针"
        elif target == "GND":
            ty, label = 500, "GND"
        else:
            ty, label = None, target
        if ty is None:
            yy = 150 + ["A0", "A1", "A2", "A3", "A4", "A5"].index(target) * 30
            s.wire([(232, y), (300, y), (300, yy), (360, yy)], col, 2.5)
            s.pin(360, yy, f"{target} 的 S 针", "left", col)
        else:
            s.wire([(232, y), (280 + i * 4, y), (280 + i * 4, ty), (360, ty)], col, 2.5)
            s.pin(360, ty, label, "left", col)
    # servos on the right
    for i, (a, d, name) in enumerate(CH):
        y = 150 + i * 50
        s.pin(740, y, f"{d}  G V S", "right", C["sig"])
        s.wire([(740, y), (860, y)], C["sig"], 3)
        s.box(860, y - 18, 200, 36, f"{name} 舵机", "", title_dy=23)
    s.text(800, 136, "棕 红 橙 → G V S", 11.5, "middle", color=C["mute"])
    # power terminal
    s.pin(640, 550, "接线端子 VCC / GND", "bottom", C["v6"])
    s.wire([(640, 550), (640, 620)], C["v6"], 4)
    s.box(560, 620, 200, 60, "6 V ≥ 5 A", "适配器 / 机械臂的 6 V", title_dy=24)
    s.box(760, 470, 150, 60, "SEL 跳帽", "拔掉！", fill="#FBE3E0", title_dy=24)
    s.pin(360, 530, "D2（可选按键 → GND）", "left", C["sig"])
    s.note(40, 600, 1, "网线 4 号蓝线（5V）接 Uno 的 5V 针，")
    s.text(68, 622, "不要接 A0–A5 那几排的 V 针：拔掉 SEL 后，", 12.5, color=C["mute"])
    s.text(68, 642, "有的扩展板那一排 V 是 6 V 舵机电，会烧模拟口", 12.5, color=C["mute"])
    s.note(40, 672, 2, "5 号白蓝（GND）接任意 G 针")
    s.note(800, 600, 3, "舵机插头：棕 = G，红 = V，橙 = S")
    s.note(800, 630, 4, "舵机电和 Uno 的 GND 在扩展板上是通的")
    s.save("fig1-hand-wiring.svg")


def fig_glove_wiring():
    s = Svg(1100, 600, "图 2  手套端接线：6 个电位器 → 迷你面包板 → RJ45 转接板",
            "每个电位器 3 根线：两边的脚接 5V / GND（哪边都行，标定会自动认方向），中间脚是信号")
    s.box(40, 90, 250, 450, "6 × WH148 B10K", "", title_dy=24)
    s.box(360, 90, 300, 450, "迷你面包板（学习套件）", "", title_dy=24)
    s.box(760, 90, 300, 450, "RJ45 转接板（手套端）", "", title_dy=24)
    # rails
    s.wire([(380, 125), (640, 125)], C["v5"], 6)
    s.text(650, 118, "5V 母线", 11.5, "end", "700", C["v5"])
    s.wire([(380, 520), (640, 520)], C["gnd"], 6)
    s.text(650, 512, "GND 母线", 11.5, "end", "700", C["gnd"])
    names = ["食指", "中指", "无名指", "小指", "拇指弯曲", "拇指转动"]
    rjpin = [1, 2, 3, 6, 7, 8]
    for i, n in enumerate(names):
        y = 170 + i * 60
        s.parts.append(f'<circle cx="80" cy="{y}" r="16" fill="#DDE6EA" stroke="{C["boxline"]}" stroke-width="1.4"/>')
        s.text(104, y + 4, n, 12.5, weight="600")
        for dy, lab in ((-12, "边"), (0, "中"), (12, "边")):
            s.text(282, y + dy + 4, lab, 10, "end", color=C["mute"])
        xv, xg = 400 + i * 10, 470 + i * 10
        s.wire([(290, y - 12), (xv, y - 12), (xv, 125)], C["v5"], 2)
        s.wire([(290, y + 12), (xg, y + 12), (xg, 520)], C["gnd"], 2)
        pin, cname, col, net = RJ[rjpin[i] - 1]
        yy = 150 + (rjpin[i] - 1) * 44
        xs = 690 + i * 9
        s.wire([(290, y), (xs, y), (xs, yy), (760, yy)], col, 2.6)
    for i, (pin, cname, col, net) in enumerate(RJ):
        s.pin(760, 150 + i * 44, f"{pin} {cname} · {net}", "left", col)
    s.wire([(640, 125), (745, 125), (745, 282), (760, 282)], C["v5"], 3)
    s.wire([(640, 520), (750, 520), (750, 326), (760, 326)], C["gnd"], 3)
    s.note(40, 575, 1, "网线两头的颜色要一一对应（都按 T568B），拧螺丝前再对一遍")
    s.note(620, 575, 2, "电位器线不够长就接一根公对母杜邦线")
    s.save("fig2-glove-wiring.svg")


def fig_linkage():
    s = Svg(1100, 600, "图 3  一根手指的两个四连杆（食指，MCP 弯 40°）",
            "驱动连杆：舵盘 H 推 / 拉近节上的 D 点 → 近节绕 MCP 转；联动杆 GC：近节一转，远节自动跟着弯")
    f = fl.Finger("index", *fl.FINGERS["index"])
    t = 40.0
    tm = f.theta_m(t)
    phi = f.phi_for(t)
    k, ox, oy = 3.2, 560, 300

    def P(c):
        return (ox + k * c.real, oy - k * c.imag)

    O = 0j
    B = f.l1 * fl.rot(t)
    Cp = B + fl.rot(tm) * fl.C_OFF
    tip = f.tip(t, tm)
    D = f.drive_pin(t)
    H = f.horn(phi)
    S = f.S
    G = f.G
    # palm outline
    x0, y0 = P(complex(-75, 22))
    x1, y1 = P(complex(-6, -22))
    s.parts.append(f'<rect x="{x0}" y="{y0}" width="{x1 - x0}" height="{y1 - y0}" rx="8" fill="#E8EEF2" stroke="{C["boxline"]}" stroke-width="1.2"/>')
    s.text(x0 + 12, y1 - 12, "手掌（侧视，手背朝上）", 12.5, color=C["mute"])
    # segments
    s.wire([P(O), P(B)], "#9AA5A8", 22)
    s.wire([P(B), P(tip)], "#C8CDD0", 22)
    s.wire([P(G), P(Cp)], "#E07B00", 5)
    s.wire([P(H), P(D)], "#D23B2F", 5)
    s.wire([P(S), P(H)], "#555555", 5)
    for name, c, dx, dy in (("O  MCP", O, 10, -10), ("B  PIP", B, 12, -8), ("C", Cp, 10, -8), ("G", G, -26, 18),
                            ("D", D, 10, -10), ("H 舵盘", H, -60, -10), ("S 舵机轴", S, -40, 30)):
        x, y = P(c)
        s.parts.append(f'<circle cx="{x}" cy="{y}" r="5" fill="#FFFFFF" stroke="{C["ink"]}" stroke-width="2"/>')
        s.text(x + dx, y + dy, name, 13, weight="700")
    x, y = P(tip)
    s.text(x + 8, y + 18, "指尖", 13, weight="700")
    s.text(40, 110, "近节 P = OB（L₁ = 42 mm）", 13)
    s.text(40, 132, "远节 M = B → 指尖（L₂ = 46 mm）", 13)
    s.text(40, 154, f"联动杆 GC = {f.link:.1f} mm（橙）", 13, color="#E07B00")
    s.text(40, 176, f"驱动连杆 HD = {f.rod:.1f} mm（红）", 13, color="#D23B2F")
    s.text(40, 198, f"舵盘 SH = {fl.HORN_R:.0f} mm（灰）", 13)
    s.text(40, 230, f"此时 θ₁ = {t:.0f}°，PIP 相对弯曲 θ_M − θ₁ = {tm - t:.0f}°", 13, weight="700")
    s.text(40, 252, f"舵机从伸直转过 {abs(phi - f.phi0):.0f}°", 13, weight="700")
    rows = f.sweep(step=5)
    s.text(40, 470, "全行程（tools/finger_linkage.py 算出并校核）：", 13, weight="700")
    txt = "   ".join(f"θ₁ {r[0]:.0f}° → PIP {r[2]:.0f}°" for r in rows[::6] + [rows[-1]])
    s.text(40, 494, txt, 12.5, color=C["mute"])
    s.text(40, 520, "传动角全程 ≥ 34°，舵机行程 107°，指尖 1 N 需要 ≤ 1.16 kg·cm（MG90S 堵转 2.0 kg·cm）", 12.5,
           color=C["mute"])
    s.save("fig3-linkage.svg")


if __name__ == "__main__":
    fig_overview()
    fig_hand_wiring()
    fig_glove_wiring()
    fig_linkage()
