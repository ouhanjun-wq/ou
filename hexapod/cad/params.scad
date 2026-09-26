// 六足机器人 · 共享参数（被 body_plate / deck / femur / tibia 引用，本文件自己不导出 STL）
//
// 腿的几何尺寸和 Seeker 固件 mcu_ws/lib/RobotConfig/HexapodConfig.h 一一对应：
//   body_half_length ↔ kBodyHalfLength    body_half_width ↔ kBodyHalfWidth
//   L1 ↔ kL1（髋轴 → 膝轴）              L2 ↔ kL2（膝轴 → 脚尖）
// 改了这里的数字，固件里的同名常数也要一起改。
//
// ★ 先用游标卡尺量你的舵机和舵机臂，改 [舵机] 和 [舵机臂] 两组数字，再导出 STL。

/* [舵机 MG90S（量你自己的）] */
servo_w = 12.2;           // 机身宽度 (mm)
servo_l = 22.8;           // 机身长度，沿安装耳方向 (mm)
ear_span = 32.5;          // 两个安装耳外端之间的总长 (mm)
ear_hole_spacing = 28.0;  // 两个安装孔的中心距 (mm)
ear_z = 15.9;             // 机身底面 → 安装耳下表面 (mm)
case_h = 22.7;            // 机身底面 → 机壳顶面（不含输出轴）(mm)
shaft_off = 5.4;          // 输出轴中心 → 机身中心，沿长度方向 (mm)
shaft_gap = 4.0;          // 机壳顶面 → 舵机臂贴合面（花键凸台高度）(mm)

/* [舵机臂（单边臂，量你自己的）] */
horn_hub_d = 7.6;         // 舵机臂中心圆盘直径 (mm)
horn_len = 18.5;          // 中心 → 臂尖 (mm)
horn_w_hub = 6.2;         // 臂根宽度 (mm)
horn_w_tip = 4.2;         // 臂尖宽度 (mm)
horn_t = 1.6;             // 臂厚 = 零件上凹槽深度 (mm)
horn_holes = [10.5, 14.5];// 用来固定舵机臂的两个孔到中心的距离 (mm)
horn_screw_access = 5.5;  // 中心过孔：舵机臂中心螺丝和螺丝刀从这里进 (mm)

/* [腿的几何（和固件一致）] */
body_half_length = 60;    // 前 / 后腿髋轴到机身中心的 X 距离 (mm)
body_half_width = 40;     // 左 / 右腿髋轴到机身中心的 Y 距离 (mm)
L1 = 45;                  // 大腿：髋轴 → 膝轴 (mm)
L2 = 65;                  // 小腿：膝轴 → 脚尖 (mm)

/* [打印余量] */
clearance = 0.4;          // 方孔比舵机大多少 (mm)；太紧就加大到 0.5
screw_pilot = 1.6;        // M2 自攻螺丝底孔 (mm)
m3_hole = 3.4;            // M3 通孔 (mm)

/* [Hidden] */
// 6 条腿：[髋轴 X, 髋轴 Y, 朝外方向角°]，顺序 FL FR ML MR RL RR（和固件的 leg 0..5 一致）
legs = [
  [ body_half_length,  body_half_width,   60],
  [ body_half_length, -body_half_width,  -60],
  [ 0,                 body_half_width,   90],
  [ 0,                -body_half_width,  -90],
  [-body_half_length,  body_half_width,  120],
  [-body_half_length, -body_half_width, -120]
];
leg_names = ["FL", "FR", "ML", "MR", "RL", "RR"];

pocket_w = servo_w + clearance;
pocket_l = servo_l + clearance;
servo_end_out = servo_l / 2 - shaft_off;   // 输出轴 → 机身外端 (mm)
servo_end_in = servo_l / 2 + shaft_off;    // 输出轴 → 机身内端 (mm)

// 单边舵机臂的 2D 外形：中心在原点，臂沿 +X
module horn_2d(extra = 0) {
  hull() {
    circle(d = horn_hub_d + extra);
    translate([0, -(horn_w_hub + extra) / 2]) square([0.01, horn_w_hub + extra]);
    translate([horn_len - horn_w_tip / 2, 0]) circle(d = horn_w_tip + extra);
  }
}

// 舵机臂凹槽（从 z = 0 往 +Z 挖 horn_t 深）+ 中心过孔 + 两个固定孔
module horn_cut(depth_through = 10) {
  translate([0, 0, -0.01]) linear_extrude(horn_t + 0.01) horn_2d(clearance);
  translate([0, 0, -1]) cylinder(d = horn_screw_access, h = depth_through + 2);
  for (r = horn_holes) translate([r, 0, -1]) cylinder(d = screw_pilot, h = depth_through + 2);
}
