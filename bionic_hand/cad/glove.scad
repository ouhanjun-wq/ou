// 仿生手 · 操控手套（外骨骼）(control glove)
//
// 手背上绑一块板，板上 6 个电位器：4 个手指各一个（测 MCP 关节弯曲），拇指 2 个（弯曲 + 转动）。
// 每个手指：电位器轴 P → 曲柄 → 连杆 → 套在近节指骨上的指环。手指一弯，电位器就转。
// 四连杆尺寸在 linkage_params.scad（GLOVE_*），由 tools/finger_linkage.py 校核：
// 电位器转角单调、行程 ≈ 130°（约 440 个 ADC 读数）、传动角 ≥ 30°。
//
// 电位器：WH148 B10K（直径 16 mm，M7 螺纹套，6 mm D 形轴，轴长 15 mm），买卖家焊好杜邦线的。
//
// 导出：
//   openscad -D 'part="plate"'      -o glove_plate.stl glove.scad     （右手；左手加 -D 'left=true'）
//   openscad -D 'part="crank"'      -o glove_crank.stl glove.scad     （打印 6 个）
//   openscad -D 'part="link"'       -o glove_link.stl glove.scad      （打印 6 个）
//   openscad -D 'part="ring"' -D 'ring_d=19' -o glove_ring_M.stl glove.scad  （打印 4 个；拇指用 thumb_ring）
//   openscad -D 'part="thumb_ring"' -o glove_thumb_ring.stl glove.scad
//
// 打印：PETG 或 PLA，层高 0.2，3 圈壁，25% 填充。底板平放；曲柄、连杆平放；指环平放。

include <common.scad>

part = "plate";      // [plate, crank, link, ring, thumb_ring, all]
left = false;        // 左手版：整块镜像

/* [手的尺寸（量你自己的手）] */
GLOVE_PITCH = 20;    // 相邻两个指关节（MCP）中心距
BACK_H = 14;         // 手背皮肤到 MCP 关节轴的高度（含 2 mm 泡棉）
ring_d = 19;         // 指环内径：量近节指骨最粗处，S 17 / M 19 / L 21
thumb_d = 22;        // 拇指指环内径

/* [电位器 WH148] */
POT_BUSH = 7.4;      // M7 螺纹套的孔
POT_BODY_R = 8.5;    // 电位器外壳半径（留间隙）
D_SHAFT = 6.0;       // D 形轴直径
D_FLAT = 4.5;        // D 形轴削平处的厚度

/* [Hidden] */
PLATE_T = 3;
PLATE_Y = BACK_H;                        // 底板下表面（手背坐标：原点在 MCP 轴，Y 向上）
TOWER_T = 3;
CRANK_T = 4;
GLINK_T = 3;
FX = [-1.5, -0.5, 0.5, 1.5] * GLOVE_PITCH;   // 食指 → 小指（右手，手背朝上看，拇指在 -X）
// 每根手指的侧向排布（相对手指中心线）：指环凸台 | 连杆 | 曲柄 | 螺母 | 支架 | 电位器外壳
X_LINK = 2.5;                      // 连杆 x ∈ [2.5, 5.5]
X_CRANK = X_LINK + GLINK_T;        // 曲柄 x ∈ [5.5, 9.5]
X_TOWER = 14;                      // 支架 x ∈ [14, 17]
THUMB_FLEX = [-58, -38];           // 拇指弯曲电位器（x, z）
THUMB_ROT = [-46, -64];            // 拇指转动电位器（x, z），轴竖直朝上

module xbox(xr, yr, zr) { translate([xr[0], yr[0], zr[0]]) cube([xr[1] - xr[0], yr[1] - yr[0], zr[1] - zr[0]]); }

// 一个电位器支架：竖直的墙，孔沿 X
module tower(x0) {
  difference() {
    side(x0, x0 + TOWER_T) hull() {
      disc(GLOVE_POT, POT_BODY_R + 1);
      translate([GLOVE_POT[0] - 11, PLATE_Y + PLATE_T - 0.5]) square([22, 1]);
    }
    x_hole(GLOVE_POT, POT_BUSH);
  }
}

