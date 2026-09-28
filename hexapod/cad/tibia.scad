// 六足机器人 · 小腿 (tibia)
//
// 上端压在膝舵机的舵机臂上，下端是脚尖。膝轴 → 脚尖的距离 = L2。
// 6 条腿用同一个零件：右边三条腿把它翻过来装（舵机臂凹槽始终朝着舵机）。
// 脚尖可以套一小段 6 mm 热缩管或硅胶脚套防滑。
//
// 打印：PETG 或 PLA+，层高 0.2 mm，4 圈壁，30% 填充。
//       平放，有凹槽的一面朝上，不需要支撑。打 6 个（多打 1–2 个备用）。
// 坐标：原点 = 膝轴，X+ = 沿小腿指向脚尖，Z = 厚度方向。

include <params.scad>

/* [小腿] */
tib_t = 4.0;              // 厚度 (mm)
hub_r = 9;                // 膝端圆盘半径 (mm)
bar_w_top = 11;           // 靠膝盖处宽度 (mm)
foot_r = 3.5;             // 脚尖圆角半径 (mm)

/* [Hidden] */
$fn = 48;

module profile_2d() {
  hull() {
    circle(r = hub_r);
    translate([hub_r + 4, -bar_w_top / 2]) square([0.01, bar_w_top]);
    translate([L2 - foot_r, 0]) circle(r = foot_r);
  }
}

difference() {
  linear_extrude(tib_t) profile_2d();
  // 膝舵机的舵机臂凹槽：在顶面 (z = tib_t) 往下挖，臂沿 +X
  translate([0, 0, tib_t]) mirror([0, 0, 1]) horn_cut(tib_t);
}
