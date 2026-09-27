// 仿生手 · 零件库 (part library)
//
// finger.scad、palm.scad、assembly.scad 都 include 这个文件，零件模块全在这里。
// 尺寸参数在 common.scad（舵机、销轴、手指截面）和本文件的 [手掌] / [拇指] 两节。

include <common.scad>

// ================= 手指零件 =================
ZB0 = RJ + CL + 0.8;   // 近节 / 远节实心段的起点（让开对面零件的关节圆）

module rrect(p0, p1, r = 2) {             // 2D 圆角矩形，p0 / p1 为对角
  translate(p0 + [r, r]) offset(r) square(p1 - p0 - [2 * r, 2 * r]);
}

// ---------------- 近节 P ----------------
module proximal(name) {
  F = finger_params(name);
  L1 = F[0];
  L2 = F[1];
  DIRECT = F[4] == "direct";
  D_PT = polar(D_R, F[5]);
  ZB1 = L1 - (norm(C_OFF) + RB + CL + 0.5);  // 近节实心段的终点（让开远节叉子上的 C 点凸耳）
  difference() {
    union() {
      // 中段实心
      side(-W / 2, W / 2) rrect([ZB0, -T / 2], [ZB1, T / 2]);
      // 远端舌头（插进远节的叉子）
      side(-TG / 2, TG / 2) hull() {
        disc([L1, 0], RJ);
        translate([ZB1 - 1, -T / 2]) square([1, T]);
      }
      if (DIRECT) {
        // 拇指：近端整宽，内侧（-x）面贴在舵机臂上
        side(-W / 2, W / 2) hull() {
          disc([0, 0], RJ + 1);
          translate([ZB0, -T / 2]) square([1, T]);
        }
      } else {
        // 其他手指：近端是叉子（夹住手掌的舌头），叉子上带驱动销 D 的凸耳
        for (s = [-1, 1]) side(s > 0 ? CHEEK_IN : -W / 2, s > 0 ? W / 2 : -CHEEK_IN) {
          hull() { disc([0, 0], RJ); translate([ZB0, -T / 2]) square([1, T]); }
          hull() { disc([0, 0], RJ); disc(D_PT, RB); }
        }
      }
    }
    x_hole([L1, 0], PIN);
    if (DIRECT) {
      horn_pocket();
    } else {
      x_hole([0, 0], PIN);
      x_hole(D_PT, PIN);
    }
  }
}

// 拇指近节内侧面上的舵机臂槽：单臂舵盘平放进去，臂朝指尖方向。
// 先用舵盘自带的 2 颗小自攻螺丝把舵盘从槽里拧到近节上，再整体套上舵机输出轴，
// 最后从外侧的大孔伸进螺丝刀拧中心螺丝。
HORN_ARM = 17;      // 舵盘中心到臂端的长度
HORN_T = 2.0;       // 舵盘厚度
HORN_HUB = 7.5;     // 舵盘中心圆台直径
module horn_pocket() {
  translate([-W / 2 - 1, 0, 0]) rotate([0, 90, 0]) cylinder(d = HORN_HUB + 0.4, h = HORN_T + 1 + 0.6);
  side(-W / 2 - 1, -W / 2 + HORN_T + 0.3) hull() {
    disc([0, 0], 3.3);
    disc([HORN_ARM - 2, 0], 2.2);
  }
  x_hole([0, 0], 4.5);                               // 拧中心螺丝的通孔
  for (r = [HORN_R, HORN_R + 3]) x_hole([r, 0], PIN_TAP, -W / 2, 0);
}

