// 短拼接条：打 2 个。装在底板下面，把前块和后块拼起来（左右各一个，两个一样）。
include <parts.scad>
translate([0, 0, -splice_z]) splice_short();
