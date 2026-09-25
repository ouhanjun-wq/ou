// 机械臂 · 舵机线夹 (cable clip)
//
// 卡在铝合金支架的板边上，把舵机线（最多 3 根并排）固定在支架上，关节处线不会被夹住。
// 打印：PETG（有弹性，PLA 容易断），层高 0.2 mm，100% 填充，按导出方向平放，不需要支撑。
// ★ 量一下支架钢板 / 铝板的厚度，改 sheet_t。一次打 10 个左右。

/* [支架] */
sheet_t = 2.0;         // 支架板厚 (mm)
grip_depth = 7;        // 夹进板边的深度 (mm)
grip_fit = 0.1;        // 夹口比板厚小多少（越大越紧）

/* [线束] */
cables = 3;            // 并排几根舵机线
cable_w = 3.6;         // 一根舵机线的宽度（3 芯扁线）
cable_h = 1.3;         // 舵机线厚度
slack = 0.6;           // 线槽余量

/* [结构] */
width = 8;             // 线夹宽度（沿板边方向）
wall = 1.6;

/* [Hidden] */
ch_w = cables * cable_w + slack;
ch_h = cable_h * 2 + slack;          // 可以叠两层
slot = sheet_t - grip_fit;
body_h = grip_depth + wall;

// 截面画在 XY 平面，沿 Z 拉伸 width
linear_extrude(width) difference() {
  union() {
    // 夹子（U 形）：夹住板边
    square([slot + 2 * wall, body_h]);
    // 线槽（C 形），在夹子一侧
    translate([slot + 2 * wall - 0.01, body_h - ch_w - 2 * wall]) square([ch_h + 2 * wall, ch_w + 2 * wall]);
  }
  // 夹口
  translate([wall, -0.01]) square([slot, grip_depth]);
  // 线槽空腔
  translate([slot + 3 * wall, body_h - ch_w - wall]) square([ch_h, ch_w]);
  // 线槽开口：一条窄缝，比线厚宽、比线宽窄。舵机线竖着塞进去，进去后放平就不会掉出来
  translate([slot + 3 * wall + ch_h - 0.01, body_h - ch_w / 2 - wall - (cable_h + 0.5) / 2])
    square([wall + 0.1, cable_h + 0.5]);
}
// 夹口防滑凸点
for (z = [width / 2]) translate([wall - 0.01, grip_depth - 1.5, z]) rotate([0, 90, 0]) cylinder(d = 1.2, h = 0.35, $fn = 16);
