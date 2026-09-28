// 基节舵机底部托架 hip_cradle：打 6 个（6 条腿通用）。底板朝下平放。
// 装配：先把 M3 螺母放进托架底板的六角槽，再把基节舵机的底部放进托架，托架两头的耳朵顶住底板下面，
// 和舵机安装耳一起用 M2 × 10 自攻螺丝拧紧。
include <parts.scad>
translate([0, 0, -cr_floor_z0]) hip_cradle_local();