// ---------------- 远节 M（中节 + 指尖）----------------
module distal(name) {
  L2 = finger_params(name)[1];
  tip_r = T / 2 - 1;
  difference() {
    union() {
      side(-W / 2, W / 2) hull() {
        translate([ZB0, -T / 2]) square([1, T]);
        disc([L2 - tip_r, -1], tip_r);
      }
      for (s = [-1, 1]) side(s > 0 ? CHEEK_IN : -W / 2, s > 0 ? W / 2 : -CHEEK_IN) {
        hull() { disc([0, 0], RJ); translate([ZB0, -T / 2]) square([1, T]); }
        if (s > 0) hull() { disc([0, 0], RJ); disc(C_OFF, RB); }   // C 点凸耳只在 +x 一侧
      }
    }
    x_hole([0, 0], PIN);
    x_hole(C_OFF, PIN_TAP, CHEEK_IN + 0.8, W / 2 + 1);             // 盲孔，别打穿到舌头那边
    // 指腹防滑槽
    for (z = [L2 - 22 : 4 : L2 - 8]) side(-W / 2 - 1, W / 2 + 1) translate([z, -T / 2 - 0.2]) square([1.2, 0.8]);
  }
}

// ---------------- 联动杆 / 驱动连杆（平放打印，孔竖直）----------------
module coupling_link(name) { bar(finger_params(name)[2], LINK_T); }
module drive_rod(name) { bar(finger_params(name)[3], ROD_T); }


// ================= 手掌、盖板、拇指支架 =================
/* [手掌] */
PALM_X = 46;         // 手掌半宽
PALM_Y = 16;         // 手掌半厚
PALM_Z0 = -114;      // 手腕端（底面，装在底座上）
PALM_FRONT = -16.5;  // 手指叉子让位槽的后沿
TONGUE_Y = 4.5;      // 手掌舌头的半高（上下要给驱动连杆让路）
COVER_T = 2.5;       // 盖板厚度
M3_TAP = 2.5;        // M3 自攻底孔

/* [拇指] */
THUMB_A = [-50, -22];      // 拇指转动轴（X, Y）
THUMB_HUB_Z = -83;         // 拇指支架圆盘的后表面（贴在舵盘上）
THUMB_ZT = 27;             // 支架坐标里：拇指 MCP 销到圆盘后表面的距离
THUMB_XBOT = -4.5;         // 支架坐标里：弯曲舵机底面的位置（让出中心螺丝的通道）
HUB_R = 11;                // 支架圆盘半径（MG90S 圆舵盘直径约 20）

/* [Hidden] */
LAYER = [1, -1, -1, 1];    // 食指 / 小指在手背层，中指 / 无名指在手心层
SDIR = [-1, 1, -1, 1];     // 舵机输出轴朝向（-1 = 朝 -X）
SERVO_ZC = SERVO_Z - SV_SH;                   // 舵机机身中心的 Z
R_T = -(THUMB_XBOT - (H_ROD - ROD_T / 2)) + W / 2;   // 转动轴 → 拇指中心面

// ---------- 手指舵机的几何（手掌坐标）----------
function xb(i) = FINGER_X[i] - SDIR[i] * H_ROD;         // 机身底面
function yl(i) = LAYER[i] * LAYER_Y;                    // 层中心
function xspan(i, a, b) = let(p = xb(i) + SDIR[i] * a, q = xb(i) + SDIR[i] * b) [min(p, q), max(p, q)];

module xbox(xr, yr, zr) { translate([xr[0], yr[0], zr[0]]) cube([xr[1] - xr[0], yr[1] - yr[0], zr[1] - zr[0]]); }

