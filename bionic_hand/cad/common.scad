// 仿生手 · 公共尺寸 (shared dimensions)
//
// 所有零件都 include 这个文件。★ 打印前先用游标卡尺量你的舵机，改 [舵机尺寸]。
// 坐标（手掌和手指通用）：X = 左右（销轴方向），Y = 手背方向为 +，Z = 沿手指指向指尖为 +。
// 所有单位 mm。

include <linkage_params.scad>

/* [舵机尺寸：MG90S（量你自己的舵机）] */
SV_L = 22.8;        // 机身长度（安装耳方向）
SV_W = 12.2;        // 机身宽度
SV_H = 22.5;        // 机身高度：底面 → 顶面（不含输出轴凸台）
SV_FL = 32.2;       // 两个安装耳外端之间的总长
SV_FT = 2.5;        // 安装耳厚度
SV_FZ = 15.6;       // 底面 → 安装耳下表面
SV_SH = 5.9;        // 输出轴中心偏离机身中心的距离（沿长度方向）
SV_BOSS_R = 6.0;    // 顶面齿轮凸台半径
H_ROD = 31.5;       // 机身底面 → 连杆中心面：凸台 4.5 + 舵盘 2 + 半个连杆厚度 2 + 余量

/* [销轴与间隙] */
PIN = 2.3;          // M2 螺丝当销轴：通孔直径
PIN_TAP = 1.7;      // M2 自攻螺丝底孔
CL = 0.3;           // 活动配合的单边间隙；太紧就加到 0.4

/* [手指截面] */
W = 15;             // 手指宽度
T = 13;             // 手指厚度
TG = 6;             // 关节"舌头"宽度（插在叉子中间的那片）
RJ = 5.5;           // 关节外圆半径
RB = 3.2;           // 小销孔外圆半径（连杆两端、驱动销）
LINK_T = 3;         // 联动杆厚度
ROD_T = 4;          // 驱动连杆厚度
PITCH = 24;         // 相邻手指中心距

/* [Hidden] */
$fn = 40;
CHEEK_IN = TG / 2 + CL;             // 叉子内侧面 |x|
LINK_X0 = W / 2 + 0.4;              // 联动杆内侧面 |x|（在手指 +x 一侧）
LINK_X1 = LINK_X0 + LINK_T;
FINGER_X = [-1.5, -0.5, 0.5, 1.5] * PITCH;   // 食指、中指、无名指、小指中心线

function finger_params(name) =
  name == "index" ? FINGER_INDEX :
  name == "middle" ? FINGER_MIDDLE :
  name == "ring" ? FINGER_RING :
  name == "pinky" ? FINGER_PINKY : FINGER_THUMB;

// 在 Y-Z 平面画的 2D 轮廓（2D 的 x = 模型 Z，2D 的 y = 模型 Y），沿 X 拉伸到 [x0, x1]。
module side(x0, x1) {
  multmatrix([[0, 0, -1, 0], [0, 1, 0, 0], [1, 0, 0, 0], [0, 0, 0, 1]])
    translate([0, 0, -x1]) linear_extrude(x1 - x0) children();
}

// 沿 X 方向的圆孔（销孔），中心在 Y-Z 平面上的点 p = [z, y]。
module x_hole(p, d, x0 = -50, x1 = 50) {
  side(x0, x1) translate(p) circle(d = d);
}

// 2D：以 p 为圆心、r 为半径的圆（p = [z, y]）
module disc(p, r) { translate(p) circle(r); }

// 2D：角度 a（从 +Z 转向 +Y，度）方向、长度 r 的点
function polar(r, a) = [r * cos(a), r * sin(a)];

// 两头带销孔的平板连杆，沿 +Z 长 len，厚 t（x ∈ [0, t]）
module bar(len, t) {
  difference() {
    side(0, t) hull() { disc([0, 0], RB); disc([len, 0], RB); }
    x_hole([0, 0], PIN);
    x_hole([len, 0], PIN);
  }
}
