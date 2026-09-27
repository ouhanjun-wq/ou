// 仿生手 · 手掌 + 盖板 + 拇指支架 (palm, covers, thumb bracket)
//
// 手掌里上下两层各放 2 个 MG90S：
//   手背层（y = +LAYER_Y）：食指、小指的舵机；手心层（y = -LAYER_Y）：中指、无名指的舵机。
//   每个舵机的输出轴都横着（沿 X），舵盘和驱动连杆正好落在对应手指的中心平面上，
//   所以连杆是纯平面运动，不会别劲。舵机从手背 / 手心两面放进槽里，用盖板压住。
// 拇指：手掌根部手心一侧的"大鱼际"块里装拇指转动舵机（输出轴沿 Z），
//   舵机臂上装拇指支架，支架上再装拇指弯曲舵机，拇指近节直接装在它的舵盘上。
//
// 导出：
//   openscad -D 'part="palm"'           -o palm.stl palm.scad
//   openscad -D 'part="cover_dorsal"'   -o cover_dorsal.stl palm.scad
//   openscad -D 'part="cover_palmar"'   -o cover_palmar.stl palm.scad   （打印 2 个，一左一右，形状相同）
//   openscad -D 'part="thenar_cover"'   -o thenar_cover.stl palm.scad
//   openscad -D 'part="thumb_bracket"'  -o thumb_bracket.stl palm.scad
//
// 打印：PETG 或 PLA，层高 0.2，4 圈壁，30% 填充。
//   手掌：STL 已经摆好，手背面贴热床。手背层舵机槽朝下，槽顶靠 12 mm 桥接，不用支撑；
//         切片时"支撑：仅从热床生成"，给小指侧的 G 点耳朵托一下即可。
//   盖板、拇指支架：大平面朝下，不需要支撑。

include <hand_lib.scad>

part = "palm";   // [palm, cover_dorsal, cover_palmar, thenar_cover, thumb_bracket]

if (part == "palm") rotate([-90, 0, 0]) palm();   // 手背面贴热床
else if (part == "cover_dorsal") rotate([-90, 0, 0]) cover_dorsal();
else if (part == "cover_palmar") rotate([90, 0, 0]) cover_palmar();
else if (part == "thenar_cover") rotate([90, 0, 0]) thenar_cover();
else if (part == "thumb_bracket") thumb_bracket();