// 一个手指舵机需要挖掉的空间：机身、安装耳、齿轮凸台 + 舵盘、连杆通道、走线槽。
module servo_cut(i) {
  s = LAYER[i];
  open = s > 0 ? [yl(i) - SV_W / 2 - CL, 40] : [-40, yl(i) + SV_W / 2 + CL];
  // 机身
  xbox(xspan(i, -CL, SV_H + CL), open, [SERVO_ZC - SV_L / 2 - CL, SERVO_ZC + SV_L / 2 + CL]);
  // 安装耳（整圈凸缘）
  xbox(xspan(i, SV_FZ - CL, SV_FZ + SV_FT + CL), open, [SERVO_ZC - SV_FL / 2 - CL, SERVO_ZC + SV_FL / 2 + CL]);
  // 凸台 + 舵盘的转动空间（只在本层那一半，免得挖穿另一层）
  intersection() {
    translate([0, yl(i), SERVO_Z]) rotate([0, 90, 0])
      translate([0, 0, min(xspan(i, SV_H, H_ROD + ROD_T / 2 + 0.7))])
        cylinder(r = HORN_R + RB + 1.5, h = abs(H_ROD + ROD_T / 2 + 0.7 - SV_H));
    xbox([-60, 60], s > 0 ? [0.5, 40] : [-40, -0.5], [-80, 0]);
  }
  xbox(xspan(i, SV_H, H_ROD + ROD_T / 2 + 0.7), s > 0 ? [yl(i), 40] : [-40, yl(i)],
       [SERVO_Z - HORN_R - RB - 1.5, SERVO_Z + HORN_R + RB + 1.5]);
  // 驱动连杆通道：从舵盘一直通到手指
  xbox([FINGER_X[i] - ROD_T / 2 - 0.7, FINGER_X[i] + ROD_T / 2 + 0.7],
       s > 0 ? [TONGUE_Y + 0.3, 40] : [-40, -TONGUE_Y - 0.3],
       [SERVO_Z - HORN_R - RB - 1.5, 5]);
  // 走线槽：机身尾部 → 手腕空腔
  cx = xspan(i, 1, 7);
  xbox(cx, s > 0 ? [yl(i) - 3, 40] : [-40, yl(i) + 3], [-76, SERVO_ZC - SV_L / 2]);
  xbox([min(cx[0], -18), max(cx[1], 18)], s > 0 ? [yl(i) - 3, 40] : [-40, yl(i) + 3], [-76, -70]);
}

// 拇指转动舵机（输出轴沿 +Z，机身长度方向沿 X）
module thumb_servo_cut() {
  zc_top = THUMB_HUB_Z - 2 - 4.5;          // 舵盘 2 mm + 凸台 4.5 mm
  zb = zc_top - SV_H;
  xc = THUMB_A[0] + SV_SH;
  yr = [THUMB_A[1] - SV_W / 2 - CL, THUMB_A[1] + SV_W / 2 + CL];
  xbox([xc - SV_L / 2 - CL, xc + SV_L / 2 + CL], [-45, yr[1]], [zb - CL, zc_top + CL]);
  xbox([xc - SV_FL / 2 - CL, xc + SV_FL / 2 + CL], [-45, yr[1]], [zb + SV_FZ - CL, zb + SV_FZ + SV_FT + CL]);
  xbox([xc + SV_L / 2 - 4, -16], [THUMB_A[1] - 3, -8], [zb, zb + 6]);   // 走线 → 手腕空腔
}

THENAR_X = [-62, -22];
THENAR_Y = [-33.5 + COVER_T, -10];
THENAR_Z = [PALM_Z0, -84.5];
THENAR_SCREWS = [[-27, -88], [-27, -99], [-59.5, -100]];   // (x, z)
COVER_Z = [-70, -20];
DORSAL_SCREWS = [[0, -66], [0, -24]];
PALMAR_SCREWS = [[32, -66], [32, -24]];                      // 右侧；左侧 x 取负

