// 18 舵机六足 · 3D 打印机身 · 所有零件的模块（装配坐标）
// 各个 *.scad 包装文件调用这里的模块，并把零件转成适合打印 / 下单的姿态。
include <params.scad>

$fn = 48;

// ---------------- 条形孔（照套件铝板的样子：一排排长条孔） ----------------
// 板件只有 3 mm 厚，做不了内部空心，所以在不受力的地方打通孔减重（嘉立创按体积计价，少用料就省钱）。
// 孔做成套件铝板那种圆头长条孔，宽度 = M3 通孔：板上任意位置都能拧 M3 螺丝、穿扎带。
// 只打完整的条孔：离外框 slot_rim、离其他孔 / 槽 slot_keep 以内的整条不打，不会出现切掉一半的碎孔。
/* [减重条形孔] */
lighten = true;           // false = 全部实心（更结实，也更贵）
slot_w = 3.4;             // 条孔宽 = M3 通孔 (mm)
slot_l = 12;              // 条孔长 (mm)
slot_px = 16;             // 同一排条孔的中心距 (mm)；相邻两排错开半个
slot_py = 7;              // 排距 (mm)
slot_rim = 3.5;           // 外框宽度 (mm)
slot_keep = 2.5;          // 离其他孔位、槽口至少留多少材料 (mm)

// 点 p 到条孔 c 中心线段的距离（条孔沿 X）
function slot_dist(c, p) = let(a = (slot_l - slot_w) / 2, dx = max(abs(p[0] - c[0]) - a, 0), dy = p[1] - c[1])
  sqrt(dx * dx + dy * dy);
// area = [x0, y0, x1, y1]；circles = [[x, y, r], ...] 和 boxes = [[x0, y0, x1, y1], ...] 是要保留实心的地方
function slot_ok(c, area, circles, boxes) =
  c[0] - slot_l / 2 >= area[0] + slot_rim && c[0] + slot_l / 2 <= area[2] - slot_rim &&
  c[1] - slot_w / 2 >= area[1] + slot_rim && c[1] + slot_w / 2 <= area[3] - slot_rim &&
  len([for (k = circles) if (slot_dist(c, k) < k[2] + slot_w / 2 + slot_keep) 1]) == 0 &&
  len([for (b = boxes) if (c[0] + slot_l / 2 + slot_keep > b[0] && c[0] - slot_l / 2 - slot_keep < b[2] &&
                           c[1] + slot_w / 2 + slot_keep > b[1] && c[1] - slot_w / 2 - slot_keep < b[3]) 1]) == 0;
function slot_centers(area, circles = [], boxes = []) = [
  for (j = [floor(area[1] / slot_py) : ceil(area[3] / slot_py)],
       i = [floor(area[0] / slot_px) - 1 : ceil(area[2] / slot_px) + 1])
    let(c = [(i + (j % 2 == 0 ? 0 : 0.5)) * slot_px, j * slot_py])
    if (slot_ok(c, area, circles, boxes)) c];
module slots_2d(centers) {
  if (lighten) for (c = centers) translate(c)
    hull() for (s = [-1, 1]) translate([s * (slot_l - slot_w) / 2, 0]) circle(d = slot_w, $fn = 24);
}
function box_of(center, size) = [center[0] - size[0] / 2, center[1] - size[1] / 2, center[0] + size[0] / 2, center[1] + size[1] / 2];

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
// 照套件的 U 形支架：顶板压在基节舵机的舵机臂上，外侧一块竖直的“U 形背板”往下绕过舵机，
// 底下的下摆臂套在舵机底部托架（hip_cradle）的转轴上。这样基节舵机上下两头都有支点，
// 腿的重量不只压在舵机输出轴上。侧面的竖板夹住大腿舵机（输出轴水平）。
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

