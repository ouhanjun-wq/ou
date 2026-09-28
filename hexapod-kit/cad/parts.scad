// 18 舵机六足 · 3D 打印机身 · 所有零件的模块（装配坐标）
// 各个 *.scad 包装文件调用这里的模块，并把零件转成适合打印 / 下单的姿态。
include <params.scad>

$fn = 48;

// ---------------- 减重镂空（省打印费） ----------------
// 零件只有 3–4 mm 厚，做不了内部空心（壁会薄到打不出来，还会困住树脂 / 粉末），
// 所以在不受力的地方打六边形通孔：嘉立创树脂 / 尼龙按体积计价，镂空多少就省多少。
/* [减重镂空] */
lighten = true;           // false = 全部实心（更结实，也更贵）
lighten_d = 10;           // 六边形孔的外接圆直径 (mm)
lighten_pitch = 12;       // 孔中心距，三角形排布 (mm)；筋宽约 pitch - 0.87 * d
lighten_rim = 3.5;         // 外边框宽度 (mm)
lighten_keep = 2.5;        // 孔离其他孔位、槽口至少留多少材料 (mm)

module hex_lattice_2d(r = 80) {
  dy = lighten_pitch * sqrt(3) / 2;
  for (j = [-ceil(r / dy) : ceil(r / dy)], i = [-ceil(r / lighten_pitch) - 1 : ceil(r / lighten_pitch) + 1])
    translate([(i + (j % 2 == 0 ? 0 : 0.5)) * lighten_pitch, j * dy]) rotate(30) circle(d = lighten_d, $fn = 6);
}

// 减重孔：六边形阵列 ∩（外形往里缩 lighten_rim）−（要保留实心的区域，往外扩 lighten_keep）。
// 第一个子对象 = 零件外形，第二个 = 要保留实心的区域。最后去掉 1.6 mm 以下的碎孔。
module lighten_holes_2d() {
  if (lighten)
    offset(r = 0.8) offset(r = -0.8) intersection() {
      hex_lattice_2d();
      difference() {
        offset(delta = -lighten_rim) children(0);
        offset(r = lighten_keep) children(1);
      }
    }
}

// ---------------- 前方传感器 ----------------
/* [摄像头模块：亚博 ESP32-S3 WiFi 图传模块 Lite（YB-MEV04），量你自己的] */
cam_w = 35;               // 板子宽度（左右）(mm)
cam_h = 46;               // 板子高度（上下）(mm)
cam_d = 12;               // 板子厚度，含背面排针 / 排母 (mm)
cam_gap = 0.6;            // 托架比板子大多少 (mm)
cam_base_h = 3;           // 托架底面离甲板上表面 (mm)
/* [激光雷达 M1C1-Mini 平台] */
lidar_standoff_h = 50;    // 甲板到雷达平台的 M3 铜柱长度 (mm)
lidar_holes = [];         // 雷达底座安装孔 [[x, y], ...]（平台中心为原点，X+ = 机头）；量好再填，空 = 只用扎带
lidar_hole_d = 2.7;       // M2.5 螺丝过孔 (mm)
lidar_cable_d = 16;       // 平台中间的走线孔 (mm)

// ---------------- 基节支架 coxa ----------------
// 装在基节舵机的舵机臂上（在舵机上面），侧面的竖板夹住大腿舵机。
top_t = 4.0;              // 顶板厚度 (mm)
hub_r = 10;               // 转轴处圆盘半径 (mm)
coxa_horn_angle = -45;    // 舵机臂在顶板下面的朝向（腿坐标，°）

femur_servo_y = -bar_t / 2;                      // 大腿舵机臂贴合面的 Y
wall_y1 = femur_servo_y + ear_low;               // 竖板贴安装耳的一面
wall_y0 = wall_y1 - mount_t;
wall_x0 = coxa_len + shaft_off - ear_span / 2 - 3;
wall_x1 = coxa_len + shaft_off + ear_span / 2 + 3;
wall_z0 = -pocket_w / 2 - 3;
top_y_max = -7;           // 顶板在竖板一侧最多伸到的 Y（再往 +Y 会碰到抬起的大腿）

module femur_servo_frame() translate([coxa_len, femur_servo_y, 0]) rotate([-90, 0, 0]) children();

