// 六足机器人 · 机身底板 (body plate)
//
// 6 个髋舵机从上往下插进方孔：输出轴朝下，安装耳压在底板上面，用 M2 自攻螺丝固定。
// 大腿装在底板下面的舵机臂上。电池用魔术贴扎带绑在底板下面正中间。
// 4 个 M3 孔装 25 mm 铜柱，上面再叠一层甲板 (deck.scad)。
//
// 打印：PETG 或 PLA+，层高 0.2 mm，3 圈壁，20% 填充，平放，不需要支撑。
// 坐标：X+ = 机器人前方，Y+ = 左边，原点 = 机身中心（和固件 / URDF 一致）。

include <params.scad>

/* [底板] */
plate_t = 3.0;                    // 厚度 (mm)
outline_reach = 6;                // 外轮廓圆心：髋轴往外多少 (mm)
outline_r = 9.5;                  // 外轮廓圆半径 (mm)
standoffs = [[34, 22], [34, -22], [-34, 22], [-34, -22]];  // M3 铜柱位置（甲板一样）
strap_slots = [[18, 18], [-18, 18], [18, -18], [-18, -18]]; // 电池扎带槽中心
strap_w = 22;                     // 扎带槽长度（20 mm 宽魔术贴）
strap_t = 3;                      // 扎带槽宽度
wire_hole = [9, 4.5];             // 每条腿内侧的走线槽 [长, 宽]
wire_hole_at = 27;                // 走线槽中心：髋轴往里多少 (mm)
grid_pitch = 10;                  // 中间安装孔阵的间距 (mm)
grid_x = [-20, 20];               // 孔阵 X 范围
grid_y = [-5, 5];                 // 孔阵 Y 范围
label_depth = 0.6;                // 腿名刻字深度 (mm)

/* [Hidden] */
$fn = 48;

module at_leg(l) translate([l[0], l[1]]) rotate(l[2]) children();

module outline_2d() {
  hull() for (l = legs) at_leg(l) translate([outline_reach, 0]) circle(r = outline_r);
}

// 腿坐标系：+X 朝外，输出轴在原点，机身往 -X 方向伸
module servo_pocket_2d() {
  translate([-servo_end_in, -pocket_w / 2]) square([pocket_l, pocket_w]);
}

module ear_holes_2d() {
  for (s = [-1, 1]) translate([-shaft_off + s * ear_hole_spacing / 2, 0]) circle(d = screw_pilot);
}

module holes_2d() {
  for (l = legs) at_leg(l) {
    servo_pocket_2d();
    ear_holes_2d();
    translate([-wire_hole_at, 0]) square([wire_hole[1], wire_hole[0]], center = true);
  }
  for (p = standoffs) translate(p) circle(d = m3_hole);
  for (p = strap_slots) translate(p) square([strap_w, strap_t], center = true);
  for (x = [grid_x[0] : grid_pitch : grid_x[1]], y = [grid_y[0] : grid_pitch : grid_y[1]])
    translate([x, y]) circle(d = m3_hole);
}

difference() {
  linear_extrude(plate_t) difference() { outline_2d(); holes_2d(); }
  // 在上表面刻腿名，装舵机时对照固件的 FL/FR/ML/MR/RL/RR
  for (i = [0 : 5]) at_leg(legs[i])
    translate([-shaft_off, pocket_w / 2 + 6, plate_t - label_depth])
      linear_extrude(label_depth + 1)
        rotate(-legs[i][2]) text(leg_names[i], size = 5, halign = "center", valign = "center");
  // 前方箭头
  translate([body_half_length - 20, 0, plate_t - label_depth])
    linear_extrude(label_depth + 1) polygon([[8, 0], [-2, 5], [-2, -5]]);
}