// U 形背板 + 下摆臂 + 舵机底部托架（腿坐标，高度原点 = 大腿舵机轴）
case_bottom_z = coxa_horn_z - shaft_gap - case_h;  // 基节舵机机壳底面
cradle_floor = 4.5;       // 托架底板厚度（里面埋一颗 M3 螺母）(mm)
arm_t = 3;                // 下摆臂厚度 (mm)
arm_gap = 4;              // 下摆臂和托架之间的间隙 (mm)：装 U 形支架时要先抬高 ~3.5 mm 才能压进舵机花键
arm_z1 = case_bottom_z - cradle_floor - arm_gap;   // 下摆臂上表面
arm_z0 = arm_z1 - arm_t;                           // 下摆臂下表面
web_x0 = wall_x0;         // U 形背板内表面离基节转轴 (mm)：基节转 ±60° 时刚好绕开底板和舵机安装耳
web_t = 2.5;
web_y1 = 7;               // 背板往 +Y 伸到哪
pivot_d = 7;              // 转轴套外径 (mm)；下摆臂的孔大 0.4
web_window = [-3, -7, 9]; // 背板上的圆窗 [y, z, 直径]（套件支架上的圆孔，减重）

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
      // U 形背板：从顶板一直往下到下摆臂
      translate([web_x0, wall_y0, arm_z0]) cube([web_t, web_y1 - wall_y0, coxa_horn_z + top_t - arm_z0]);
      // 下摆臂：从背板底下伸回基节转轴
      translate([0, 0, arm_z0]) linear_extrude(arm_t) hull() {
        circle(d = pivot_d + 6);
        translate([web_x0, wall_y0]) square([web_t, web_y1 - wall_y0]);
      }
    }
    translate([0, 0, coxa_horn_z]) rotate(coxa_horn_angle) horn_cut(top_t);
    femur_servo_frame() servo_mount_cut();
    translate([0, 0, arm_z0 - 1]) cylinder(d = pivot_d + 0.4, h = arm_t + 2);
    translate([web_x0 - 1, web_window[0], web_window[1]]) rotate([0, 90, 0]) cylinder(d = web_window[2], h = web_t + 2);
  }
}

// ---------------- 基节舵机底部托架 hip_cradle ----------------
// 托住基节舵机的底部，两头的耳朵顶在底板下面，和舵机安装耳用同一颗 M2 螺丝拧紧（从上往下：安装耳 → 底板 → 托架）。
// 下摆臂的转轴是一个带法兰的转轴套（pivot_bushing）：U 形支架装好后从下面穿过下摆臂的孔顶到托架底面，
// M3 螺丝穿过转轴套拧进埋在托架里的 M3 螺母。转轴套比下摆臂长，螺丝拧紧只压转轴套，下摆臂能自由转。
// 在舵机本地坐标里建模（和 servo_body() 一样：输出轴 = +Z，舵机臂贴合面 z = 0）。
cr_c = 0.2;                                        // 托架比舵机机壳大多少（每边）
cr_wall = 2;
cr_x0 = shaft_off - servo_l / 2 - cr_c;             // 机壳两头（含间隙）
cr_x1 = shaft_off + servo_l / 2 + cr_c;
cr_y = servo_w / 2 + cr_c;
cr_bottom = -shaft_gap - case_h;                    // 机壳底面
cr_top = ear_low - plate_t;                         // 底板下表面
cr_tab_h = 5;                                       // 顶在底板下面的耳朵厚度（M2 螺丝拧进去的深度）
m3_nut_af = 5.5;                                    // M3 螺母对边 (mm)
m3_nut_t = 2.4;
cr_floor_z0 = cr_bottom - cradle_floor;             // 托架底面
bush_flange = [10, 1.2];                            // 转轴套法兰 [直径, 厚度]：挡住下摆臂
bush_z0 = cr_floor_z0 - arm_gap - arm_t - 0.3;      // 转轴套管子的下端（法兰上表面）

