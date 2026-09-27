// 激光雷达平台：打 1 个。平放。装在甲板上方 4 根 M3×50 铜柱上。
include <parts.scad>
translate([0, 0, -lidar_z]) lidar_mount();
