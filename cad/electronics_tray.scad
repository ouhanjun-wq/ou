// 机械臂 · 电控托板 (electronics tray)
//
// 所有电控部件装在这一块板上，板子再用 4 颗木螺丝固定在底板上（机械臂正后方，见图 6）。
// - 有固定孔的：Arduino Uno（官方孔位）、舵机分线板（洞洞板）：打印立柱 + M2.5 / M3 自攻螺丝
// - 固定孔不统一的：两个降压模块、WAGO 端子：扎带槽 + 双面胶
//
// 打印：PETG，层高 0.2 mm，3 圈壁，20% 填充，平放，不需要支撑。
// ★ 不同店铺的模块孔距不一样：先量你的模块，改下面 [模块] 里的数字。

/* [托板] */
tray_w = 200;          // 宽 (mm)
tray_d = 130;          // 深 (mm)
tray_t = 3;            // 厚 (mm)
corner_hole = 4.5;     // 四角固定孔（M4 / 木螺丝）
rib_h = 5;             // 四周加强边高度

/* [立柱] */
post_h = 6;            // 立柱高度（模块板底离托板面）
post_d = 6;            // 立柱外径
post_hole = 2.2;       // M2.5 自攻螺丝底孔

/* [模块]  位置 = 模块左下角 (x, y) */
// Arduino Uno R3（上面叠 USB Host Shield），68.6 x 53.3，官方 4 个安装孔（相对左下角）
uno = [12, 12, 68.6, 53.3];
uno_holes = [[14.0, 2.5], [15.3, 50.7], [66.1, 7.6], [66.1, 35.5]];
// 舵机分线板：50 x 70 洞洞板，四角孔距 45 x 65（横放）
perf = [100, 10, 70, 50, 65, 45];          // [x, y, 长, 宽, 孔距x, 孔距y]
// 扎带固定区： [x, y, 长, 宽]
buck = [10, 78, 66, 40];                  // 20 A 降压模块（带散热片）
mp1584 = [95, 75, 25, 20];                // MP1584 小降压
wago = [130, 72, 60, 22];                 // WAGO 分线端子

/* [扎带] */
tie_w = 3.2;           // 扎带宽 3 mm
tie_l = 2.0;

/* [Hidden] */
$fn = 32;

module post(x, y) {
  translate([x, y, tray_t - 0.01])
    difference() {
      cylinder(d = post_d, h = post_h);
      translate([0, 0, 1]) cylinder(d = post_hole, h = post_h);
    }
}

module post_pair(m) {
  // 4 根立柱，按模块孔距居中
  cx = m[0] + m[2] / 2;
  cy = m[1] + m[3] / 2;
  for (sx = [-1, 1], sy = [-1, 1])
    translate([cx + sx * m[4] / 2, cy + sy * m[5] / 2, tray_t - 0.01])
      difference() {
        cylinder(d = post_d, h = post_h);
        translate([0, 0, 1]) cylinder(d = post_hole, h = post_h);
      }
}

module tie_slots(m) {
  // 模块两端各一对扎带孔（扎带从下面穿上来，绕过模块）
  for (x = [m[0] + 4, m[0] + m[2] - 4])
    for (y = [m[1] - 3, m[1] + m[3] + 3 - tie_l])
      translate([x - tie_w / 2, y, -1]) cube([tie_w, tie_l, tray_t + 2]);
}

module outline(m) {
  // 0.6 mm 深的浅槽，标出模块摆放位置
  translate([m[0], m[1], tray_t - 0.6]) difference() {
    cube([m[2], m[3], 1]);
    translate([0.8, 0.8, -1]) cube([m[2] - 1.6, m[3] - 1.6, 3]);
  }
}

difference() {
  union() {
    cube([tray_w, tray_d, tray_t]);
    // 四周加强边
    difference() {
      cube([tray_w, tray_d, tray_t + rib_h]);
      translate([2, 2, -1]) cube([tray_w - 4, tray_d - 4, tray_t + rib_h + 2]);
      // 后边留出线缆出口
      translate([tray_w / 2 - 30, -1, tray_t]) cube([60, 4, rib_h + 1]);
      translate([-1, tray_d / 2 - 15, tray_t]) cube([4, 30, rib_h + 1]);
    }
    post_pair(perf);
    for (h = uno_holes) post(uno[0] + h[0], uno[1] + h[1]);
  }
  for (m = [buck, mp1584, wago]) { tie_slots(m); outline(m); }
  outline(perf);
  outline(uno);
  // 四角固定孔（带沉头）
  for (x = [7, tray_w - 7], y = [7, tray_d - 7])
    translate([x, y, -1]) {
      cylinder(d = corner_hole, h = tray_t + 2);
      translate([0, 0, tray_t - 1]) cylinder(d1 = corner_hole, d2 = corner_hole + 4, h = 2.01);
    }
  // 减重 + 通风孔（降压模块下面）
  for (i = [0:4]) translate([buck[0] + 10 + i * 11, buck[1] + 10, -1]) cube([6, buck[3] - 20, tray_t + 2]);
}

echo(str("托板 ", tray_w, " x ", tray_d, " mm"));
