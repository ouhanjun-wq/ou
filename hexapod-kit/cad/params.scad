// 18 舵机六足 · 3D 打印机身 · 共享参数和公共模块（本文件自己不导出 STL）
//
// 腿的几何尺寸和固件 firmware/hexapod_g7pro/params.h 的默认值一一对应：
//   coxa_len ↔ coxa    femur_len ↔ femur    tibia_len ↔ tibia
//   front_x / front_y / front_angle / mid_y / rear_x / rear_y / rear_angle 同名
// 所以打印这套机身，固件的腿长和安装位置参数都不用改。
//
// 坐标约定（和固件一致）：
//   机身坐标：X+ = 前，Y+ = 左，Z+ = 上，原点 = 机身中心，高度原点 = 大腿舵机轴。
//   腿坐标：原点 = 基节转轴，X+ = 腿朝外，Y+ = 逆时针一侧，Z+ = 上。
// 6 条腿的零件完全一样（不分左右），左右舵机转向的差别由固件里的 dir 参数处理。

/* [舵机 MG90S（量你自己的）] */
servo_w = 12.2;           // 机身宽度 (mm)
servo_l = 22.8;           // 机身长度，沿安装耳方向 (mm)
ear_span = 32.5;          // 两个安装耳外端之间的总长 (mm)
ear_hole_spacing = 28.0;  // 两个安装孔的中心距 (mm)
ear_z = 15.9;             // 机壳底面 → 安装耳下表面 (mm)
ear_t = 2.5;              // 安装耳厚度 (mm)
case_h = 22.7;            // 机壳底面 → 机壳顶面（不含输出轴）(mm)
shaft_off = 5.4;          // 输出轴中心 → 机身中心，沿长度方向 (mm)
shaft_gap = 4.0;          // 机壳顶面 → 舵机臂贴合面 (mm)

/* [舵机臂（单边臂，量你自己的）] */
horn_hub_d = 7.6;         // 舵机臂中心圆盘直径 (mm)
horn_len = 18.5;          // 中心 → 臂尖 (mm)
horn_w_hub = 6.2;         // 臂根宽度 (mm)
horn_w_tip = 4.2;         // 臂尖宽度 (mm)
horn_t = 1.6;             // 臂厚 = 零件上凹槽深度 (mm)
horn_holes = [10.5, 14.5];// 固定舵机臂的两个孔到中心的距离 (mm)
horn_screw_access = 5.5;  // 中心过孔：舵机臂中心螺丝和螺丝刀从这里进 (mm)

/* [腿的几何（和固件 params.h 一致）] */
coxa_len = 28;            // 基节转轴 → 大腿舵机轴 (mm)
femur_len = 50;           // 大腿舵机轴 → 小腿舵机轴 (mm)
tibia_len = 75;           // 小腿舵机轴 → 脚尖 (mm)
front_x = 60; front_y = 40; front_angle = 45;
mid_y = 55;
rear_x = 60; rear_y = 40; rear_angle = 135;

/* [打印余量和螺丝] */
clearance = 0.4;          // 方孔比舵机大多少 (mm)；太紧改 0.5
screw_pilot = 1.6;        // M2 自攻螺丝底孔（舵机安装耳）(mm)
horn_screw_hole = 2.0;    // 舵机臂固定螺丝过孔 (mm)
m3_hole = 3.4;            // M3 通孔 (mm)

/* [机身] */
plate_t = 3.0;            // 底板 / 甲板厚度 (mm)
standoff_h = 25;          // 底板和甲板之间的 M3 铜柱长度 (mm)
bar_t = 4.0;              // 大腿板厚度 (mm)
mount_t = 3.0;            // 舵机安装板厚度 (mm)
coxa_horn_z = 11;         // 基节舵机臂贴合面高度（相对大腿轴）(mm)

/* [Hidden] */
// 6 条腿：[髋轴 X, 髋轴 Y, 朝向°]，顺序 LF LM LR RF RM RR（和固件 leg 0..5 一致）
legs = [
  [ front_x,  front_y,  front_angle],
  [ 0,        mid_y,    90],
  [-rear_x,   rear_y,   rear_angle],
  [ front_x, -front_y, -front_angle],
  [ 0,       -mid_y,   -90],
  [-rear_x,  -rear_y,  -rear_angle]
];
leg_names = ["LF", "LM", "LR", "RF", "RM", "RR"];

pocket_w = servo_w + clearance;
pocket_l = servo_l + clearance;
// 舵机本地坐标：输出轴 = +Z，舵机臂贴合面在 z = 0，机身沿 +X 伸出（机身中心 x = shaft_off）
ear_low = -shaft_gap - case_h + ear_z;       // 安装耳“朝机壳底”的那一面，安装板贴在这里
plate_top_z = coxa_horn_z + ear_low;         // 底板上表面高度（相对大腿轴）
deck_z = plate_top_z + standoff_h;           // 甲板下表面高度

module at_leg(l) translate([l[0], l[1]]) rotate(l[2]) children();

// 舵机实体（装配预览 / 干涉检查用）
servo_color = "DimGray";
servo_label = false;      // 装配效果图里给舵机两侧画套件那样的粉色标签（干涉检查时关掉）
module servo_body() {
  if (servo_label) color([0.85, 0.2, 0.65])
    for (s = [-1, 1]) translate([shaft_off - 7, s > 0 ? servo_w / 2 + 0.02 : -servo_w / 2 - 0.32, -shaft_gap - case_h + 6]) cube([14, 0.3, 9]);
  color(servo_color) {
    translate([shaft_off - servo_l / 2, -servo_w / 2, -shaft_gap - case_h]) cube([servo_l, servo_w, case_h]);
    // 耳朵和输出轴都留 0.05 mm，避免“刚好贴着”被干涉检查当成碰撞
    translate([shaft_off - ear_span / 2, -servo_w / 2, ear_low + 0.05]) cube([ear_span, servo_w, ear_t - 0.05]);
    translate([0, 0, -shaft_gap]) cylinder(d = 5, h = shaft_gap - 0.05);
  }
}

// 安装板上的舵机方孔 + 两个安装耳螺丝底孔（在舵机本地坐标里，穿过安装板）
module servo_mount_cut() {
  translate([shaft_off - pocket_l / 2, -pocket_w / 2, ear_low - mount_t - 1]) cube([pocket_l, pocket_w, mount_t + 2]);
  for (s = [-1, 1]) translate([shaft_off + s * ear_hole_spacing / 2, 0, ear_low - mount_t - 1])
    cylinder(d = screw_pilot, h = mount_t + 2, $fn = 16);
}

// 单边舵机臂的 2D 外形：中心在原点，臂沿 +X
module horn_2d(extra = 0) {
  hull() {
    circle(d = horn_hub_d + extra);
    translate([0, -(horn_w_hub + extra) / 2]) square([0.01, horn_w_hub + extra]);
    translate([horn_len - horn_w_tip / 2, 0]) circle(d = horn_w_tip + extra);
  }
}

// 舵机臂凹槽：从 z = 0 往 +Z 挖 horn_t 深，加中心过孔和两个螺丝过孔（贯穿 depth）
module horn_cut(depth) {
  translate([0, 0, -0.01]) linear_extrude(horn_t + 0.01) horn_2d(clearance);
  translate([0, 0, -1]) cylinder(d = horn_screw_access, h = depth + 2, $fn = 24);
  for (r = horn_holes) translate([r, 0, -1]) cylinder(d = horn_screw_hole, h = depth + 2, $fn = 16);
}