module hip_cradle_local() {
  difference() {
    union() {
      translate([cr_x0 - cr_wall, -cr_y, cr_floor_z0]) cube([cr_x1 - cr_x0 + 2 * cr_wall, 2 * cr_y, cradle_floor]);
      for (x = [cr_x0 - cr_wall, cr_x1])
        translate([x, -cr_y, cr_bottom - 0.01]) cube([cr_wall, 2 * cr_y, cr_top - cr_bottom + 0.01]);
      for (s = [-1, 1]) {
        hx = shaft_off + s * ear_hole_spacing / 2;
        translate([s < 0 ? hx - 2.5 : cr_x1 - 0.01, -4, cr_top - cr_tab_h])
          cube([s < 0 ? cr_x0 - hx + 2.5 + 0.01 : hx + 2.5 - cr_x1 + 0.01, 8, cr_tab_h]);
      }
    }
    for (s = [-1, 1]) translate([shaft_off + s * ear_hole_spacing / 2, 0, cr_top - cr_tab_h - 1])
      cylinder(d = screw_pilot, h = cr_tab_h + 2, $fn = 16);
    // 舵机线从机壳尾端底部出来：尾端的墙开个口
    translate([cr_x1 - 1, -3.5, cr_bottom - 0.02]) cube([cr_wall + 2, 7, 8]);
    // M3 螺丝孔 + 从上面放进去的 M3 螺母（装舵机之前放，舵机压住它）
    translate([0, 0, cr_floor_z0 - 1]) cylinder(d = m3_hole, h = 20, $fn = 24);
    translate([0, 0, cr_bottom - 3]) rotate(30) cylinder(d = (m3_nut_af + 0.3) / cos(30), h = 3.01, $fn = 6);
  }
}

// 转轴套 pivot_bushing（也在舵机本地坐标里）：管子从托架底面往下穿过下摆臂，下端一圈法兰挡住下摆臂。
module pivot_bushing_local() {
  difference() {
    union() {
      translate([0, 0, bush_z0]) cylinder(d = pivot_d, h = cr_floor_z0 - bush_z0);
      translate([0, 0, bush_z0 - bush_flange[1]]) cylinder(d = bush_flange[0], h = bush_flange[1] + 0.01);
    }
    translate([0, 0, bush_z0 - bush_flange[1] - 1]) cylinder(d = m3_hole, h = 20, $fn = 24);
  }
}

// ---------------- 大腿 femur ----------------
// 一块平板：一头压大腿舵机的舵机臂（-Y 面），另一头压小腿舵机的舵机臂（+Y 面）。
// 照套件的样子，上边往上拱起，中间开一个三角窗（两头的舵机臂凹槽都在拱的下面，不受影响）。
femur_hub_r = 8.5;
femur_apex = [25, 13, 3];                           // 拱顶 [x, z, 圆角半径]
femur_window = [[13, 5.5], [37, 5.5], [25, 10.5]];  // 三角窗
module femur_2d() {
  difference() {
    hull() {
      circle(r = femur_hub_r);
      translate([femur_len, 0]) circle(r = femur_hub_r);
      translate([femur_apex[0], femur_apex[1]]) circle(r = femur_apex[2]);
    }
    offset(r = 1.5) offset(delta = -1.5) polygon(femur_window);
  }
}

