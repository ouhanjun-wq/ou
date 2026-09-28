// 六足机器人 · 大腿 (femur)
//
// 一头（原点）压在髋舵机的舵机臂上，另一头是一块竖着的侧板，膝舵机横着插在侧板的方孔里，
// 输出轴朝外，小腿 (tibia.scad) 装在膝舵机的舵机臂上。髋轴 → 膝轴的距离 = L1。
//
//   side = "a"：左边三条腿（FL / ML / RL）用，3 个
//   side = "b"：右边三条腿（FR / MR / RR）用，3 个（a 的镜像）
//
// 打印：PETG 或 PLA+，层高 0.2 mm，4 圈壁，30% 填充。
//       顶面（有舵机臂凹槽的一面）朝下贴热床，侧板朝上，不需要支撑。
// 坐标：原点 = 髋轴，X+ = 沿大腿朝外，Z+ = 朝上（装到机器人上时的方向）。

include <params.scad>

side = "a";               // "a" 或 "b"

/* [大腿] */
arm_t = 3.0;              // 上面横板厚度 (mm)
arm_w = 14;               // 横板宽度 (mm)
hub_r = 10;               // 髋端圆盘半径 (mm)
wall_t = 3.0;             // 侧板厚度 (mm)
wall_margin = 3.0;        // 侧板在舵机方孔下面留的边 (mm)
servo_gap = 0.4;          // 膝舵机和横板之间留的缝 (mm)

/* [Hidden] */
$fn = 48;
w0 = arm_w / 2;                                       // 侧板外表面的 Y
z_k = -arm_t - servo_gap - pocket_w / 2;              // 膝轴的 Z
wall_x0 = L1 - shaft_off - ear_span / 2 - 2;          // 侧板起点
wall_x1 = L1 - shaft_off + ear_span / 2 + 2;          // 侧板终点
wall_z0 = z_k - pocket_w / 2 - wall_margin;           // 侧板底边

module arm() {
  hull() {
    translate([0, 0, -arm_t]) cylinder(r = hub_r, h = arm_t);
    translate([wall_x1 - 1, -arm_w / 2, -arm_t]) cube([1, arm_w, arm_t]);
  }
}

module wall() {
  translate([wall_x0, w0 - wall_t, wall_z0]) cube([wall_x1 - wall_x0, wall_t, -wall_z0]);
}

// 侧板和横板之间的三角加强筋（在侧板内侧、方孔两头以外）
module ribs() {
  for (x = [wall_x0, wall_x1 - 2])
    translate([x, w0 - wall_t + 0.01, -arm_t + 0.01])
      rotate([90, 0, 90]) linear_extrude(2)
        polygon([[0, 0], [-6, 0], [0, -6]]);
}

module femur_a() {
  difference() {
    union() { arm(); wall(); ribs(); }
    // 髋舵机的舵机臂凹槽：在顶面 (z = 0) 往下挖
    translate([0, 0, 0]) mirror([0, 0, 1]) horn_cut(arm_t);
    // 膝舵机方孔（沿 Y 贯穿侧板），输出轴在 x = L1
    translate([L1 - servo_end_in, w0 - wall_t - 1, z_k - pocket_w / 2])
      cube([pocket_l, wall_t + 2, pocket_w]);
    // 膝舵机安装耳螺丝孔
    for (s = [-1, 1])
      translate([L1 - shaft_off + s * ear_hole_spacing / 2, w0 + 1, z_k])
        rotate([90, 0, 0]) cylinder(d = screw_pilot, h = wall_t + 2);
  }
}

if (side == "b") mirror([0, 1, 0]) femur_a(); else femur_a();

echo(str("膝轴位置 x = ", L1, " mm, z = ", z_k, " mm；膝舵机输出轴端面约在 y = ",
         w0 + case_h - ear_z, " mm"));
