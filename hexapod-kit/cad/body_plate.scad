// 机身底板：打 1 个。平放。
include <parts.scad>
translate([0, 0, -(plate_top_z - plate_t)]) body_plate();
