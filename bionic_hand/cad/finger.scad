// 仿生手 · 手指零件 (finger parts)
//
// 每根手指 = 近节 P + 远节 M（中节和指尖做成一体）+ 联动杆 + 驱动连杆：
//   手掌 ─MCP 销─ 近节 P ─PIP 销─ 远节 M
//   · 联动杆：手掌上的 G 点 → 远节上的 C 点。近节一转，远节跟着弯（交叉四连杆）。
//   · 驱动连杆：舵机臂 → 近节上的驱动销 D（拇指没有：近节直接装在舵机臂上）。
// 连杆长度、销孔位置都来自 linkage_params.scad，由 tools/finger_linkage.py 算好并校核
// （不卡死、传动角 ≥ 30°、舵机扭矩够用）。
//
// 导出（每根手指各导一次，finger = index / middle / ring / pinky / thumb）：
//   openscad -D 'finger="index"' -D 'part="proximal"' -o index_proximal.stl finger.scad
//   part = proximal（近节）/ distal（远节）/ link（联动杆）/ rod（驱动连杆，拇指没有）/ all（预览）
//
// 打印：PLA 或 PETG，层高 0.2，4 圈壁，40% 填充。所有零件侧躺打印（销孔竖直朝上），不要支撑。
// 销轴：M2 螺丝 + 防松螺母（MCP、PIP、D 点、连杆两端）；C 点和 G 点用 M2×8 自攻螺丝。

include <hand_lib.scad>

finger = "index";   // [index, middle, ring, pinky, thumb]
part = "all";       // [all, proximal, distal, link, rod]

// 打印摆放：侧躺，X 轴朝上
module print_pose() { translate([0, 0, W / 2]) rotate([0, -90, 0]) children(); }

if (part == "proximal") print_pose() proximal(finger);
else if (part == "distal") print_pose() distal(finger);
else if (part == "link") print_pose() coupling_link(finger);
else if (part == "rod") { assert(finger_params(finger)[4] != "direct", "拇指没有驱动连杆"); print_pose() drive_rod(finger); }
else {
  // 预览：伸直的手指（手指自己的坐标系）。装在手掌上的整体效果见 assembly.scad。
  color("white") proximal(finger);
  color("lightgrey") translate([0, 0, finger_params(finger)[0]]) distal(finger);
}