// 顶板 = 转轴处的圆盘（连到竖板的起点）+ 竖板上方的一条窄板。
// 不能整体 hull：那样顶板会伸到 y = 0 附近，大腿抬起来时会撞上。
module coxa_top_2d() {
  hull() {
    circle(r = hub_r);
    rotate(coxa_horn_angle) translate([horn_len - 2, 0]) circle(r = 4.5);
    translate([wall_x0, wall_y0]) square([2, top_y_max - wall_y0]);
  }
  translate([wall_x0, wall_y0]) square([wall_x1 - wall_x0, top_y_max - wall_y0]);
}

module coxa_bracket() {
  difference() {
    union() {
      translate([0, 0, coxa_horn_z]) linear_extrude(top_t) coxa_top_2d();
      translate([wall_x0, wall_y0, wall_z0]) cube([wall_x1 - wall_x0, mount_t, coxa_horn_z + top_t - wall_z0]);
      // 竖板和顶板之间的三角加强筋（在大腿舵机上方）
      for (x = [wall_x0, wall_x1 - 2.5])
        translate([x, wall_y1 - 0.01, coxa_horn_z + 0.01]) rotate([90, 0, 90]) linear_extrude(2.5)
          polygon([[0, 0], [top_y_max - wall_y1 + 0.01, 0], [0, -(coxa_horn_z - pocket_w / 2 - 0.8)]]);
    }
    translate([0, 0, coxa_horn_z]) rotate(coxa_horn_angle) horn_cut(top_t);
    femur_servo_frame() servo_mount_cut();
  }
}

// ---------------- 大腿 femur ----------------
// 一块平板：一头压大腿舵机的舵机臂（-Y 面），另一头压小腿舵机的舵机臂（+Y 面）。
femur_hub_r = 8.5;

module femur_bar() {
  difference() {
    translate([0, bar_t / 2, 0]) rotate([90, 0, 0]) linear_extrude(bar_t)
      hull() { circle(r = femur_hub_r); translate([femur_len, 0]) circle(r = femur_hub_r); }
    translate([0, -bar_t / 2, 0]) rotate([-90, 0, 0]) horn_cut(bar_t);
    translate([femur_len, bar_t / 2, 0]) rotate([90, 0, 0]) rotate([0, 0, 180]) horn_cut(bar_t);
    // 减重：两面各挖一个 1 mm 深的浅槽，中间留 2 mm。
    // +Y 面避开大腿端的舵机臂螺丝孔和小腿端的舵机臂凹槽，-Y 面反过来。
    if (lighten) {
      pocket_d = (bar_t - 2) / 2;
      a0 = horn_holes[1] + 3;                    // 大腿端螺丝孔之后
      a1 = femur_len - horn_len - 2;             // 小腿端舵机臂凹槽之前（+Y 面）
      b0 = horn_len + 2;                         // 大腿端舵机臂凹槽之后（-Y 面）
      b1 = femur_len - horn_holes[1] - 3;        // 小腿端螺丝孔之前
      translate([0, bar_t / 2 - pocket_d, 0]) rotate([-90, 0, 0]) linear_extrude(pocket_d + 0.01)
        hull() { translate([a0 + 4, 0]) circle(r = 4); translate([a1 - 4, 0]) circle(r = 4); }
      translate([0, -bar_t / 2 - 0.01, 0]) rotate([-90, 0, 0]) linear_extrude(pocket_d + 0.01)
        hull() { translate([b0 + 4, 0]) circle(r = 4); translate([b1 - 4, 0]) circle(r = 4); }
    }
  }
}

// ---------------- 小腿 tibia ----------------
// 小腿舵机装在上半截的安装板里，输出轴朝 -Y 压在大腿板上；下半截斜着收回到腿的中心平面，脚尖在 y = 0。
tib_plate_y0 = bar_t / 2 - ear_low;              // 安装板贴安装耳的一面
tib_x0 = shaft_off - ear_span / 2 - 3;
tib_x1 = shaft_off + ear_span / 2 + 3;
tib_half_w = pocket_w / 2 + 3;
foot_r = 3.5;
foot_t = 3.0;

module tibia_servo_frame() translate([0, bar_t / 2, 0]) rotate([90, 0, 0]) children();

module tibia_part() {
  difference() {
    union() {
      translate([tib_x0, tib_plate_y0, -tib_half_w]) cube([tib_x1 - tib_x0, mount_t, 2 * tib_half_w]);
      hull() {
        translate([tib_x1 - 1, tib_plate_y0, -tib_half_w]) cube([1, mount_t, 2 * tib_half_w]);
        translate([44, -foot_t / 2, -6]) cube([1, foot_t, 12]);
      }
      hull() {
        translate([44, -foot_t / 2, -6]) cube([1, foot_t, 12]);
        translate([tibia_len - foot_r, foot_t / 2, 0]) rotate([90, 0, 0]) cylinder(r = foot_r, h = foot_t);
      }
    }
    tibia_servo_frame() servo_mount_cut();
  }
}

