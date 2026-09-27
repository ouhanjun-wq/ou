// 整机装配预览 + 干涉检查（不打印）。
//   pose = "stand" / "sit" / "custom"；custom 时用 coxa_deg / femur_deg / tibia_deg（6 条腿一样）。
//   show = "all"，或者干涉检查用的子集（见 tools/check_clearance.py）。
include <parts.scad>

pose = "stand";
coxa_deg = 0; femur_deg = 0; tibia_deg = 0;
leg_only = -1;            // >= 0：只画这一条腿（干涉检查用）
show = "all";

stand_h = 60; stand_r = 80;     // 和固件 height / stance 一致
sit_h = 18; sit_r = 95;

function ik(h, r) = let(
    rr = r - coxa_len, d = sqrt(rr * rr + h * h),
    f = atan2(-h, rr) + acos((femur_len * femur_len + d * d - tibia_len * tibia_len) / (2 * femur_len * d)),
    t = acos((femur_len * femur_len + tibia_len * tibia_len - d * d) / (2 * femur_len * tibia_len)) - 90)
  [0, f, t];

angles = pose == "stand" ? ik(stand_h, stand_r) : pose == "sit" ? ik(sit_h, sit_r) : [coxa_deg, femur_deg, tibia_deg];
echo(str("pose ", pose, ": coxa ", angles[0], "  femur ", angles[1], "  tibia ", angles[2]));

module leg_moving(a, part) {
  rotate(a[0]) {
    if (part == "all" || part == "coxa") { color("SteelBlue") coxa_bracket(); femur_servo_frame() servo_body(); }
    translate([coxa_len, 0, 0]) rotate([0, -a[1], 0]) {
      if (part == "all" || part == "femur") color("Orange") femur_bar();
      translate([femur_len, 0, 0]) rotate([0, 90 - a[2], 0])
        if (part == "all" || part == "tibia") { color("SeaGreen") tibia_part(); tibia_servo_frame() servo_body(); }
    }
  }
}

module frame_fixed() {
  color("Wheat") body_plate();
  for (l = legs) coxa_servo_frame(l) servo_body();
  for (p = standoffs) translate([p[0], p[1], plate_top_z]) color("Gold") cylinder(d = 5, h = standoff_h, $fn = 6);
  color("Wheat") deck();
  for (p = standoffs) translate([p[0], p[1], deck_z + plate_t]) color("Gold") cylinder(d = 5, h = lidar_standoff_h, $fn = 6);
  color("Wheat") lidar_mount();
  // 雷达占位（外形只是示意）
  color("DimGray") translate([0, 0, lidar_z + plate_t]) cylinder(d = 70, h = 30, $fn = 48);
}

if (show == "all") {
  frame_fixed();
  for (i = [0 : 5]) if (leg_only < 0 || leg_only == i) at_leg(legs[i]) leg_moving(angles, "all");
}