module femur_bar() {
  difference() {
    translate([0, bar_t / 2, 0]) rotate([90, 0, 0]) linear_extrude(bar_t) femur_2d();
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

// 下半截是一片“桁架”刀片：外侧（+Z）往外鼓，中间两个三角窗，脚尖还是在 x = tibia_len、z = 0。
blade_x0 = 44;
blade_z = [-6, 16];                                 // 刀片起点的上下边
blade_bulge = [58, 13, 3.5];                          // 外侧鼓包 [x, z, 圆角半径]
tibia_windows = [
  [[47, -3], [47, 12.5], [55, 4.5]],                // ▷
  [[58, 4.5], [64, -0.5], [64, 8.5]],               // ◁
];
// 在零件的 (x, z) 平面里画：2D 的 y 就是零件的 z
module tibia_blade_2d() {
  difference() {
    hull() {
      translate([blade_x0, blade_z[0]]) square([1, blade_z[1] - blade_z[0]]);
      translate([blade_bulge[0], blade_bulge[1]]) circle(r = blade_bulge[2]);
      translate([tibia_len - foot_r, 0]) circle(r = foot_r);
    }
    for (w = tibia_windows) offset(r = 1.2) offset(delta = -1.2) polygon(w);
  }
}

module tibia_servo_frame() translate([0, bar_t / 2, 0]) rotate([90, 0, 0]) children();

module tibia_part() {
  difference() {
    union() {
      translate([tib_x0, tib_plate_y0, -tib_half_w]) cube([tib_x1 - tib_x0, mount_t, 2 * tib_half_w]);
      hull() {
        translate([tib_x1 - 1, tib_plate_y0, -tib_half_w]) cube([1, mount_t, 2 * tib_half_w]);
        translate([blade_x0, -foot_t / 2, blade_z[0]]) cube([1, foot_t, blade_z[1] - blade_z[0]]);
      }
      translate([0, foot_t / 2, 0]) rotate([90, 0, 0]) linear_extrude(foot_t) tibia_blade_2d();
    }
    tibia_servo_frame() servo_mount_cut();
  }
}

// ---------------- 机身底板 ----------------
// 6 个基节舵机从上往下插进方孔，安装耳压在底板上表面；输出轴朝上，基节支架装在上面。
// 外形照套件：中间一块长方形（布满条形孔），6 个髋轴处各伸出一块圆头的舵机座。
standoffs = [[30, 28], [30, -28], [-30, 28], [-30, -28]];
body_core = [100, 82];    // 中间长方形 (mm)：边离每个髋轴 >= 14 mm，基节转 ±60° 时 U 形背板扫不到
pad_r = 13;               // 每个髋轴周围的圆盘半径 (mm)

module coxa_servo_frame(l) translate([l[0], l[1], coxa_horn_z]) rotate(l[2]) rotate(180) children();

module body_plate_2d() {
  union() {
    square(body_core, center = true);
    for (l = legs) at_leg(l) {
      circle(r = pad_r);
      translate([-(shaft_off + ear_span / 2 + 3), -(pocket_w / 2 + 3)])
        square([shaft_off + ear_span / 2 + 3, pocket_w + 6]);
    }
  }
}

// 嘉立创免费打印：单个模型不能超过 100 × 100 × 100 mm，所以底板拆成 4 块（前左、前右、后左、后右），
// 下面用 3 根拼接条和 M3 沉头螺丝 + 螺母拼起来。拼缝：x = split_x 和 y = 0。
// 螺丝孔在底板上表面有沉头孔，螺丝头和板面齐平（电池压在上面不会晃）。
split_x = 20;             // 前块 / 后块的分界 (mm)
seam_gap = 0.3;           // 两块之间留的缝 (mm)
splice_t = 3;             // 拼接条厚度 (mm)
splice_long_holes = [for (x = [-34, -8, 34], s = [-1, 1]) [x, s * 4]];        // 长拼接条（沿 y = 0）
splice_short_holes = [for (x = [split_x - 4, split_x + 4], y = [13, 21]) [x, y]]; // 短拼接条（沿 x = split_x，左边；右边镜像）
body_wire_slots = [for (sy = [-1, 1]) [-20, sy * 34, 12, 6]];                  // [x, y, 长, 宽]
body_wire_slots2 = [for (sx = [-1, 1], sy = [-1, 1]) [sx * 44, sy * 11, 6, 8]];

function splice_holes() = concat(splice_long_holes, [for (p = splice_short_holes, s = [-1, 1]) [p[0], s * p[1]]]);

// 条形孔只打在中间长方形里，避开：舵机方孔和安装耳（沿每个舵机一串圆）、铜柱、拼接螺丝的沉头、
// 扎带槽、走线孔、两条拼缝两边各 6–7 mm。
function body_slots() = slot_centers(
  [-body_core[0] / 2, -body_core[1] / 2, body_core[0] / 2, body_core[1] / 2],
  concat([for (p = standoffs) [p[0], p[1], m3_hole / 2 + 1]],
         [for (h = splice_holes()) [h[0], h[1], m3_hole / 2 + countersink_h]],
         [for (l = legs, t = [-6 : 5 : 21]) [l[0] - t * cos(l[2]), l[1] - t * sin(l[2]), 7]]),
  concat([for (s = [-1, 1]) box_of([0, s * 24], [22, 3])],
         [for (w = concat(body_wire_slots, body_wire_slots2)) box_of([w[0], w[1]], [w[2], w[3]])],
         [[-200, -6, 200, 6], [split_x - 7, -200, split_x + 7, 200]]));

module body_holes_2d(splice = true) {
  for (p = standoffs) translate(p) circle(d = m3_hole);
  for (s = [-1, 1]) translate([0, s * 24]) square([22, 3], center = true);        // 电池魔术贴扎带槽（20 mm 宽）
  for (w = concat(body_wire_slots, body_wire_slots2)) translate([w[0], w[1]]) square([w[2], w[3]], center = true);  // 走线孔
  if (splice) for (h = splice_holes()) translate(h) circle(d = m3_hole);
}

// 沉头孔（M3 沉头螺丝）：直孔 + 顶部 90° 锥，用一个旋转体做成一整块，网格没有接缝（不出碎三角形）。
module countersunk_hole(depth, cs_h) {
  rotate_extrude($fn = 48) polygon([[0, -1], [m3_hole / 2, -1], [m3_hole / 2, depth - cs_h],
                                    [m3_hole / 2 + cs_h + 1, depth + 1], [0, depth + 1]]);
}

module body_plate() {
  difference() {
    translate([0, 0, plate_top_z - plate_t]) linear_extrude(plate_t) difference() {
      body_plate_2d();
      body_holes_2d(splice = false);
      slots_2d(body_slots());
    }
    for (l = legs) coxa_servo_frame(l) servo_mount_cut();
    // 拼接螺丝的沉头孔（M3 沉头螺丝，头部直径约 6 mm）
    for (h = splice_holes()) translate([h[0], h[1], plate_top_z - plate_t]) countersunk_hole(plate_t, countersink_h);
  }
}
countersink_h = 1.6;

// 底板的一块：front = 前块（x > split_x），left = 左块（y > 0）
module body_plate_piece(front, left) {
  g = seam_gap / 2;
  intersection() {
    body_plate();
    translate([front ? split_x + g : -200, left ? g : -200, -100])
      cube([front ? 200 : 200 + split_x - g, left ? 200 : 200 - g, 200]);
  }
}

// 拼接条：装在底板下面，M3 螺丝从上往下穿，下面上螺母
splice_z = plate_top_z - plate_t - splice_t;
module splice_long() {
  translate([0, 0, splice_z]) linear_extrude(splice_t) difference() {
    offset(r = 3) square([80 - 6, 16 - 6], center = true);
    for (h = splice_long_holes) translate(h) circle(d = m3_hole);
  }
}
module splice_short(side = 1) {
  mirror([0, side > 0 ? 0 : 1, 0]) translate([0, 0, splice_z]) linear_extrude(splice_t) difference() {
    translate([split_x - 8, 9]) offset(r = 3) translate([3, 3]) square([16 - 6, 16 - 6]);
    for (h = splice_short_holes) translate(h) circle(d = m3_hole);
  }
}

// ---------------- 甲板（上层）+ 前面的传感器支架 ----------------
// 照套件的顶板：一块长方形板，布满 M3 宽的条形孔，扩展板用 M3 尼龙柱装在任意条孔上。
deck_rect = [-48.5, -35, 36, 35];                      // [x0, y0, x1, y1]；前缘 = 前面板外表面
// 免费打印限 100 mm：前面板往里收到 x = front_plate_x，托架前端不超出甲板太多（整块 < 100 mm）。
// 面板只到摄像头板高度的 80%（上面两道扎带够用），这样顶端低于雷达平台。
front_plate_x = 36;       // 前面板外表面 X
front_plate_t = 4;
front_plate_w = 48;
front_plate_h = cam_base_h + 0.8 * cam_h;
tray_lip = 2;             // 托架前挡边厚度

deck_wire_holes = [for (sx = [-1, 1], sy = [-1, 1]) [sx * 20, sy * 30, 12, 5]];   // 走线孔 [x, y, 长, 宽]
function deck_slots() = slot_centers(deck_rect,
  concat([for (p = standoffs) [p[0], p[1], 3]],
         [for (x = [deck_rect[0], deck_rect[2]], y = [deck_rect[1], deck_rect[3]]) [x, y, 6]]),   // 四个圆角
  concat([for (w = deck_wire_holes) box_of([w[0], w[1]], [w[2], w[3]])],
         [[front_plate_x - front_plate_t - 1, -front_plate_w / 2, 100, front_plate_w / 2]]));   // 前面板底座

module deck_2d() {
  difference() {
    translate([deck_rect[0], deck_rect[1]]) offset(r = 4) offset(delta = -4)
      square([deck_rect[2] - deck_rect[0], deck_rect[3] - deck_rect[1]]);
    for (p = standoffs) translate(p) circle(d = m3_hole);
    for (w = deck_wire_holes) translate([w[0], w[1]]) square([w[2], w[3]], center = true);
    slots_2d(deck_slots());
  }
}

module front_mount() {
  x1 = front_plate_x;         // 前面板外表面
  x0 = x1 - front_plate_t;
  z0 = deck_z + plate_t;      // 甲板上表面
  difference() {
    union() {
      translate([x0, -front_plate_w / 2, z0 - 0.01]) cube([front_plate_t, front_plate_w, front_plate_h]);
      // 摄像头托架：底板 + 前挡边 + 两边侧挡（托架同时从前面撑住面板，不用三角加强筋）
      translate([x1 - 0.01, -(cam_w + cam_gap) / 2 - 2, z0 + cam_base_h - 3])
        cube([cam_d + cam_gap + tray_lip, cam_w + cam_gap + 4, 3]);
      translate([x1 + cam_d + cam_gap, -(cam_w + cam_gap) / 2 - 2, z0 + cam_base_h - 3])
        cube([tray_lip, cam_w + cam_gap + 4, 6]);
      for (s = [-1, 1])
        translate([x1 - 0.01, s > 0 ? (cam_w + cam_gap) / 2 : -(cam_w + cam_gap) / 2 - 2, z0 + cam_base_h - 3])
          cube([cam_d + cam_gap + tray_lip, 2, 14]);
    }
    // 摄像头板背后的窗口：排针和杜邦线从这里穿到后面
    translate([x0 - 1, -(cam_w - 8) / 2, z0 + cam_base_h + 5]) cube([front_plate_t + 2, cam_w - 8, front_plate_h - cam_base_h - 10]);
    // 扎带槽：两道扎带把摄像头板绑在面板上
    for (s = [-1, 1], h = [0.3, 0.7])
      translate([x0 - 1, s * ((cam_w + cam_gap) / 2 + 2.5) - 1.5, z0 + cam_base_h + h * cam_h - 2])
        cube([front_plate_t + 2, 3, 4]);
  }
}

// ---------------- 激光雷达平台（装在甲板上方的 4 根铜柱上） ----------------
// 雷达要在整机最高处，360° 都看得见：平台底面比甲板高 lidar_standoff_h，雷达的激光平面在前面板以上。
// 雷达底座的孔位还不知道：默认只开扎带槽（两道扎带横着绑住雷达底座）；量好孔位填 lidar_holes 再导出。
// XIAO ESP32S3 用扎带绑在平台下面（中间两个小槽），雷达线从中间的孔穿下来。
lidar_z = deck_z + plate_t + lidar_standoff_h;    // 平台底面高度

lidar_rect = [-standoffs[0][0] - 6, -standoffs[0][1] - 6, standoffs[0][0] + 6, standoffs[0][1] + 6];
lidar_zip = [for (sx = [-1, 1], sy = [-1, 1]) [sx * 16, sy * 26, 6, 3]];   // 雷达扎带槽
xiao_zip = [for (sy = [-1, 1]) [-14, sy * 10, 3, 5]];                      // XIAO 扎带槽
function lidar_slots() = slot_centers(lidar_rect,
  concat([for (p = standoffs) [p[0], p[1], 3]], [[0, 0, lidar_cable_d / 2], [24, 0, 7]],   // 铜柱、走线孔、箭头
         [for (h = lidar_holes) [h[0], h[1], lidar_hole_d / 2]]),
  [for (w = concat(lidar_zip, xiao_zip)) box_of([w[0], w[1]], [w[2], w[3]])]);

module lidar_mount_2d() {
  difference() {
    offset(r = 6) square([2 * standoffs[0][0], 2 * standoffs[0][1]], center = true);
    for (p = standoffs) translate(p) circle(d = m3_hole);
    circle(d = lidar_cable_d);
    for (h = lidar_holes) translate(h) circle(d = lidar_hole_d);
    for (w = concat(lidar_zip, xiao_zip)) translate([w[0], w[1]]) square([w[2], w[3]], center = true);
    slots_2d(lidar_slots());
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