// ---------------- 机身底板 ----------------
// 6 个基节舵机从上往下插进方孔，安装耳压在底板上表面；输出轴朝上，基节支架装在上面。
standoffs = [[30, 28], [30, -28], [-30, 28], [-30, -28]];
inner_inset = 14;         // 内六边形顶点：髋轴往里多少 (mm)
pad_r = 13;               // 每个髋轴周围的圆盘半径 (mm)

module coxa_servo_frame(l) translate([l[0], l[1], coxa_horn_z]) rotate(l[2]) rotate(180) children();

module inner_hex_2d(inset) {
  hull() for (l = legs) at_leg(l) translate([-inset, 0]) circle(r = 0.01);
}

module body_plate_2d() {
  union() {
    inner_hex_2d(inner_inset);
    for (l = legs) at_leg(l) {
      circle(r = pad_r);
      translate([-(shaft_off + ear_span / 2 + 3), -(pocket_w / 2 + 3)])
        square([shaft_off + ear_span / 2 + 3, pocket_w + 6]);
    }
  }
}

module body_plate() {
  difference() {
    translate([0, 0, plate_top_z - plate_t]) linear_extrude(plate_t) difference() {
      body_plate_2d();
      for (p = standoffs) translate(p) circle(d = m3_hole);
      for (s = [-1, 1]) translate([0, s * 24]) square([20, 3], center = true);        // 电池魔术贴扎带槽
      for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 20, sy * 34]) square([12, 6], center = true);  // 走线孔
      for (s = [-1, 1]) translate([s * 44, 0]) square([6, 20], center = true);         // 走线孔
      lighten_holes_2d() {
        body_plate_2d();
        union() {
          for (l = legs) at_leg(l) {
            circle(r = pad_r);
            translate([-(shaft_off + ear_span / 2 + 3), -(pocket_w / 2 + 3)])
              square([shaft_off + ear_span / 2 + 3, pocket_w + 6]);
          }
          for (p = standoffs) translate(p) circle(r = 4);
          for (s = [-1, 1]) translate([0, s * 24]) square([20, 3], center = true);
          for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 20, sy * 34]) square([12, 6], center = true);
          for (s = [-1, 1]) translate([s * 44, 0]) square([6, 20], center = true);
          translate([12, 0]) circle(r = 9);                                            // 箭头
        }
      }
    }
    for (l = legs) coxa_servo_frame(l) servo_mount_cut();
    // 上表面刻一个朝前的箭头
    translate([12, 0, plate_top_z - 0.6]) linear_extrude(1) polygon([[8, 0], [-4, 5], [-4, -5]]);
  }
}

// ---------------- 甲板（上层）+ 前面的传感器支架 ----------------
deck_inset = 16;
front_x_deck = front_x - deck_inset * cos(front_angle);   // 甲板前缘 X
front_plate_t = 3;
front_plate_w = 52;
front_plate_h = cam_base_h + cam_h + 4;

module deck_2d() {
  difference() {
    inner_hex_2d(deck_inset);
    for (p = standoffs) translate(p) circle(d = m3_hole);
    // 10 mm 间距 M3 孔阵：固定 ESP32 扩展板（用 M3 尼龙柱，孔位不对就用最近的孔 + 扎带）
    for (x = [-30 : 10 : 20], y = [-20 : 10 : 20])
      if (min([for (p = standoffs) norm([x, y] - p)]) > 7) translate([x, y]) circle(d = m3_hole);
    for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 20, sy * 30]) square([12, 5], center = true);  // 走线孔
    lighten_holes_2d() {
      inner_hex_2d(deck_inset);
      union() {
        for (p = standoffs) translate(p) circle(r = 4);
        for (x = [-30 : 10 : 20], y = [-20 : 10 : 20]) translate([x, y]) circle(d = m3_hole);
        for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 20, sy * 30]) square([12, 5], center = true);
        // 前面板和加强筋站在这里，保持实心
        translate([front_x_deck - front_plate_t - 22, -front_plate_w / 2]) square([22 + front_plate_t + 1, front_plate_w]);
      }
    }
  }
}