module palm() {
  difference() {
    union() {
      xbox([-PALM_X, PALM_X], [-PALM_Y, PALM_Y], [PALM_Z0, 1]);
      xbox(THENAR_X, THENAR_Y, THENAR_Z);                               // 大鱼际（拇指转动舵机）
      xbox([PALM_X - 2, FINGER_X[3] + LINK_X1 + 0.4 + 4.4], [-8 - RB - 2, -8 + RB + 2], [-20, 1]);   // 小指 G 点耳朵
    }
    for (i = [0 : 3]) {
      xf = FINGER_X[i];
      // 手指叉子的让位槽（舌头稍后补回去）
      xbox([xf - W / 2 - 0.8, xf + W / 2 + 0.8], [-40, 40], [PALM_FRONT, 5]);
      // 联动杆的让位槽
      xbox([xf + LINK_X0 - 0.4, xf + LINK_X1 + 0.4], [-40, 40], [G_OFF[0] - RB - 1, 5]);
      servo_cut(i);
      // G 点自攻孔（从联动杆那一侧打进隔板）
      translate([xf + LINK_X1 + 0.4 - 0.1, G_OFF[1], G_OFF[0]]) rotate([0, 90, 0]) cylinder(d = PIN_TAP, h = 6);
    }
    xbox([-60, -PALM_X + 1.7], [-40, 40], [PALM_FRONT, 5]);           // 食指外侧的薄边去掉
    // 拇指：转动舵机、支架圆盘的转动空间
    thumb_servo_cut();
    translate([THUMB_A[0], THUMB_A[1], THUMB_HUB_Z - 7]) cylinder(r = SV_BOSS_R + 1, h = 5);   // 齿轮凸台
    translate([THUMB_A[0], THUMB_A[1], THUMB_HUB_Z - 3]) cylinder(r = HUB_R + 1.5, h = 16.5);
    // 手腕空腔（舵机线都从这里出去）+ 底座螺丝孔
    xbox([-20, 20], [-10, 10], [PALM_Z0 - 1, -70]);
    for (x = [-28, 28], y = [-9, 9]) translate([x, y, PALM_Z0 - 1]) cylinder(d = M3_TAP, h = 13);
    // 盖板螺丝孔
    for (p = DORSAL_SCREWS) translate([p[0], PALM_Y - 10, p[1]]) rotate([-90, 0, 0]) cylinder(d = M3_TAP, h = 11);
    for (p = PALMAR_SCREWS, s = [-1, 1]) translate([s * p[0], -PALM_Y + 10, p[1]]) rotate([90, 0, 0]) cylinder(d = M3_TAP, h = 11);
    for (p = THENAR_SCREWS) translate([p[0], THENAR_Y[0] + 10, p[1]]) rotate([90, 0, 0]) cylinder(d = M3_TAP, h = 11);
  }
  // 舌头（MCP 销）
  for (i = [0 : 3]) {
    xf = FINGER_X[i];
    difference() {
      intersection() {
        side(xf - TG / 2, xf + TG / 2) hull() { disc([0, 0], RJ); translate([PALM_FRONT - 1, -RJ]) square([1, 2 * RJ]); }
        xbox([-60, 60], [-TONGUE_Y, TONGUE_Y], [-40, 10]);
      }
      x_hole([0, 0], PIN);
    }
  }
}

// ---------- 盖板 ----------
// 盖板压在舵机上，把舵机按在槽里。垫块刚好顶到舵机侧面；太松就贴一层双面泡棉胶。
module cover(xr, screws, s) {
  pad = PALM_Y - (LAYER_Y + SV_W / 2);
  difference() {
    union() {
      xbox(xr, [0, COVER_T], COVER_Z);
      for (i = [0 : 3]) if (LAYER[i] == s) {
        r = xspan(i, 1, SV_H - 1);
        if (r[0] >= xr[0] - 1 && r[1] <= xr[1] + 1)
          xbox(r, [-pad, 0.5], [SERVO_ZC - SV_L / 2 + 1, SERVO_ZC + SV_L / 2 - 1]);
      }
    }
    for (p = screws) translate([p[0], -5, p[1]]) rotate([-90, 0, 0]) { cylinder(d = 3.4, h = 20); translate([0, 0, 5 + COVER_T - 1.8]) cylinder(d = 6, h = 5); }
  }
}
module cover_dorsal() { cover([-28, 28], DORSAL_SCREWS, 1); }
module cover_palmar() { cover([18, PALM_X], PALMAR_SCREWS, -1); }   // 两块一样：左边那块在盖板平面内转 180° 装