module plate() {
  difference() {
    union() {
      // 手背板
      xbox([-GLOVE_PITCH * 2 - 6, GLOVE_PITCH * 1.5 + X_TOWER + TOWER_T + 12], [PLATE_Y, PLATE_Y + PLATE_T], [-99, 8]);
      // 拇指翼
      hull() {
        xbox([-GLOVE_PITCH * 2 - 6, -GLOVE_PITCH * 2 - 5], [PLATE_Y, PLATE_Y + PLATE_T], [-78, -20]);
        translate([THUMB_FLEX[0] - 6, PLATE_Y, THUMB_FLEX[1] - 14]) cube([1, PLATE_T, 30]);
        translate([THUMB_ROT[0], PLATE_Y, THUMB_ROT[1]]) rotate([-90, 0, 0]) cylinder(r = 14, h = PLATE_T);
      }
      for (x = FX) tower(x + X_TOWER);
      // 拇指弯曲：同样的支架，放在拇指翼上
      translate([THUMB_FLEX[0] - FX[0], 0, THUMB_FLEX[1]]) tower(FX[0] + X_TOWER);
      // 拇指转动：水平搁板，电位器从上面插，轴朝下，曲柄在搁板和底板之间转
      translate([THUMB_ROT[0], 0, THUMB_ROT[1]]) difference() {
        union() {
          xbox([-11, 11], [PLATE_Y + PLATE_T + 9, PLATE_Y + PLATE_T + 12], [-11, 11]);
          for (s = [-1, 1]) xbox([s * 11 - (s > 0 ? 3 : 0), s * 11 + (s > 0 ? 0 : 3)], [PLATE_Y + PLATE_T - 0.5, PLATE_Y + PLATE_T + 12], [-11, 11]);
        }
        translate([0, PLATE_Y, 0]) rotate([-90, 0, 0]) cylinder(d = POT_BUSH, h = 30);
      }
    }
    // 魔术贴带子槽（20 mm 宽）
    for (z = [-86, -12]) for (x = [-GLOVE_PITCH * 2 - 1, GLOVE_PITCH * 1.5 + X_TOWER + TOWER_T + 7])   // 后面那条在拇指机构后面
      xbox([x - 1.5, x + 1.5], [PLATE_Y - 1, PLATE_Y + PLATE_T + 1], [z - 11, z + 11]);
    // RJ45 转接板的扎带孔
    for (x = [-12, 12], z = [-50, -30]) translate([x, PLATE_Y - 1, z]) rotate([-90, 0, 0]) cylinder(d = 3.5, h = PLATE_T + 2);
  }
}

// 曲柄：一头是 D 形孔套在电位器轴上，一头是 M2 销孔
module crank() {
  difference() {
    side(0, CRANK_T) hull() { disc([0, 0], 6); disc([GLOVE_CRANK, 0], RB); }
    intersection() {
      x_hole([0, 0], D_SHAFT + 0.2);
      side(-1, CRANK_T + 1) translate([-D_SHAFT, -(D_FLAT + 0.2) / 2]) square([2 * D_SHAFT, D_FLAT + 0.2]);
    }
    x_hole([GLOVE_CRANK, 0], PIN);
  }
}

// 指环：C 形，套在近节指骨上，开口朝手心（-Y）；顶上的凸台带销孔，离指环中心 GLOVE_RING[1]。
module ring(d = ring_d, side_boss = false) {
  w = 8;
  r_out = d / 2 + 2.5;
  difference() {
    union() {
      translate([0, 0, -w / 2]) linear_extrude(w) difference() { circle(r = r_out); circle(d = d); }
      side(-3, X_LINK - 0.3) hull() {
        translate([-w / 2, r_out - 1]) square([w, 1]);
        disc([0, GLOVE_RING[1]], RB);
      }
      if (side_boss) translate([-r_out - 5, -4, -w / 2]) cube([6, 8, w]);   // 拇指转动连杆接这里
    }
    translate([0, 0, -w]) linear_extrude(2 * w) polygon([[0, 0], [-r_out * 2, -r_out * 2], [r_out * 2, -r_out * 2]]);   // 开口 90°
    x_hole([0, GLOVE_RING[1]], PIN_TAP);
    if (side_boss) translate([-r_out - 2.5, 0, 0]) rotate([-90, 0, 0]) cylinder(d = PIN_TAP, h = 20, center = true);
  }
}

module glove_link() { bar(GLOVE_LINK, GLINK_T); }

module mirror_if(m) { if (m) mirror([1, 0, 0]) children(); else children(); }

if (part == "plate") mirror_if(left) rotate([90, 0, 0]) translate([0, -PLATE_Y, 0]) plate();
else if (part == "crank") translate([0, 0, CRANK_T]) rotate([0, 90, 0]) crank();
else if (part == "link") rotate([0, -90, 0]) glove_link();
else if (part == "ring") translate([0, 0, 4]) ring(ring_d);
else if (part == "thumb_ring") translate([0, 0, 4]) ring(thumb_d, true);
else {
  // 预览：手背板 + 手指伸直时的曲柄、连杆、指环
  mirror_if(left) {
    color("lightsteelblue") plate();
    for (x = FX) translate([x, 0, 0]) {
      k = GLOVE_POT + GLOVE_CRANK * [0, 1];
      color("orange") translate([X_CRANK, 0, 0]) translate([0, GLOVE_POT[1], GLOVE_POT[0]]) rotate([-90, 0, 0]) crank();
      color("gold") translate([X_LINK, k[1], k[0]]) rotate([-atan2(GLOVE_RING[1] - k[1], GLOVE_RING[0] - k[0]), 0, 0]) glove_link();
      color("white") translate([0, 0, GLOVE_RING[0]]) ring(ring_d);
    }
  }
}