module front_mount() {
  x1 = front_x_deck;          // 前面板外表面
  x0 = x1 - front_plate_t;
  z0 = deck_z + plate_t;      // 甲板上表面
  difference() {
    union() {
      translate([x0, -front_plate_w / 2, z0 - 0.01]) cube([front_plate_t, front_plate_w, front_plate_h]);
      // 背后的两块三角加强筋
      for (y = [-front_plate_w / 2, front_plate_w / 2 - 3])
        translate([x0 + 0.01, y, z0 - 0.01]) rotate([90, 0, 0]) translate([0, 0, -3])
          linear_extrude(3) difference() {
            polygon([[0, 0], [-22, 0], [0, 40]]);
            if (lighten) offset(r = 1) offset(delta = -5) polygon([[0, 0], [-22, 0], [0, 40]]);
          }
      // 摄像头托架：底板 + 前挡边
      translate([x1 - 0.01, -(cam_w + cam_gap) / 2 - 2, z0 + cam_base_h - 3])
        cube([cam_d + cam_gap + 2.5, cam_w + cam_gap + 4, 3]);
      translate([x1 + cam_d + cam_gap, -(cam_w + cam_gap) / 2 - 2, z0 + cam_base_h - 3])
        cube([2.5, cam_w + cam_gap + 4, 6]);
      // 托架两边的侧挡
      for (s = [-1, 1])
        translate([x1 - 0.01, s > 0 ? (cam_w + cam_gap) / 2 : -(cam_w + cam_gap) / 2 - 2, z0 + cam_base_h - 3])
          cube([cam_d + cam_gap + 2.5, 2, 14]);
    }
    // 摄像头板背后的窗口：排针和杜邦线从这里穿到后面
    translate([x0 - 1, -(cam_w - 8) / 2, z0 + cam_base_h + 5]) cube([front_plate_t + 2, cam_w - 8, cam_h - 12]);
    // 扎带槽：两道扎带把摄像头板绑在面板上
    for (s = [-1, 1], h = [0.3, 0.75])
      translate([x0 - 1, s * ((cam_w + cam_gap) / 2 + 4.5) - 1.5, z0 + cam_base_h + h * cam_h - 2])
        cube([front_plate_t + 2, 3, 4]);
  }
}

// ---------------- 激光雷达平台（装在甲板上方的 4 根铜柱上） ----------------
// 雷达要在整机最高处，360° 都看得见：平台底面比甲板高 lidar_standoff_h，雷达的激光平面在前面板以上。
// 雷达底座的孔位还不知道：默认只开扎带槽（两道扎带横着绑住雷达底座）；量好孔位填 lidar_holes 再导出。
// XIAO ESP32S3 用扎带绑在平台下面（中间两个小槽），雷达线从中间的孔穿下来。
lidar_z = deck_z + plate_t + lidar_standoff_h;    // 平台底面高度

module lidar_mount_2d() {
  difference() {
    offset(r = 6) square([2 * standoffs[0][0], 2 * standoffs[0][1]], center = true);
    for (p = standoffs) translate(p) circle(d = m3_hole);
    circle(d = lidar_cable_d);
    for (h = lidar_holes) translate(h) circle(d = lidar_hole_d);
    for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 16, sy * 26]) square([6, 3], center = true);   // 雷达扎带
    for (sy = [-1, 1]) translate([-14, sy * 10]) square([3, 5], center = true);                       // XIAO 扎带
    lighten_holes_2d() {
      offset(r = 6) square([2 * standoffs[0][0], 2 * standoffs[0][1]], center = true);
      union() {
        for (p = standoffs) translate(p) circle(r = 4);
        circle(d = lidar_cable_d);
        for (h = lidar_holes) translate(h) circle(d = lidar_hole_d);
        for (sx = [-1, 1], sy = [-1, 1]) translate([sx * 16, sy * 26]) square([6, 3], center = true);
        for (sy = [-1, 1]) translate([-14, sy * 10]) square([3, 5], center = true);
        translate([24, 0]) circle(r = 7);                                              // 箭头
      }
    }
  }
}

module lidar_mount() {
  difference() {
    translate([0, 0, lidar_z]) linear_extrude(plate_t) lidar_mount_2d();
    // 朝前的箭头：雷达的“零度方向”标记对准它（见 docs/lidar-mapping.md）
    translate([24, 0, lidar_z + plate_t - 0.6]) linear_extrude(1) polygon([[6, 0], [-3, 4], [-3, -4]]);
  }
}

module deck() {
  translate([0, 0, deck_z]) linear_extrude(plate_t) deck_2d();
  front_mount();
}
