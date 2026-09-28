// 转轴套 pivot_bushing：打 6 个（6 条腿通用）。法兰朝下平放。
// U 形支架装好后，从下面穿过下摆臂的孔顶到托架底面，M3 × 12 螺丝穿过它拧进托架里的螺母。
include <parts.scad>
translate([0, 0, -(bush_z0 - bush_flange[1])]) pivot_bushing_local();