module thenar_cover() {
  pad = THENAR_Y[0] - (THUMB_A[1] - SV_W / 2);
  difference() {
    union() {
      xbox(THENAR_X, [0, COVER_T], THENAR_Z);
      xbox([THUMB_A[0] + SV_SH - SV_L / 2 + 1, THUMB_A[0] + SV_SH + SV_L / 2 - 1], [0, -pad], [THUMB_HUB_Z - 6.5 - SV_H + 1, THUMB_HUB_Z - 7.5]);
    }
    for (p = THENAR_SCREWS) translate([p[0], -5, p[1]]) rotate([-90, 0, 0]) { cylinder(d = 3.4, h = 20); translate([0, 0, 5 + COVER_T - 1.8]) cylinder(d = 6, h = 5); }
  }
}

// ---------- 拇指支架（支架坐标：原点在转动轴上、圆盘后表面；+Z 朝指尖；-X 朝外）----------
// 圆盘用 4 颗舵盘自带的小自攻螺丝固定在圆舵盘上（孔位对不上就用 1.5 mm 钻头现钻），
// 然后套上转动舵机的输出轴，从圆盘中心孔拧中心螺丝。
// 弯曲舵机从外侧（-X）放进托架，安装耳用 M2 自攻螺丝拧在托架的端面上。
module thumb_bracket() {
  body_z = [THUMB_ZT - SV_SH - SV_L / 2, THUMB_ZT - SV_SH + SV_L / 2];
  ear_z = [THUMB_ZT - SV_SH - SV_FL / 2, THUMB_ZT - SV_SH + SV_FL / 2];
  ear_face = THUMB_XBOT - SV_FZ;                 // 安装耳下表面（朝 +X）
  wall = SV_W / 2 + CL + 2.2;
  mcp = [THUMB_ZT, 0];
  xo = -R_T - LINK_X1 - 0.4;                     // G 点侧板内表面
  difference() {
    union() {
      cylinder(r = HUB_R, h = 3);
      // 托架：两侧壁 + 端面（托住安装耳），中间让出中心螺丝的通道
      for (s = [-1, 1]) xbox([ear_face, THUMB_XBOT + 2], s > 0 ? [SV_W / 2 + CL, wall] : [-wall, -SV_W / 2 - CL], [2.9, ear_z[1] + 2]);
      xbox([ear_face, ear_face + 4], [-wall, wall], [ear_z[0] - 2 < 3 ? 3 : ear_z[0] - 2, ear_z[1] + 2]);
      // 手背侧横梁 → 外侧 G 点侧板
      xbox([xo - 4.4, ear_face + 4], [SV_W / 2 + CL + 0.8, SV_W / 2 + CL + 4.8], [mcp[0] - 9, mcp[0] - 5]);
      xbox([xo - 4.4, xo], [G_OFF[1] - RB - 1.5, SV_W / 2 + CL + 4.8], [mcp[0] - 9, mcp[0] + G_OFF[0] + RB + 1]);
    }
    translate([0, 0, -1]) cylinder(d = 7.5, h = 5);                                  // 中心螺丝
    for (a = [45 : 90 : 360]) translate([8 * cos(a), 8 * sin(a), -1]) cylinder(d = PIN_TAP, h = 5);
    xbox([THUMB_XBOT - SV_H - 6, THUMB_XBOT + CL], [-SV_W / 2 - CL, SV_W / 2 + CL], [body_z[0] - CL, body_z[1] + CL]);   // 机身 + 凸台
    for (z = [THUMB_ZT - SV_SH - (SV_FL / 2 - 2.2), THUMB_ZT - SV_SH + (SV_FL / 2 - 2.2)])
      translate([ear_face - 0.1, 0, z]) rotate([0, 90, 0]) cylinder(d = PIN_TAP, h = 8);
    translate([xo + 0.1, G_OFF[1], mcp[0] + G_OFF[0]]) rotate([0, -90, 0]) cylinder(d = PIN_TAP, h = 6);
  }
}
