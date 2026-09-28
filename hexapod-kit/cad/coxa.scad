// 基节支架 coxa：打 6 个（6 条腿通用）。顶板朝下放，竖板朝上。
include <parts.scad>
translate([0, 0, coxa_horn_z + top_t]) rotate([180, 0, 0]) coxa_bracket();
