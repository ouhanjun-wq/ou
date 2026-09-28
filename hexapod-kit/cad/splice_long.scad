// 长拼接条：打 1 个。装在底板下面，沿前后方向的中缝（y = 0）把左右两半拼起来。
include <parts.scad>
translate([0, 0, -splice_z]) splice_long();
