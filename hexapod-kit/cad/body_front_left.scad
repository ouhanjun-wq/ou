// 底板前左（左前腿）：打 1 个。平放（有沉头孔的一面朝上）。
include <parts.scad>
translate([0, 0, -(plate_top_z - plate_t)]) body_plate_piece(front = true, left = true);
