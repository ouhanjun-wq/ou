// 六足机器人 · 上层甲板 (deck)
//
// 用 4 根 M3 × 25 mm 铜柱架在底板 (body_plate.scad) 上面。
// 上面装：PCA9685 舵机驱动板、主控洞洞板（BNO085、分压电阻、功放）、OLED、LD14P 雷达；
// 前沿的竖板用扎带绑 XIAO ESP32S3 Sense，摄像头朝前；后面的方孔装船型电源开关。
// 甲板上是 10 mm 间距的 M3 孔阵，模块用 M2.5 / M3 螺丝 + 尼龙柱或扎带固定，不用对孔位。
//
// 打印：PETG 或 PLA+，层高 0.2 mm，3 圈壁，20% 填充。平放，竖板朝上，不需要支撑。
// 坐标：X+ = 机器人前方，Y+ = 左边，原点 = 机身中心。

include <params.scad>

/* [甲板] */
deck_l = 124;                 // 长 (mm)
deck_w = 80;                  // 宽 (mm)
deck_t = 3.0;                 // 厚 (mm)
corner_r = 8;                 // 圆角 (mm)
standoffs = [[34, 22], [34, -22], [-34, 22], [-34, -22]];  // 和底板一致
grid_pitch = 10;              // 孔阵间距 (mm)
switch_cut = [19.2, 13.2];    // KCD1 船型开关开孔 [Y 向, X 向] (mm)
switch_at = [-50, 0];         // 开关位置
cam_wall = [30, 18, 2.5];     // 前沿竖板 [宽, 高, 厚] (mm)
tie_slot = [3.5, 2.0];        // 扎带孔 [宽, 高] (mm)

/* [Hidden] */
$fn = 40;

function near(p, q, d) = norm([p[0] - q[0], p[1] - q[1]]) < d;
function blocked(p) =
  len([for (s = standoffs) if (near(p, s, 7)) 1]) > 0 ||
  (abs(p[0] - switch_at[0]) < switch_cut[1] / 2 + 4 && abs(p[1] - switch_at[1]) < switch_cut[0] / 2 + 4) ||
  (p[0] > deck_l / 2 - cam_wall[2] - 5 && abs(p[1]) < cam_wall[0] / 2 + 3);

module deck_2d() {
  offset(r = corner_r) square([deck_l - 2 * corner_r, deck_w - 2 * corner_r], center = true);
}

module holes_2d() {
  for (s = standoffs) translate(s) circle(d = m3_hole);
  translate(switch_at) square([switch_cut[1], switch_cut[0]], center = true);
  nx = floor((deck_l / 2 - 8) / grid_pitch);
  ny = floor((deck_w / 2 - 8) / grid_pitch);
  for (i = [-nx : nx], j = [-ny : ny]) {
    p = [i * grid_pitch, j * grid_pitch];
    if (!blocked(p)) translate(p) circle(d = m3_hole);
  }
}

module cam_wall() {
  difference() {
    translate([deck_l / 2 - cam_wall[2], -cam_wall[0] / 2, deck_t - 0.01])
      cube([cam_wall[2], cam_wall[0], cam_wall[1]]);
    // 两对扎带孔，XIAO 板夹在中间（板宽 17.8 mm）
    for (y = [-12, 12], z = [deck_t + 5, deck_t + 13])
      translate([deck_l / 2 - cam_wall[2] - 1, y - tie_slot[0] / 2, z - tie_slot[1] / 2])
        cube([cam_wall[2] + 2, tie_slot[0], tie_slot[1]]);
  }
  // 竖板后面的两块三角加强筋
  for (y = [-cam_wall[0] / 2, cam_wall[0] / 2 - 2])
    translate([deck_l / 2 - cam_wall[2] + 0.01, y, deck_t - 0.01])
      rotate([90, 0, 0]) mirror([0, 0, 1]) linear_extrude(2)
        polygon([[0, 0], [-8, 0], [0, cam_wall[1] - 4]]);
}

union() {
  linear_extrude(deck_t) difference() { deck_2d(); holes_2d(); }
  cam_wall();
}
