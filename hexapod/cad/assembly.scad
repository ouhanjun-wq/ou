// 六足机器人 · 整机装配预览（只用来看位置关系、检查干涉，不用打印）
//
// 先导出 stl/ 里的零件，再运行：
//   openscad -o img/assembly.png --imgsize=1200,800 --camera=0,0,-15,62,0,215,620 assembly.scad
//
// 灰色 = MG90S 舵机，绿色 = 3S 电池，黑色 = LD14P 雷达（外形示意），金色 = M3 铜柱。

include <params.scad>

hip_deg = 0;         // 髋关节角度（0 = 大腿沿安装方向朝外）
knee_deg = 60;       // 膝关节角度（0 = 小腿水平，90 = 竖直向下），和固件定义一致；推荐站姿 60°

/* [Hidden] */
plate_t = 3.0;
standoff_h = 25;
ear_t = 2.5;
horn_z = plate_t - (case_h - ear_z - ear_t) - shaft_gap;   // 髋舵机臂贴合面 = 大腿顶面的 Z
w0 = 7;                                                    // 大腿侧板外表面的 Y（= arm_w / 2）
z_k = -3.0 - 0.4 - pocket_w / 2;                           // 膝轴相对大腿顶面的 Z
tib_y = w0 + (case_h - ear_z) + shaft_gap;                 // 小腿内表面的 Y
$fn = 32;

module servo_block() {   // 舵机自己的坐标：底面 z = 0，输出轴在原点朝 +Z
  color("DimGray") {
    translate([-servo_end_in, -servo_w / 2, 0]) cube([servo_l, servo_w, case_h]);
    translate([-shaft_off - ear_span / 2, -servo_w / 2, ear_z]) cube([ear_span, servo_w, ear_t]);
    cylinder(d = 5, h = case_h + shaft_gap);
  }
}

module leg_parts(right) {
  color("SteelBlue") import(right ? "stl/femur_b.stl" : "stl/femur_a.stl");
  mirror([0, right ? 1 : 0, 0]) {
    // 膝舵机：输出轴朝大腿外侧，安装耳贴在侧板外表面
    translate([L1, w0, z_k]) rotate([-90, 0, 0]) translate([0, 0, -ear_z]) servo_block();
    // 小腿：凹槽面朝舵机
    translate([L1, tib_y, z_k]) rotate([-90, 0, 0]) rotate(knee_deg)
      color("Orange") translate([0, 0, 4]) mirror([0, 0, 1]) import("stl/tibia.stl");
  }
}

module leg(i) {
  l = legs[i];
  translate([l[0], l[1], 0]) rotate(l[2]) {
    // 髋舵机：输出轴朝下插在底板方孔里，安装耳压在底板上面
    translate([0, 0, plate_t + ear_z + ear_t]) mirror([0, 0, 1]) servo_block();
    translate([0, 0, horn_z]) rotate(hip_deg) leg_parts(l[1] < 0);
  }
}

color("LightSteelBlue") import("stl/body_plate.stl");
for (i = [0 : 5]) leg(i);
color("LightSteelBlue") translate([0, 0, plate_t + standoff_h]) import("stl/deck.stl");
for (x = [-34, 34], y = [-22, 22]) color("Gold") translate([x, y, plate_t]) cylinder(d = 5, h = standoff_h);
color("ForestGreen") translate([-37, -17, -24]) cube([75, 34, 24]);
for (x = [-20, 20], y = [-20, 20]) color("White") translate([x, y, plate_t + standoff_h + 3]) cylinder(d = 5, h = 20);
color("Black") translate([0, 0, plate_t + standoff_h + 23]) cylinder(d = 70, h = 36);   // 架在 4 根 M3×20 尼龙柱上

echo(str("站立（膝 ", knee_deg, "°）时底板下表面离地约 ",
         -(horn_z + z_k - L2 * sin(knee_deg)), " mm；脚尖离髋轴水平距离约 ",
         L1 + L2 * cos(knee_deg), " mm"));
