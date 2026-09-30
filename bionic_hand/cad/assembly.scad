// 仿生手 · 总装预览（不用打印）
//
// 按 tools/finger_linkage.py 同样的四连杆关系，把所有零件摆到指定姿态，
// 用来看装配关系、检查干涉。
//   close  = 0 张开 … 1 握拳（四指 MCP 弯曲 = close × THETA_MAX）
//   psi    = 拇指转动角（0 = 张开在侧面，120 = 对掌）
//   tflex  = 拇指弯曲角（MCP，度）
//   openscad -D close=0.5 -D psi=90 -D tflex=40 -o hand.png assembly.scad

include <hand_lib.scad>

close = 0.0;
psi = 0;
tflex = 0;
THETA_MAX = 85;
show_palm = true;
render_hand = true;   // 被别的文件 include 时设成 false

function rot2(v, a) = [v[0] * cos(a) + v[1] * sin(a), -v[0] * sin(a) + v[1] * cos(a)];   // 弯曲 a 度（朝 -y）
function ang(v) = atan2(v[1], v[0]);
// 两圆交点里离 ref 较近的那个
function circ(c0, r0, c1, r1, ref) =
  let(d = norm(c1 - c0), a = (r0 * r0 - r1 * r1 + d * d) / (2 * d), h = sqrt(max(0, r0 * r0 - a * a)),
      e = (c1 - c0) / d, p = c0 + a * e, n = [-e[1], e[0]], p1 = p + h * n, p2 = p - h * n)
  norm(p1 - ref) < norm(p2 - ref) ? p1 : p2;

// 手指（手指坐标）摆到 MCP 弯曲角 t
module finger_pose(name, t, sdir = 1, with_rod = true) {
  F = finger_params(name);
  l1 = F[0]; link = F[2]; rod = F[3];
  B = rot2([l1, 0], t);
  C = circ(B, norm(C_OFF), G_OFF, link, B + rot2(C_OFF, 1.9 * t));
  tm = ang(C_OFF) - ang(C - B);
  color("white") rotate([t, 0, 0]) proximal(name);
  color("gainsboro") translate([0, B[1], B[0]]) rotate([tm, 0, 0]) distal(name);
  color("orange") translate([LINK_X0, G_OFF[1], G_OFF[0]]) rotate([-ang(C - G_OFF), 0, 0]) bar(link, LINK_T);
  if (with_rod && F[4] != "direct") {
    S = [SERVO_Z, (F[4] == "dorsal" ? 1 : -1) * LAYER_Y];
    D = rot2(polar(D_R, F[5]), t);
    H = circ(S, HORN_R, D, rod, S + polar(HORN_R, F[6] - 1.26 * t));
    color("orange") translate([-ROD_T / 2, H[1], H[0]]) rotate([-ang(D - H), 0, 0]) bar(rod, ROD_T);
    color("dimgray") translate([0, S[1], S[0]]) rotate([-ang(H - S), 0, 0])
      translate([-sdir * (ROD_T / 2 + 1.1) - 1, 0, 0]) side(0, 2) hull() { disc([0, 0], 3.5); disc([HORN_R, 0], 2.2); }
  }
}

module servo_mock() {   // 舵机坐标：底面中心在原点，输出轴沿 +X，长度沿 Z（轴在 +Z 一侧）
  color("royalblue") {
    translate([0, -SV_W / 2, -SV_SH - SV_L / 2]) cube([SV_H, SV_W, SV_L]);
    translate([SV_FZ, -SV_W / 2, -SV_SH - SV_FL / 2]) cube([SV_FT, SV_W, SV_FL]);
    translate([SV_H, 0, 0]) rotate([0, 90, 0]) cylinder(r = SV_BOSS_R, h = 4.5);
  }
}

module hand() {
  t = close * THETA_MAX;
  names = ["index", "middle", "ring", "pinky"];
  if (show_palm) {
    color("lightsteelblue") palm();
    color("lightsteelblue", 0.6) translate([0, PALM_Y, 0]) cover_dorsal();
    color("lightsteelblue", 0.6) translate([0, -PALM_Y, 0]) mirror([0, 1, 0]) cover_palmar();
    color("lightsteelblue", 0.6) translate([0, -PALM_Y, 0]) mirror([0, 1, 0]) mirror([1, 0, 0]) cover_palmar();
    color("lightsteelblue", 0.6) translate([0, THENAR_Y[0], 0]) mirror([0, 1, 0]) thenar_cover();
  }
  for (i = [0 : 3]) {
    translate([FINGER_X[i], 0, 0]) finger_pose(names[i], t, SDIR[i]);
    translate([xb(i), yl(i), SERVO_Z]) mirror([SDIR[i] < 0 ? 1 : 0, 0, 0]) servo_mock();
  }
  // 拇指转动舵机
  translate([THUMB_A[0], THUMB_A[1], THUMB_HUB_Z - 6.5 - SV_H]) rotate([0, -90, 0]) servo_mock();
  // 拇指支架 + 弯曲舵机 + 拇指
  translate([THUMB_A[0], THUMB_A[1], THUMB_HUB_Z]) rotate([0, 0, psi]) {
    color("khaki") thumb_bracket();
    translate([THUMB_XBOT, 0, THUMB_ZT]) mirror([1, 0, 0]) servo_mock();
    translate([-R_T, 0, THUMB_ZT]) mirror([1, 0, 0]) finger_pose("thumb", tflex, 1, false);
  }
}

if (render_hand) hand();
