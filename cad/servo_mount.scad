// 仿生蝴蝶 · 舵机座 (servo mount)
//
// 两个舵机左右并排装在前面板上，输出轴朝前；中间的方形套筒套在 4×4 mm 碳纤维方管主梁上。
// 舵机从前面插进方孔，安装耳压在面板前面，用 M2 自攻螺丝固定。
//
// 打印：PETG，层高 0.2 mm，3 圈壁，20–30% 填充。
//       前面板平放在热床上（套筒朝上），不需要支撑。
// 坐标：X = 左右，Y = 上下，Z = 前后（Z+ 朝机尾）。
//
// ★ 先用游标卡尺量你的舵机，改下面 [舵机尺寸] 里的数字，再导出 STL。

/* [舵机尺寸（量你自己的舵机）] */
servo_w = 12.0;             // 机身宽度 (mm)
servo_l = 23.0;             // 机身长度，沿安装耳方向 (mm)
servo_ear_span = 32.5;      // 两个安装耳外端之间的总长 (mm)
servo_hole_spacing = 28.0;  // 两个安装孔的中心距 (mm)

/* [主梁] */
keel = 4.0;                 // 碳纤维方管边长 (mm)

/* [打印余量] */
clearance = 0.3;            // 孔比零件大多少 (mm)；太紧就加大到 0.4

/* [结构] */
plate_t = 2.4;              // 前面板厚度 (mm)
margin = 3.0;               // 面板左右两侧的边距 (mm)
sleeve_wall = 1.4;          // 套筒壁厚 (mm)
sleeve_len = 22;            // 套筒向后伸出的长度 (mm)
gusset_t = 1.2;             // 加强筋厚度 (mm)
screw_d = 1.6;              // M2 自攻螺丝底孔 (mm)

/* [Hidden] */
$fn = 40;
hole = keel + clearance;                  // 主梁孔
sleeve_o = hole + 2 * sleeve_wall;        // 套筒外径
rib = sleeve_o;                           // 两个舵机之间的中筋宽度 = 套筒外径
pocket_w = servo_w + clearance;
pocket_l = servo_l + clearance;
plate_w = 2 * pocket_w + rib + 2 * margin;
plate_h = servo_ear_span + 3;
servo_x = rib / 2 + pocket_w / 2;         // 舵机中心的 X 位置

module plate() {
  difference() {
    translate([-plate_w / 2, -plate_h / 2, 0]) cube([plate_w, plate_h, plate_t]);
    for (sx = [-1, 1]) {
      // 舵机方孔
      translate([sx * servo_x - pocket_w / 2, -pocket_l / 2, -1]) cube([pocket_w, pocket_l, plate_t + 2]);
      // 安装螺丝孔
      for (sy = [-1, 1])
        translate([sx * servo_x, sy * servo_hole_spacing / 2, -1]) cylinder(d = screw_d, h = plate_t + 2);
    }
  }
}

module sleeve() {
  translate([-sleeve_o / 2, -sleeve_o / 2, 0]) cube([sleeve_o, sleeve_o, plate_t + sleeve_len]);
}

// 上下两块三角加强筋，把套筒和面板连成一体
module gussets() {
  g_h = plate_h / 2 - sleeve_o / 2 - 1.5;
  for (sy = [-1, 1])
    mirror([0, sy < 0 ? 1 : 0, 0])
      translate([gusset_t / 2, sleeve_o / 2 - 0.01, plate_t - 0.01])
        rotate([0, -90, 0])
          linear_extrude(gusset_t)
            polygon([[0, 0], [sleeve_len * 0.7, 0], [0, g_h]]);
}

difference() {
  union() {
    plate();
    sleeve();
    gussets();
  }
  // 主梁方孔，贯穿面板和套筒
  translate([-hole / 2, -hole / 2, -1]) cube([hole, hole, plate_t + sleeve_len + 2]);
}

echo(str("面板尺寸 ", plate_w, " x ", plate_h, " mm；两个舵机中心距 ", 2 * servo_x, " mm"));
