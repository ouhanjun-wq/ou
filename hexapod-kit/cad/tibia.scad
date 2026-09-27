// 小腿 tibia：打 6 个（6 条腿通用）。舵机安装板一面朝下。
include <parts.scad>
translate([0, 0, tib_plate_y0 + mount_t]) rotate([-90, 0, 0]) tibia_part();
