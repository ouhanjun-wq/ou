// 仿生蝴蝶 · 翼根座 (wing root socket)
//
// 一侧翅膀的所有碳杆（前缘 Ø2、两根翅脉 Ø1.5、后翅杆 Ø1.5）都插在这个零件上；
// 零件内侧的插槽套在舵机臂上，用 M2 螺丝 + 502 固定。
//
// 打印：PETG，层高 0.2 mm，3 圈壁，30% 填充；平放打印，不需要支撑。
//       左右各打印一个：side = "left" 和 side = "right"。
// 坐标：Y = 向外（翼尖方向），X = 向前（机头方向），Z = 向上。
//
// ★ 先量你的舵机臂（单臂），改下面 [舵机臂] 里的数字。

/* [左右] */
side = "left";              // "left" 或 "right"

/* [碳杆孔] */
rod_clearance = 0.1;        // 孔比碳杆大多少 (mm)
hole_depth = 15;            // 碳杆插入深度 (mm)
// 每根杆：[入口 X 位置 (mm), 角度 (°，正 = 向前偏), 碳杆直径 (mm)]
rods = [
  [ 9,  12, 2.0],           // 前缘
  [ 1,  -5, 1.5],           // 前翅翅脉 1
  [-7, -22, 1.5],           // 前翅翅脉 2
  [-15, -40, 1.5]           // 后翅杆
];

/* [舵机臂] */
horn_w = 5.0;               // 舵机臂宽度 (mm)
horn_t = 1.6;               // 舵机臂厚度 (mm)
horn_insert = 12;           // 舵机臂插进插槽的深度 (mm)
horn_screw_from_end = 4;    // 螺丝孔离插槽口的距离 (mm)，对准舵机臂最外面的孔

/* [结构] */
block_depth = 16;           // 翼根块厚度（向外）(mm)
block_h = 5;                // 翼根块高度 (mm)
rod_wall = 2.0;             // 碳杆孔周围的最小壁厚 (mm)
tab_len = 14;               // 插槽段长度 (mm)
tab_wall = 1.6;             // 插槽壁厚 (mm)
screw_d = 1.6;              // M2 自攻螺丝底孔 (mm)

/* [Hidden] */
$fn = 32;
tab_w = horn_w + 0.3 + 2 * tab_wall;

module socket_right() {
  difference() {
    union() {
      // 翼根块：沿着碳杆走向的扇形（比长方块轻约 30%）
      linear_extrude(block_h)
        hull() {
          for (r = rods) {
            translate([r[0], block_depth - 0.01]) square([0.01, 0.01]);
            // 孔底位置
            translate([r[0] - hole_depth * sin(r[1]), block_depth - hole_depth * cos(r[1])])
              circle(d = r[2] + 2 * rod_wall);
            // 外侧面上的孔口，两边各留壁厚
            translate([r[0] - (r[2] / 2 + rod_wall), block_depth - 2]) square([r[2] + 2 * rod_wall, 2]);
          }
          translate([-tab_w / 2, 0]) square([tab_w, 1]);
        }
      // 舵机臂插槽段（向内）
      translate([-tab_w / 2, -tab_len, 0]) cube([tab_w, tab_len + 0.01, block_h]);
    }
    // 碳杆孔：从外侧面斜着钻进去
    for (r = rods)
      translate([r[0], block_depth + 1, block_h / 2])
        rotate([0, 0, -r[1]])
          rotate([90, 0, 0])
            cylinder(d = r[2] + rod_clearance, h = hole_depth + 1);
    // 舵机臂插槽
    translate([-(horn_w + 0.3) / 2, -tab_len - 1, (block_h - horn_t - 0.25) / 2])
      cube([horn_w + 0.3, horn_insert + 1, horn_t + 0.25]);
    // 固定螺丝孔（竖直穿过插槽）
    translate([0, -tab_len + horn_screw_from_end, -1]) cylinder(d = screw_d, h = block_h + 2);
  }
}

// 左侧零件是右侧零件前后镜像
if (side == "right") socket_right();
else mirror([1, 0, 0]) socket_right();
