/*
 * Claude — "Ignite Every Idea"
 * A 30-second motion-graphics film. Every frame is a pure function of time:
 * window.renderAt(t) draws the frame at t seconds, so the renderer can capture
 * frames in any order (and in parallel) and always get the same picture.
 * The soundtrack (tools/score.py) is written on the same 120 BPM grid.
 */
'use strict';
(() => {
  const W = 1920, H = 1080, CX = W / 2, CY = H / 2, DURATION = 30, FPS = 60;
  const TAU = Math.PI * 2, PI = Math.PI;
  const RENDER = /[?&]render=1/.test(location.search);
  // device pixels per CSS pixel: 2 renders the 1920x1080 layout natively at 3840x2160
  const DPR = parseFloat(new URLSearchParams(location.search).get('dpr')) || 1;
  const BLUR = DPR; // canvas filter blur ignores the transform, so scale it by hand

  // ------------------------------------------------------------------ utils
  const clamp = (x, a = 0, b = 1) => (x < a ? a : x > b ? b : x);
  const lerp = (a, b, t) => a + (b - a) * t;
  const inv = (a, b, x) => clamp((x - a) / (b - a));
  const E = {
    outCubic: t => 1 - (1 - t) ** 3,
    inCubic: t => t * t * t,
    inOutCubic: t => (t < 0.5 ? 4 * t * t * t : 1 - (-2 * t + 2) ** 3 / 2),
    outQuint: t => 1 - (1 - t) ** 5,
    outExpo: t => (t >= 1 ? 1 : 1 - 2 ** (-10 * t)),
    inExpo: t => (t <= 0 ? 0 : 2 ** (10 * t - 10)),
    inOutSine: t => -(Math.cos(PI * t) - 1) / 2,
    outBack: (t, s = 1.70158) => 1 + (s + 1) * (t - 1) ** 3 + s * (t - 1) ** 2,
  };
  function rng(seed) {
    let a = seed >>> 0;
    return () => {
      a = (a + 0x6d2b79f5) | 0;
      let t = Math.imul(a ^ (a >>> 15), 1 | a);
      t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
  }
  function hash1(n) {
    n = Math.imul(n ^ 0x9e3779b9, 0x85ebca6b);
    n ^= n >>> 13; n = Math.imul(n, 0xc2b2ae35); n ^= n >>> 16;
    return (n >>> 0) / 4294967296;
  }
  const hash2 = (x, y) => hash1((Math.imul(x, 374761393) + Math.imul(y, 668265263)) | 0);
  function vnoise(x, y) {
    const ix = Math.floor(x), iy = Math.floor(y), fx = x - ix, fy = y - iy;
    const u = fx * fx * (3 - 2 * fx), v = fy * fy * (3 - 2 * fy);
    return lerp(lerp(hash2(ix, iy), hash2(ix + 1, iy), u), lerp(hash2(ix, iy + 1), hash2(ix + 1, iy + 1), u), v);
  }
  const fbm = (x, y) => 0.58 * vnoise(x, y) + 0.29 * vnoise(x * 2.03 + 5.2, y * 2.03 + 1.3) + 0.13 * vnoise(x * 4.1 + 9.1, y * 4.1 + 7.7);

  const COL = {
    ink: [243, 237, 228], orange: [217, 119, 87], orange2: [235, 154, 120], amber: [227, 169, 107],
    deep: [168, 78, 52], sand: [201, 168, 128], blue: [132, 170, 210], green: [156, 180, 124], white: [255, 246, 236],
  };
  const rgba = (c, a) => `rgba(${c[0]},${c[1]},${c[2]},${a})`;

  // ------------------------------------------------------------------ DOM
  const stage = document.getElementById('stage');
  const cam = document.getElementById('cam');
  const cv = document.getElementById('c'), g = cv.getContext('2d');
  const fxc = document.getElementById('fx'), fg = fxc.getContext('2d');
  const grc = document.getElementById('grain'), gg = grc.getContext('2d');
  const dom = document.getElementById('dom');
  for (const c of [cv, fxc, grc]) { c.width = W * DPR; c.height = H * DPR; }
  const hudRoot = document.getElementById('hud');
  let SA = 1; // alpha multiplier of the scene currently drawing on the canvas

  function mk(tag, cls, parent, html) {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (html != null) e.innerHTML = html;
    (parent || dom).appendChild(e);
    return e;
  }
  // Split text into per-character spans; chars whose index is in `hi` get the accent colour.
  function split(el, text, hi) {
    el.textContent = '';
    const out = [];
    let i = 0;
    for (const ch of text) {
      const s = document.createElement('span');
      s.className = 'ch' + (hi && i >= hi[0] && i < hi[1] ? ' o' : '');
      s.textContent = ch;
      el.appendChild(s);
      out.push(s);
      i++;
    }
    return out;
  }
  function css(e, a, x = 0, y = 0, sc = 1, b = 0, extra = '') {
    if (a <= 0.002) {
      if (e._v !== 0) { e.style.visibility = 'hidden'; e._v = 0; }
      return;
    }
    if (e._v !== 1) { e.style.visibility = 'visible'; e._v = 1; }
    e.style.opacity = a >= 0.999 ? '1' : a.toFixed(3);
    e.style.transform = `translate3d(${x.toFixed(2)}px,${y.toFixed(2)}px,0)` + (sc !== 1 ? ` scale(${sc.toFixed(4)})` : '') + extra;
    e.style.filter = b > 0.08 ? `blur(${b.toFixed(2)}px)` : 'none';
  }
  // Staggered per-character entrance (and optional exit).
  function anim(sp, t, o) {
    const n = sp.length, st = o.st ?? 0.03, dur = o.dur ?? 0.6, ease = o.ease || E.outCubic;
    for (let i = 0; i < n; i++) {
      const t0 = o.t0 + i * st;
      const p = ease(inv(t0, t0 + dur, t));
      let a = clamp(p * 1.4), x = (1 - p) * (o.dx || 0), y = (1 - p) * (o.dy ?? 20), b = (1 - p) * (o.blur ?? 10);
      const sc = 1 - (1 - p) * (o.sc || 0);
      if (o.out != null) {
        const t1 = o.out + i * (o.ost ?? 0.012);
        const q = E.inCubic(inv(t1, t1 + (o.odur ?? 0.3), t));
        a *= 1 - q; y -= q * (o.ody ?? 26); b += q * (o.oblur ?? 12);
      }
      css(sp[i], a, x, y, sc, b);
    }
  }
  function absPos(el) {
    let x = 0, y = 0, e = el;
    while (e && e !== stage) { x += e.offsetLeft; y += e.offsetTop; e = e.offsetParent; }
    return [x, y];
  }
  function sceneCss(root, a, sc = 1, b = 0, dx = 0, dy = 0) {
    if (a <= 0.002) {
      if (root._d !== 0) { root.style.display = 'none'; root._d = 0; }
      return;
    }
    if (root._d !== 1) { root.style.display = 'block'; root._d = 1; }
    root.style.opacity = a >= 0.999 ? '1' : a.toFixed(3);
    root.style.transform = `translate(${dx.toFixed(2)}px,${dy.toFixed(2)}px) scale(${sc.toFixed(4)})`;
    root.style.filter = b > 0.08 ? `blur(${b.toFixed(2)}px)` : 'none';
  }

  // ------------------------------------------------------------------ sprites
  function glowSprite(c, size = 256, hard = false) {
    const s = document.createElement('canvas');
    s.width = s.height = size;
    const x = s.getContext('2d'), r = size / 2;
    const gr = x.createRadialGradient(r, r, 0, r, r, r);
    if (hard) {
      gr.addColorStop(0, rgba(c, 1)); gr.addColorStop(0.12, rgba(c, 0.9)); gr.addColorStop(0.3, rgba(c, 0.28));
      gr.addColorStop(0.6, rgba(c, 0.06)); gr.addColorStop(1, rgba(c, 0));
    } else {
      gr.addColorStop(0, rgba(c, 1)); gr.addColorStop(0.3, rgba(c, 0.5)); gr.addColorStop(0.62, rgba(c, 0.14));
      gr.addColorStop(1, rgba(c, 0));
    }
    x.fillStyle = gr;
    x.fillRect(0, 0, size, size);
    return s;
  }
  const SPR = {
    orange: glowSprite(COL.orange, 128, true), white: glowSprite(COL.white, 128, true), ink: glowSprite(COL.ink, 128, true),
    amber: glowSprite(COL.amber, 128, true), blue: glowSprite(COL.blue, 128, true),
    fogO: glowSprite(COL.orange, 256), fogD: glowSprite(COL.deep, 256), fogA: glowSprite(COL.amber, 256), fogB: glowSprite(COL.blue, 256),
  };
  function glow(spr, x, y, r, a, ctx = g) {
    if (a <= 0.003 || r <= 0.2) return;
    ctx.globalAlpha = Math.min(1, a);
    ctx.drawImage(spr, x - r, y - r, r * 2, r * 2);
  }

  // ------------------------------------------------------------------ the spark
  const RAYS = (() => {
    const r = rng(7), n = 11, out = [];
    for (let i = 0; i < n; i++) {
      out.push({ a: (i / n) * TAU + (r() - 0.5) * 0.2, l: 0.66 + r() * 0.34, w: 0.2 + r() * 0.04, d: r() * 0.16 });
    }
    return out;
  })();
  const sparkCv = document.createElement('canvas');
  const SPK = 1040, SPR_R = 400; // offscreen spark canvas, ray length in its pixels
  sparkCv.width = sparkCv.height = SPK;
  const sg = sparkCv.getContext('2d');
  function sparkPath(ctx, R, rot, grow, color) {
    ctx.save();
    ctx.rotate(rot);
    ctx.fillStyle = color;
    for (const ray of RAYS) {
      const p = grow == null ? 1 : E.outBack(clamp((grow - ray.d) / 0.55), 2.2);
      if (p <= 0) continue;
      const L = R * ray.l * p, w0 = R * ray.w, w1 = R * ray.w * 0.8;
      ctx.save();
      ctx.rotate(ray.a);
      ctx.beginPath();
      ctx.moveTo(0, -w0 / 2);
      ctx.lineTo(L, -w1 / 2);
      ctx.arc(L, 0, w1 / 2, -PI / 2, PI / 2);
      ctx.lineTo(0, w0 / 2);
      ctx.arc(0, 0, w0 / 2, PI / 2, -PI / 2);
      ctx.closePath();
      ctx.fill();
      ctx.restore();
    }
    ctx.restore();
  }
  function drawSpark(x, y, R, rot, grow, alpha = 1, glowAmt = 0.5, color = '#d97757') {
    sg.setTransform(1, 0, 0, 1, 0, 0);
    sg.clearRect(0, 0, SPK, SPK);
    sg.translate(SPK / 2, SPK / 2);
    sparkPath(sg, SPR_R, rot, grow, color);
    const s = R / SPR_R, S = SPK * s;
    g.save();
    if (glowAmt > 0) {
      g.globalCompositeOperation = 'lighter';
      g.filter = `blur(${(Math.max(5, 60 * s) * BLUR).toFixed(1)}px)`;
      g.globalAlpha = alpha * glowAmt * SA;
      g.drawImage(sparkCv, x - S / 2, y - S / 2, S, S);
      g.filter = 'none';
      g.globalCompositeOperation = 'source-over';
    }
    g.globalAlpha = alpha * SA;
    g.drawImage(sparkCv, x - S / 2, y - S / 2, S, S);
    g.restore();
  }
  function ring(x, y, r, w, a, c = COL.orange2) {
    if (a <= 0.003 || w <= 0.05) return;
    g.globalAlpha = a * SA;
    g.strokeStyle = rgba(c, 1);
    g.lineWidth = w;
    g.beginPath();
    g.arc(x, y, r, 0, TAU);
    g.stroke();
  }

  // ------------------------------------------------------------------ timeline
  const CUTS = [8, 12, 16, 20, 24];
  const SECTIONS = [
    ['01', 'REASON', '推理', 4, 8], ['02', 'CODE', '编程', 8, 12], ['03', 'MATH', '数学', 12, 16],
    ['04', 'LANGUAGE', '语言', 16, 20], ['05', 'CREATE', '创造', 20, 24], ['06', 'AGENTS', '智能体', 24, 26],
  ];
  // Envelope of a scene: fade/scale in around t0, zoom-through out around t1.
  function env(t, t0, t1, fin = 0.32, fout = 0.3) {
    const a = t0 <= 0 ? 1 : E.outCubic(inv(t0 - 0.06, t0 - 0.06 + fin, t));
    const b = E.inCubic(inv(t1 - fout, t1 + 0.02, t));
    return { a: a * (1 - b), inP: a, outP: b };
  }
  function withScene(e, fn, zin = 0.06, zout = 0.14) {
    if (e.a <= 0.002) return;
    const sc = (1 - zin * (1 - e.inP)) * (1 + zout * e.outP);
    g.save();
    g.translate(CX, CY); g.scale(sc, sc); g.translate(-CX, -CY);
    SA = e.a;
    fn(sc);
    SA = 1;
    g.restore();
    g.globalAlpha = 1;
    g.globalCompositeOperation = 'source-over';
  }

  // ------------------------------------------------------------------ title block
  function makeTitle(parent, label, word, cn) {
    const root = mk('div', 'title', parent);
    const l = mk('div', 't-label', root), w = mk('div', 't-word', root), c = mk('div', 't-cn', root), r = mk('div', 't-rule', root);
    return { root, l: split(l, label), w: split(w, word, [word.length - 1, word.length]), c: split(c, cn), r };
  }
  function titleAnim(T, t, tin, tout) {
    anim(T.l, t, { t0: tin, st: 0.018, dur: 0.4, dy: 0, dx: -14, blur: 6, out: tout, ost: 0.004, odur: 0.2 });
    anim(T.w, t, { t0: tin + 0.06, st: 0.05, dur: 0.85, dy: 80, blur: 18, ease: E.outQuint, out: tout, ost: 0.012, odur: 0.25, ody: 40 });
    anim(T.c, t, { t0: tin + 0.3, st: 0.04, dur: 0.6, dy: 22, blur: 8, out: tout + 0.03, ost: 0.008, odur: 0.22 });
    const p = E.outExpo(inv(tin + 0.2, tin + 1.1, t)) * (1 - E.inCubic(inv(tout, tout + 0.25, t)));
    T.r.style.transform = `scaleX(${p.toFixed(4)})`;
    T.r.style.opacity = p > 0.001 ? '1' : '0';
  }

  // ================================================================== SCENES
  const scenes = [];

  // ---------------------------------------------------------------- 0 · intro
  const intro = (() => {
    const S = {};
    const P = [];
    const r = rng(11);
    const cols = ['#d97757', '#eb9a78', '#f3ede4', '#e3a96b', '#f3ede4', '#d97757'];
    for (let i = 0; i < 1500; i++) {
      P.push({
        a: r() * TAU, r0: 160 + r() ** 0.75 * 1250, w: 0.05 + r() * 0.13, s: 0.7 + r() ** 3 * 2.6,
        c: cols[(r() * cols.length) | 0], tw: r() * TAU, tws: 1 + r() * 3, z: r(), sp: 2.5 + r() * 3,
      });
    }
    const tilt = -0.2, ct = Math.cos(tilt), st = Math.sin(tilt);
    function pos(p, t) {
      const q = clamp(t / 3.97);
      const k = 1 - (0.2 * q + 0.8 * E.inExpo(q));
      const rr = p.r0 * k;
      const ang = p.a + p.w * t + p.sp * E.inCubic(q);
      const x = Math.cos(ang) * rr, y = Math.sin(ang) * rr * 0.46;
      return [CX + x * ct - y * st, CY + x * st + y * ct];
    }
    S.init = () => {
      S.root = mk('div', 'scene');
      const l1 = mk('div', 'intro-en', S.root); l1.style.top = '300px';
      const l2 = mk('div', 'intro-en', S.root); l2.style.top = '398px';
      const l3 = mk('div', 'intro-cn', S.root); l3.style.top = '648px';
      S.c1 = split(l1, 'Every great idea');
      S.c2 = split(l2, 'begins with a spark.', [14, 19]);
      S.c3 = split(l3, '每一个伟大的想法，都始于一个火花。');
      for (let i = 14; i < 19; i++) S.c2[i].style.textShadow = '0 0 34px rgba(217,119,87,.75)';
      S.all = [...S.c1, ...S.c2, ...S.c3];
    };
    S.layout = () => {
      for (const s of S.all) {
        const [x, y] = absPos(s);
        s._cx = x + s.offsetWidth / 2; s._cy = y + s.offsetHeight / 2;
        s._d = Math.hypot(s._cx - CX, (s._cy - CY) * 1.6) / 900;
      }
    };
    S.draw = t => {
      if (t > 4.2) return;
      const q = clamp(t / 3.97);
      g.globalCompositeOperation = 'lighter';
      g.lineCap = 'round';
      const fade = E.outCubic(inv(0, 1.6, t));
      for (const p of P) {
        const [x, y] = pos(p, t);
        const [px, py] = pos(p, t - 0.045);
        const tw = 0.55 + 0.45 * Math.sin(p.tw + p.tws * t);
        const a = fade * tw * (0.22 + 0.78 * p.z) * (0.45 + 0.55 * q) * (1 - inv(3.93, 4.0, t));
        if (a < 0.01) continue;
        g.globalAlpha = a * SA;
        g.strokeStyle = p.c;
        g.lineWidth = p.s;
        g.beginPath(); g.moveTo(px, py); g.lineTo(x + 0.01, y); g.stroke();
      }
      const e = E.inExpo(q), br = 0.5 + 0.5 * Math.sin(t * TAU * 0.5);
      glow(SPR.fogO, CX, CY, 260 + 60 * br + 900 * e, (0.18 + 0.5 * q) * SA);
      glow(SPR.orange, CX, CY, 40 + 14 * br + 260 * e, (0.5 + 0.5 * q) * SA * fade);
      glow(SPR.white, CX, CY, 9 + 4 * br + 70 * e, (0.7 + 0.3 * q) * SA * fade);
      g.globalCompositeOperation = 'source-over';
    };
    S.dom = t => {
      const on = t < 4.05;
      sceneCss(S.root, on ? 1 : 0);
      if (!on) return;
      const tin = [0.5, 1.2, 1.75];
      const groups = [S.c1, S.c2, S.c3];
      groups.forEach((sp, gi) => {
        sp.forEach((s, i) => {
          const t0 = tin[gi] + i * (gi === 2 ? 0.032 : 0.04);
          const p = E.outCubic(inv(t0, t0 + 0.95, t));
          let a = clamp(p * 1.3), x = 0, y = (1 - p) * (gi === 2 ? 16 : 34), b = (1 - p) * 14, sc = 1;
          // pulled into the spark
          const k = E.inCubic(inv(3.02 + (1 - s._d) * 0.28, 3.86, t));
          if (k > 0) {
            x += (CX - s._cx) * k; y += (CY - s._cy) * k;
            sc = 1 - 0.85 * k; a *= 1 - k * 0.9; b += 5 * k;
          }
          css(s, a, x, y, sc, b);
        });
      });
    };
    return S;
  })();
  scenes.push(intro);

  // ---------------------------------------------------------------- 1 · reason
  const reason = (() => {
    const S = {};
    const NV = 1500, VS = 1150, WAVES = [4.5, 5.0, 5.5, 6.0, 6.5, 7.0, 7.5];
    const net = (() => {
      const r = rng(21), pts = [];
      for (let i = 0; i < 240; i++) {
        let best = null, bd = -1;
        for (let k = 0; k < 16; k++) {
          const a = r() * TAU, rr = Math.sqrt(r());
          const x = CX + Math.cos(a) * rr * 1120, y = CY + Math.sin(a) * rr * 660;
          if (Math.hypot(x - CX, (y - CY) * 1.5) < 150) continue;
          let md = Math.hypot(x - CX, y - CY) ** 2;
          for (const p of pts) md = Math.min(md, (p.x - x) ** 2 + (p.y - y) ** 2);
          if (md > bd) { bd = md; best = { x, y }; }
        }
        if (best) { best.z = 0.3 + r() * 0.7; pts.push(best); }
      }
      pts.sort((a, b) => Math.hypot(a.x - CX, a.y - CY) - Math.hypot(b.x - CX, b.y - CY));
      const nodes = [{ x: CX, y: CY, z: 1, D: 0, root: true }, ...pts];
      const edges = [];
      for (let i = 1; i < nodes.length; i++) {
        const n = nodes[i], dn = Math.hypot(n.x - CX, n.y - CY);
        let bj = 0, bd = (n.x - CX) ** 2 + (n.y - CY) ** 2;
        for (let j = 1; j < i; j++) {
          const m = nodes[j];
          if (Math.hypot(m.x - CX, m.y - CY) >= dn - 20) continue;
          const d = (m.x - n.x) ** 2 + (m.y - n.y) ** 2;
          if (d < bd) { bd = d; bj = j; }
        }
        const L = Math.sqrt(bd);
        n.D = nodes[bj].D + L;
        edges.push({ a: bj, b: i, L, tree: true });
      }
      for (let i = 1; i < nodes.length; i++) {
        const n = nodes[i];
        let bj = -1, bd = 175 * 175;
        for (let j = 1; j < nodes.length; j++) {
          if (j === i) continue;
          const d = (nodes[j].x - n.x) ** 2 + (nodes[j].y - n.y) ** 2;
          if (d < bd && !edges.some(e => (e.a === i && e.b === j) || (e.a === j && e.b === i))) { bd = d; bj = j; }
        }
        if (bj > 0 && r() < 0.6) edges.push({ a: i, b: bj, L: Math.sqrt(bd), tree: false });
      }
      const words = ['hypothesis', 'evidence', 'context', 'analogy', 'constraint', 'insight', 'proof', 'edge case',
        'what if…', '∴ therefore', '假设', '证据', '洞见', '推演', '因果', 'abstraction', '反例', 'first principles'];
      const cand = nodes.map((n, i) => ({ n, i })).filter(({ n }) => !n.root && n.x > 900 && n.x < 1800 && n.y > 130 && n.y < 960 && n.D > 200);
      const lab = [];
      for (const w of words) {
        let best = null, bs = -1;
        for (const c of cand) {
          if (c.n.label) continue;
          let md = 1e9;
          for (const l of lab) md = Math.min(md, Math.hypot(l.x - c.n.x, (l.y - c.n.y) * 1.8));
          const s = md + r() * 60;
          if (s > bs) { bs = s; best = c; }
        }
        if (best) { best.n.label = w; lab.push(best.n); }
      }
      return { nodes, edges, maxD: Math.max(...nodes.map(n => n.D)) };
    })();
    const mask = (x, y) => 0.18 + 0.82 * clamp((x - 360) / 620) * (1 - 0.0 * y);
    S.init = () => {
      S.root = mk('div', 'scene');
      const th = mk('div', 'think', S.root);
      S.head = mk('div', 'think-h', th);
      S.rows = [
        ['Understanding the question', '理解问题'], ['Exploring every approach', '探索多种思路'],
        ['Verifying each step', '逐步验证'], ['Arriving at the answer', '得出答案'],
      ].map(([en, cn], i) => {
        const row = mk('div', 'row', th);
        const m = mk('div', 'mk', row);
        const spin = mk('div', 'spin', m), chk = mk('div', 'chk', m, '✓');
        const e = split(mk('span', 'en', row), en), c = split(mk('span', 'cn', row), cn);
        return { row, spin, chk, e, c, t0: 4.45 + i * 0.68 };
      });
      S.T = makeTitle(S.root, '01 — REASONING', 'Reason.', '深度思考，层层推理');
    };
    S.draw = t => {
      const lt = t - 4;
      if (lt < 0) return;
      const { nodes, edges } = net;
      g.lineCap = 'round';
      // edges
      g.globalCompositeOperation = 'source-over';
      g.strokeStyle = rgba(COL.ink, 1);
      g.lineWidth = 1.1;
      for (const e of edges) {
        const A = nodes[e.a], B = nodes[e.b];
        const ta = 4 + A.D / NV, tb = 4 + B.D / NV;
        const f = e.tree ? inv(ta, tb, t) : inv(Math.max(ta, tb), Math.max(ta, tb) + 0.3, t);
        if (f <= 0) continue;
        const a = (e.tree ? 0.2 : 0.09) * ((A.z + B.z) / 2) * mask((A.x + B.x) / 2, 0);
        g.globalAlpha = a * SA;
        g.beginPath(); g.moveTo(A.x, A.y); g.lineTo(lerp(A.x, B.x, f), lerp(A.y, B.y, f)); g.stroke();
      }
      // travelling pulses
      g.globalCompositeOperation = 'lighter';
      g.lineWidth = 2.2;
      for (const b of WAVES) {
        const dt = t - b;
        if (dt < 0 || dt > net.maxD / VS + 0.4) continue;
        for (const e of edges) {
          if (!e.tree) continue;
          const A = nodes[e.a], B = nodes[e.b];
          const f = (dt - A.D / VS) / (e.L / VS);
          if (f < 0 || f > 1.25) continue;
          const m = mask(A.x, 0);
          const f1 = Math.min(1, f), f0 = Math.max(0, f - 0.45);
          g.globalAlpha = 0.55 * m * SA;
          g.strokeStyle = rgba(COL.orange2, 1);
          g.beginPath(); g.moveTo(lerp(A.x, B.x, f0), lerp(A.y, B.y, f0)); g.lineTo(lerp(A.x, B.x, f1), lerp(A.y, B.y, f1)); g.stroke();
          if (f <= 1) glow(SPR.orange, lerp(A.x, B.x, f), lerp(A.y, B.y, f), 16, 0.9 * m * SA);
        }
      }
      // nodes
      for (const n of nodes) {
        if (n.root) continue;
        const ta = 4 + n.D / NV;
        if (t < ta) continue;
        const p = E.outBack(inv(ta, ta + 0.32, t), 2.5);
        let fl = Math.exp(-(t - ta) * 5);
        for (const b of WAVES) {
          const arr = b + n.D / VS;
          if (t >= arr) fl = Math.max(fl, Math.exp(-(t - arr) * 5.5));
        }
        const m = mask(n.x, n.y);
        g.globalCompositeOperation = 'source-over';
        g.globalAlpha = (0.35 + 0.55 * n.z) * m * SA;
        g.fillStyle = fl > 0.4 ? rgba(COL.orange2, 1) : rgba(COL.ink, 1);
        g.beginPath(); g.arc(n.x, n.y, Math.max(0.1, (1.5 + 2.4 * n.z) * p), 0, TAU); g.fill();
        g.globalCompositeOperation = 'lighter';
        glow(SPR.orange, n.x, n.y, 10 + 36 * fl * n.z, fl * m * SA * 0.9);
        if (n.label) {
          const la = inv(ta + 0.15, ta + 0.5, t) * m;
          g.globalCompositeOperation = 'source-over';
          g.font = /[一-鿿]/.test(n.label) ? '400 17px "Noto Sans SC"' : '400 16px "JetBrains Mono"';
          g.fillStyle = fl > 0.25 ? rgba(COL.orange2, 1) : rgba(COL.ink, 1);
          g.globalAlpha = la * (0.4 + 0.5 * fl) * SA;
          g.fillText(n.label, n.x + 10, n.y - 9);
        }
      }
      g.globalCompositeOperation = 'lighter';
      // bloom of the spark + shockwaves
      ring(CX, CY, 60 + 1500 * E.outExpo(clamp(lt / 1.1)), 5 * (1 - clamp(lt / 1.1)), 0.8 * (1 - clamp(lt / 1.1)) ** 2);
      ring(CX, CY, 40 + 900 * E.outExpo(clamp((lt - 0.06) / 1.3)), 2.5 * (1 - clamp(lt / 1.3)), 0.5 * (1 - clamp(lt / 1.3)) ** 2, COL.ink);
      glow(SPR.fogO, CX, CY, 420 + 40 * Math.sin(t * TAU), 0.28 * SA);
      let beat = 0;
      for (const b of WAVES) if (t >= b) beat = Math.max(beat, Math.exp(-(t - b) * 7));
      glow(SPR.orange, CX, CY, 150 + 50 * beat, (0.35 + 0.35 * beat) * SA);
      g.globalCompositeOperation = 'source-over';
      drawSpark(CX, CY, 88 * (1 + 0.05 * beat), 0.15 * lt, lt, 1, 0.5 + 0.3 * beat);
    };
    S.dom = (t, e) => {
      sceneCss(S.root, e.a, 1 + 0.08 * e.outP, e.outP * 8);
      if (e.a <= 0.002) return;
      const done = 7.2;
      S.head.textContent = t < done ? 'THINKING ' + '·'.repeat(1 + (Math.floor(t * 4) % 3)) : 'THOUGHT FOR 2.8 S  ✓';
      css(S.head, inv(4.3, 4.6, t));
      S.rows.forEach((R, i) => {
        const t0 = R.t0, tDone = i < 3 ? S.rows[i + 1].t0 : done;
        const p = E.outCubic(inv(t0, t0 + 0.35, t));
        css(R.row, p, -24 * (1 - p), 0);
        anim(R.e, t, { t0: t0 + 0.02, st: 0.011, dur: 0.22, dy: 0, blur: 4 });
        anim(R.c, t, { t0: t0 + 0.2, st: 0.03, dur: 0.25, dy: 0, blur: 4 });
        const dn = E.outBack(inv(tDone, tDone + 0.25, t), 2.6);
        css(R.spin, 1 - inv(tDone, tDone + 0.1, t), 0, 0, 1, 0, ` rotate(${(t * 560) % 360}deg)`);
        css(R.chk, dn > 0 ? clamp(dn) : 0, 0, 0, Math.max(0.01, dn));
        R.e.forEach(s => (s.style.color = t > tDone ? 'rgba(243,237,228,.62)' : ''));
      });
      titleAnim(S.T, t, 4.35, 7.66);
    };
    return S;
  })();
  scenes.push(reason);

  // ---------------------------------------------------------------- 2 · code
  const code = (() => {
    const S = {};
    const SRC = `import asyncio
from claude import Agent

async def create(idea: str):
    """From a single spark to something real."""
    agent = Agent(tools=["code", "search", "test"])
    plan = await agent.think(idea, depth="deep")

    for step in plan.steps:
        result = await agent.run(step)
        assert result.verified  # 每一步都验证

    return await plan.ship()

asyncio.run(create("anything you can imagine"))`;
    // tiny Python highlighter -> [{ch, cls}]
    const toks = [];
    {
      const KW = new Set(['import', 'from', 'async', 'def', 'await', 'for', 'in', 'assert', 'return']);
      const re = /("""[\s\S]*?"""|"[^"]*")|(#.*)|([A-Za-z_][A-Za-z0-9_]*)|(\d+)|(\s+)|(.)/g;
      let m;
      while ((m = re.exec(SRC))) {
        let cls = '';
        if (m[1]) cls = 's'; else if (m[2]) cls = 'c';
        else if (m[3]) {
          if (KW.has(m[3])) cls = 'k';
          else if (SRC[re.lastIndex] === '(') cls = 'f';
          else if (m[3] === 'str') cls = 'd';
        } else if (m[4]) cls = 'n'; else if (m[6]) cls = 'p';
        for (const ch of m[0]) toks.push({ ch, cls });
      }
    }
    // per-character keystroke times: human-ish rhythm, compressed to super-human speed
    const times = [];
    {
      const r = rng(5);
      let acc = 0;
      for (const tk of toks) {
        acc += tk.ch === '\n' ? 2.4 : tk.ch === ' ' ? 0.35 : 0.55 + r() * 0.9;
        times.push(acc);
      }
      const t0 = 8.28, t1 = 10.5;
      for (let i = 0; i < times.length; i++) times[i] = t0 + (times[i] / acc) * (t1 - t0);
    }
    const esc = s => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
    function htmlUpTo(n) {
      let out = '', cur = null, buf = '';
      for (let i = 0; i < n; i++) {
        const tk = toks[i];
        if (tk.cls !== cur) {
          if (buf) out += cur ? `<span class="${cur}">${esc(buf)}</span>` : esc(buf);
          cur = tk.cls; buf = '';
        }
        buf += tk.ch;
      }
      if (buf) out += cur ? `<span class="${cur}">${esc(buf)}</span>` : esc(buf);
      return out;
    }
    const TERM = [
      { t: 10.0, d: 0.22, h: '<span class="dim">$</span> pytest -q' },
      { t: 10.3, d: 0.62, h: '<span class="ok">' + '.'.repeat(46) + '</span>', tail: ' <span class="dim">[100%]</span>' },
      { t: 11.0, d: 0.01, h: '<span class="ok">248 passed</span> <span class="dim">in 1.84s</span>' },
      { t: 11.12, d: 0.22, h: '<span class="dim">$</span> git push origin main' },
      { t: 11.48, d: 0.01, h: '<span class="o">✓</span> Deployed to production' },
    ];
    function termHTML(t) {
      const lines = [];
      for (const L of TERM) {
        if (t < L.t) break;
        const plain = L.h.replace(/<[^>]+>/g, '');
        const n = Math.floor(plain.length * inv(L.t, L.t + L.d, t));
        // reveal n visible chars of the markup
        let out = '', seen = 0, i = 0;
        while (i < L.h.length && seen < n) {
          if (L.h[i] === '<') { const j = L.h.indexOf('>', i); out += L.h.slice(i, j + 1); i = j + 1; continue; }
          out += L.h[i]; i++; seen++;
        }
        const open = (out.match(/<span/g) || []).length - (out.match(/<\/span>/g) || []).length;
        out += '</span>'.repeat(Math.max(0, open));
        if (n >= plain.length && L.tail) out += L.tail;
        lines.push(out);
      }
      return lines.join('\n');
    }
    S.init = () => {
      S.root = mk('div', 'scene');
      const persp = mk('div', 'persp', S.root);
      S.ed = mk('div', 'win editor', persp,
        `<div class="bar"><i></i><i></i><i></i><span class="tab on">create.py</span><span class="tab">plan.md</span><span class="tab">test_create.py</span></div>
         <div class="codebox"><div class="hl"></div><div class="gutter"></div><pre class="src"></pre></div>`);
      S.src = S.ed.querySelector('.src'); S.gut = S.ed.querySelector('.gutter'); S.hl = S.ed.querySelector('.hl');
      S.term = mk('div', 'win term', persp, `<div class="bar"><i></i><i></i><i></i><span class="tab on">terminal</span></div><pre></pre>`);
      S.tpre = S.term.querySelector('pre');
      S.chip1 = mk('div', 'chip', persp, '+ 14 lines · 0 errors');
      S.chip1.style.left = '1560px'; S.chip1.style.top = '118px';
      S.chip2 = mk('div', 'chip', persp, '✓ 248 / 248 tests');
      S.chip2.style.left = '1090px'; S.chip2.style.top = '605px';
      S.T = makeTitle(S.root, '02 — CODING', 'Code.', '编程，快如思绪');
    };
    S.draw = t => {
      const lt = t - 8;
      // dot grid drifting
      g.fillStyle = rgba(COL.ink, 1);
      const sp = 46, ox = (lt * 22) % sp, oy = (lt * 14) % sp;
      for (let x = -sp; x < W + sp; x += sp) {
        for (let y = -sp; y < H + sp; y += sp) {
          const px = x - ox, py = y - oy;
          const d = Math.hypot((px - 1250) / 1200, (py - 480) / 700);
          const a = 0.13 * (1 - clamp(d));
          if (a < 0.01) continue;
          g.globalAlpha = a * SA;
          g.fillRect(px, py, 2, 2);
        }
      }
      g.globalCompositeOperation = 'lighter';
      glow(SPR.fogO, 1300, 470, 820, 0.2 * SA);
      glow(SPR.fogB, 700, 760, 700, 0.1 * SA);
      g.globalCompositeOperation = 'source-over';
    };
    S.dom = (t, e) => {
      sceneCss(S.root, e.a, (1 - 0.05 * (1 - e.inP)) * (1 + 0.1 * e.outP), e.outP * 10 + (1 - e.inP) * 6);
      if (e.a <= 0.002) return;
      const lt = t - 8;
      // editor
      const pe = E.outQuint(inv(7.98, 8.7, t));
      const ry = lerp(-27, -12, E.outCubic(inv(8, 12, t))), rx = lerp(8, 3, E.outCubic(inv(8, 12, t)));
      css(S.ed, clamp(pe * 1.2), 0, 60 * (1 - pe), 1, (1 - pe) * 6,
        ` translateZ(${(-260 * (1 - pe)).toFixed(1)}px) rotateY(${ry.toFixed(2)}deg) rotateX(${rx.toFixed(2)}deg)`);
      let n = 0;
      while (n < times.length && times[n] <= t) n++;
      const typed = toks.slice(0, n).map(x => x.ch).join('');
      const line = (typed.match(/\n/g) || []).length;
      const typing = n < times.length && t > 8.25;
      const blink = typing || Math.floor(t * 2.4) % 2 === 0;
      S.src.innerHTML = htmlUpTo(n) + (t > 8.2 && blink ? '<span class="caret"></span>' : '');
      S.gut.textContent = Array.from({ length: Math.max(1, line + 1) }, (_, i) => i + 1).join('\n');
      S.hl.style.top = `${26 + line * 34}px`;
      S.hl.style.opacity = t > 8.25 ? '1' : '0';
      // terminal
      const pt = E.outQuint(inv(9.82, 10.3, t));
      css(S.term, clamp(pt * 1.3), 0, 80 * (1 - pt), 1, (1 - pt) * 5,
        ` translateZ(${(120 * pt).toFixed(1)}px) rotateY(${(ry * 0.55).toFixed(2)}deg) rotateX(${rx.toFixed(2)}deg)`);
      S.tpre.innerHTML = termHTML(t);
      const c1 = E.outBack(inv(10.55, 10.85, t), 2), c2 = E.outBack(inv(11.02, 11.32, t), 2);
      css(S.chip1, clamp(c1), 0, 12 * (1 - c1), Math.max(0.01, 0.8 + 0.2 * c1));
      css(S.chip2, clamp(c2), 0, 12 * (1 - c2), Math.max(0.01, 0.8 + 0.2 * c2), 0, ` translateZ(160px)`);
      titleAnim(S.T, t, 8.3, 11.66);
      void lt;
    };
    return S;
  })();
  scenes.push(code);

  // ---------------------------------------------------------------- 3 · math
  const math = (() => {
    const S = {};
    const F = [
      { tex: 'e^{i\\pi} + 1 = 0', x: 960, y: 176, size: 78, z: 1, t: 12.12 },
      { tex: 'f(x) = \\frac{4}{\\pi} \\sum_{k=1}^{\\infty} \\frac{\\sin\\big((2k-1)\\,x\\big)}{2k-1}', x: 1290, y: 778, size: 30, z: 0.95, t: 12.55 },
      { tex: '\\int_{-\\infty}^{\\infty} e^{-x^{2}}\\,dx = \\sqrt{\\pi}', x: 318, y: 138, size: 28, z: 0.62, t: 12.35 },
      { tex: '\\sum_{n=1}^{\\infty} \\frac{1}{n^{2}} = \\frac{\\pi^{2}}{6}', x: 1610, y: 140, size: 28, z: 0.62, t: 12.5 },
      { tex: 'P(A \\mid B) = \\frac{P(B \\mid A)\\,P(A)}{P(B)}', x: 470, y: 262, size: 23, z: 0.5, t: 12.75 },
      { tex: 'i\\hbar\\,\\frac{\\partial}{\\partial t}\\Psi = \\hat{H}\\Psi', x: 950, y: 956, size: 23, z: 0.45, t: 12.9 },
      { tex: '\\nabla \\times \\mathbf{B} = \\mu_0\\mathbf{J} + \\mu_0\\varepsilon_0\\frac{\\partial \\mathbf{E}}{\\partial t}', x: 1325, y: 956, size: 22, z: 0.42, t: 13.05 },
      { tex: 'R(\\theta) = \\begin{pmatrix} \\cos\\theta & -\\sin\\theta \\\\ \\sin\\theta & \\cos\\theta \\end{pmatrix}', x: 1690, y: 952, size: 21, z: 0.42, t: 13.2 },
    ];
    const NT = [1, 2, 3, 5, 8, 13, 21, 34];
    const termT = [];
    for (let k = 1; k <= 34; k++) {
      const j = NT.findIndex(v => v >= k);
      termT.push(12.0 + 0.5 * j);
    }
    const EP = { x: 470, y: 505, A: 125 }, WX0 = 790, WX1 = 1790;
    S.init = () => {
      S.root = mk('div', 'scene');
      S.els = F.map(f => {
        const el = mk('div', 'fx', S.root);
        el.style.fontSize = f.size + 'px';
        katex.render(f.tex, el, { displayMode: true, throwOnError: false, output: 'html' });
        return el;
      });
      S.nl = mk('div', 'nlabel', S.root);
      S.nl.style.left = WX0 + 'px'; S.nl.style.top = '318px';
      S.T = makeTitle(S.root, '03 — MATHEMATICS', 'Mathematics.', '数学，严谨而优雅');
      S.T.w[0].parentNode.style.fontSize = '124px';
    };
    S.layout = () => {
      S.els.forEach((el, i) => {
        const f = F[i];
        el.style.left = (f.x - el.offsetWidth / 2).toFixed(1) + 'px';
        el.style.top = (f.y - el.offsetHeight / 2).toFixed(1) + 'px';
      });
    };
    function weights(t) {
      const w = [];
      for (let k = 0; k < 34; k++) w.push(E.outCubic(inv(termT[k], termT[k] + 0.22, t)));
      return w;
    }
    S.draw = t => {
      const lt = t - 12;
      const w = weights(t);
      const ph = TAU * 0.5 * lt + 0.9;
      const intro = E.outCubic(inv(11.98, 12.5, t));
      // axis
      g.strokeStyle = rgba(COL.ink, 1);
      g.lineWidth = 1;
      g.globalAlpha = 0.12 * SA * intro;
      g.beginPath(); g.moveTo(WX0, EP.y); g.lineTo(WX0 + (WX1 - WX0) * intro, EP.y); g.stroke();
      // epicycles
      let x = EP.x, y = EP.y;
      g.lineCap = 'round';
      for (let k = 0; k < 34; k++) {
        if (w[k] <= 0) break;
        const n = 2 * k + 1, r = (EP.A * 4) / (PI * n) * w[k] * intro;
        const nx = x + r * Math.cos(n * ph), ny = y - r * Math.sin(n * ph);
        g.globalAlpha = (k === 0 ? 0.26 : 0.17) * SA * w[k];
        g.strokeStyle = rgba(COL.ink, 1);
        g.lineWidth = 1;
        g.beginPath(); g.arc(x, y, Math.max(0.1, r), 0, TAU); g.stroke();
        g.globalAlpha = 0.85 * SA * w[k];
        g.strokeStyle = rgba(COL.orange2, 1);
        g.lineWidth = k === 0 ? 2.2 : 1.5;
        g.beginPath(); g.moveTo(x, y); g.lineTo(nx, ny); g.stroke();
        x = nx; y = ny;
      }
      // link to wave
      g.setLineDash([4, 7]);
      g.globalAlpha = 0.35 * SA * intro;
      g.strokeStyle = rgba(COL.ink, 1);
      g.lineWidth = 1;
      g.beginPath(); g.moveTo(x, y); g.lineTo(WX0, y); g.stroke();
      // target square wave
      const span = WX1 - WX0, per = span / 2.25;
      g.globalAlpha = 0.14 * SA * intro;
      g.beginPath();
      for (let px = 0; px <= span * intro; px += 2) {
        const tau = ph - (px / per) * TAU;
        const sy = EP.y - EP.A * Math.sign(Math.sin(tau));
        px === 0 ? g.moveTo(WX0 + px, sy) : g.lineTo(WX0 + px, sy);
      }
      g.stroke();
      g.setLineDash([]);
      // partial sum
      const pts = [];
      for (let px = 0; px <= span * intro; px += 2) {
        const tau = ph - (px / per) * TAU;
        let s = 0;
        for (let k = 0; k < 34; k++) {
          if (w[k] <= 0) break;
          const n = 2 * k + 1;
          s += (w[k] * 4 * Math.sin(n * tau)) / (PI * n);
        }
        pts.push(WX0 + px, EP.y - EP.A * s);
      }
      const path = () => {
        g.beginPath();
        for (let i = 0; i < pts.length; i += 2) (i ? g.lineTo(pts[i], pts[i + 1]) : g.moveTo(pts[i], pts[i + 1]));
      };
      g.globalCompositeOperation = 'lighter';
      g.strokeStyle = rgba(COL.orange, 1);
      g.lineWidth = 10; g.globalAlpha = 0.12 * SA; path(); g.stroke();
      g.lineWidth = 2.8; g.globalAlpha = 0.95 * SA; g.strokeStyle = rgba(COL.orange2, 1); path(); g.stroke();
      glow(SPR.orange, x, y, 22, 0.95 * SA * intro);
      glow(SPR.white, WX0, y, 9, 0.9 * SA * intro);
      glow(SPR.fogA, 960, 220, 520, 0.12 * SA);
      glow(SPR.fogO, 1200, 600, 700, 0.1 * SA);
      g.globalCompositeOperation = 'source-over';
    };
    S.dom = (t, e) => {
      sceneCss(S.root, e.a, (1 - 0.05 * (1 - e.inP)) * (1 + 0.1 * e.outP), e.outP * 8);
      if (e.a <= 0.002) return;
      const lt = t - 12;
      S.els.forEach((el, i) => {
        const f = F[i];
        const p = E.outCubic(inv(f.t, f.t + (i === 0 ? 0.9 : 0.6), t));
        const dx = Math.sin(lt * 0.6 + i * 1.7) * 10 * (1.2 - f.z), dy = Math.cos(lt * 0.5 + i * 2.3) * 8 * (1.2 - f.z);
        const out = e.outP;
        const ox = (f.x - CX) * 0.25 * out, oy = (f.y - CY) * 0.25 * out;
        const a = clamp(p * 1.2) * (i < 2 ? 1 : 0.35 + 0.5 * f.z);
        css(el, a, dx + ox, dy + oy + 24 * (1 - p), 0.92 + 0.08 * p, (1 - p) * 16 + (i < 2 ? 0 : (1 - f.z) * 1.1));
      });
      let N = 1;
      for (let k = 0; k < NT.length; k++) if (t >= 12 + 0.5 * k) N = NT[k];
      S.nl.textContent = `PARTIAL SUM · N = ${N}`;
      css(S.nl, inv(12.2, 12.5, t) * 0.9);
      titleAnim(S.T, t, 12.3, 15.66);
    };
    return S;
  })();
  scenes.push(math);

  // ---------------------------------------------------------------- 4 · language
  const lang = (() => {
    const S = {};
    const WORDS = ['Hello', '你好', 'こんにちは', '안녕하세요', 'Bonjour', 'Hola', 'Ciao', 'Olá', 'Hallo', 'Привет', 'Γειά σου',
      'مرحبا', 'שלום', 'नमस्ते', 'สวัสดี', 'Xin chào', 'Merhaba', 'Hej', 'Ahoj', 'Cześć', 'Szia', 'Salut', 'Jambo',
      'Sawubona', 'Halo', 'Kamusta', 'Aloha', 'Salve', 'Tere', 'Sveiki', 'გამარჯობა', 'Բարեւ', 'ሰላም', 'হ্যালো',
      'வணக்கம்', 'Kia ora', 'Dia dhuit', 'Habari', 'Сайн уу', 'Привіт', 'Здраво', 'Selamat', 'Moien', 'Salam',
      '思考', '想象', '言葉', '생각', 'idée', 'Gedanke', 'мысль', 'فكرة', 'विचार', 'ความคิด', 'idea', 'sueño', 'λόγος',
      'רעיון', '灵感', '創造', '꿈', 'rêve', 'Traum', 'sonho', 'мечта', 'حلم', 'सपना', 'ความฝัน', 'ιδέα', '未来',
      'futuro', 'avenir', 'Zukunft', 'будущее', 'مستقبل', 'भविष्य', '미래', 'みらい', 'zukunft', 'mirai', 'Ubuntu',
      'Namaste', 'Shalom', 'Grazie', 'Merci', 'Danke', 'Thank you', '谢谢', 'ありがとう', '감사합니다', 'Gracias', 'Спасибо',
      'شكرا', 'धन्यवाद', 'ขอบคุณ', 'Obrigado', 'Teşekkürler'];
    const N = WORDS.length;
    const SENT = [
      { t: 16.02, lab: 'EN · ENGLISH', s: 'Ideas know no borders.', f: 'Newsreader', it: true },
      { t: 17.0, lab: 'ZH · 中文', s: '思想，没有国界。', f: 'Noto Serif SC', size: 76 },
      { t: 17.5, lab: 'JA · 日本語', s: 'アイデアに国境はない。', f: 'Noto Sans JP', size: 70, w: 300 },
      { t: 18.0, lab: 'KO · 한국어', s: '생각에는 국경이 없다.', f: 'Noto Sans KR', size: 70, w: 300 },
      { t: 18.5, lab: 'ES · ESPAÑOL', s: 'Las ideas no tienen fronteras.', f: 'Newsreader', it: true },
      { t: 19.0, lab: 'AR · العربية', s: 'الأفكار لا تعرف حدودًا.', f: 'Noto Sans Arabic', size: 72, w: 300, rtl: true },
      { t: 19.5, lab: 'HI · हिन्दी', s: 'विचारों की कोई सीमा नहीं होती।', f: 'Noto Sans Devanagari', size: 66, w: 300 },
    ];
    SENT.forEach((s, i) => (s.e = i + 1 < SENT.length ? SENT[i + 1].t : 20.05));
    const LABFONT = '"JetBrains Mono","Noto Sans SC","Noto Sans JP","Noto Sans KR","Noto Sans Arabic","Noto Sans Devanagari",monospace';
    const pts = [];
    {
      const r = rng(33), ga = PI * (3 - Math.sqrt(5));
      for (let i = 0; i < N; i++) {
        const y = 1 - (2 * (i + 0.5)) / N, rr = Math.sqrt(1 - y * y), th = i * ga;
        pts.push({ x: Math.cos(th) * rr, y, z: Math.sin(th) * rr, s: 0.75 + r() * 0.6, hot: r() < 0.18 });
      }
    }
    S.init = () => {
      S.root = mk('div', 'scene');
      S.words = WORDS.map((w, i) => {
        const el = mk('div', 'word', S.root);
        el.textContent = w;
        el.style.fontSize = Math.round(30 * pts[i].s) + 'px';
        if (pts[i].hot) el.style.color = '#eb9a78';
        return el;
      });
      S.back = mk('div', 'abs', S.root);
      Object.assign(S.back.style, {
        left: '210px', top: '250px', width: '1500px', height: '580px',
        background: 'radial-gradient(closest-side, rgba(12,11,10,.96), rgba(12,11,10,.86) 48%, rgba(12,11,10,0))',
      });
      S.sents = SENT.map(s => {
        const el = mk('div', 'sent', S.root);
        el.style.fontFamily = `"${s.f}", serif`;
        if (!s.it) el.style.fontStyle = 'normal';
        if (s.size) el.style.fontSize = s.size + 'px';
        if (s.w) el.style.fontWeight = s.w;
        if (s.rtl) el.dir = 'rtl';
        if (s === SENT[0]) s.sp = split(el, s.s); else el.textContent = s.s;
        const lab = mk('div', 'lang', S.root);
        lab.textContent = s.lab;
        lab.style.fontFamily = LABFONT;
        return { el, lab };
      });
      S.T = makeTitle(S.root, '04 — LANGUAGES', 'Language.', '语言，跨越边界');
    };
    S.layout = () => S.words.forEach(el => { el._w = el.offsetWidth; el._h = el.offsetHeight; });
    S.draw = t => {
      g.globalCompositeOperation = 'lighter';
      glow(SPR.fogO, CX, CY, 760, 0.16 * SA);
      glow(SPR.fogB, CX + 380, CY - 240, 520, 0.06 * SA);
      // orbit rings
      const lt = t - 16;
      g.globalCompositeOperation = 'source-over';
      g.strokeStyle = rgba(COL.ink, 1);
      g.lineWidth = 1;
      const R = 410 * E.outExpo(inv(15.95, 16.7, t));
      for (let i = 0; i < 3; i++) {
        g.globalAlpha = (0.08 - i * 0.02) * SA;
        g.beginPath();
        g.ellipse(CX, CY, R * (1.08 + i * 0.16), R * (0.3 + i * 0.05), -0.18 + i * 0.12 + lt * 0.02 * (i + 1), 0, TAU);
        g.stroke();
      }
    };
    S.dom = (t, e) => {
      sceneCss(S.root, e.a, (1 - 0.05 * (1 - e.inP)) * (1 + 0.12 * e.outP), e.outP * 8);
      if (e.a <= 0.002) return;
      const lt = t - 16;
      let beat = 0;
      for (let b = 16; b <= t; b += 0.5) beat = Math.exp(-(t - b) * 8);
      const R = 400 * E.outExpo(inv(15.95, 16.8, t)) * (1 + 0.025 * beat);
      const ay = 0.5 * lt + 0.4, ax = 0.32, f = 1500;
      const cy = Math.cos(ay), sy = Math.sin(ay), cx = Math.cos(ax), sx = Math.sin(ax);
      S.words.forEach((el, i) => {
        const p = pts[i];
        const x1 = p.x * cy + p.z * sy, z1 = -p.x * sy + p.z * cy;
        const y2 = p.y * cx - z1 * sx, z2 = p.y * sx + z1 * cx;
        const k = f / (f - z2 * R);
        const X = CX + x1 * R * k, Y = CY + y2 * R * k * 0.92;
        const dep = (z2 + 1) / 2;
        const corner = 1 - 0.82 * E.inOutSine(inv(840, 600, X)) * E.inOutSine(inv(600, 720, Y));
        const a = (0.1 + 0.9 * dep ** 1.6) * inv(15.98 + i * 0.004, 16.4 + i * 0.004, t) * corner;
        css(el, a, X - el._w / 2, Y - el._h / 2, k * (0.7 + 0.3 * dep), (1 - dep) * 2.6);
      });
      css(S.back, 1);
      SENT.forEach((s, i) => {
        const { el, lab } = S.sents[i];
        const pin = E.outCubic(inv(s.t, s.t + 0.22, t));
        const pout = E.inCubic(inv(s.e - 0.14, s.e, t));
        if (i === 0) {
          css(el, pin > 0 && pout < 1 ? 1 : 0);
          anim(s.sp, t, { t0: s.t, st: 0.022, dur: 0.5, dy: 26, blur: 12, out: s.e - 0.16, ost: 0.004, odur: 0.14, ody: 16 });
        } else {
          css(el, pin * (1 - pout), 0, 20 * (1 - pin) - 16 * pout, 1, 12 * (1 - pin) + 10 * pout);
        }
        css(lab, pin * (1 - pout), 0, 8 * (1 - pin), 1, 4 * (1 - pin));
      });
      titleAnim(S.T, t, 16.3, 19.66);
    };
    return S;
  })();
  scenes.push(lang);

  // ---------------------------------------------------------------- 5 · create
  const create = (() => {
    const S = {};
    const paths = [];
    {
      const r = rng(55);
      const pal = [[COL.orange, 34], [COL.ink, 22], [COL.orange2, 16], [COL.sand, 12], [COL.amber, 8], [COL.blue, 5], [COL.green, 3]];
      const tot = pal.reduce((s, p) => s + p[1], 0);
      const pick = () => { let u = r() * tot; for (const [c, w] of pal) { if ((u -= w) <= 0) return c; } return COL.ink; };
      for (let i = 0; i < 760; i++) {
        let x = r() * (W + 200) - 100, y = r() * (H + 160) - 80;
        const x0 = x;
        const arr = [x, y];
        const steps = 110 + ((r() * 90) | 0), len = 5.2;
        for (let s = 0; s < steps; s++) {
          const n = fbm(x * 0.0017 + 3.1, y * 0.0017 + 7.3);
          const ang = n * TAU * 1.7 + 0.6 + 0.35 * Math.sin(y * 0.004);
          x += Math.cos(ang) * len; y += Math.sin(ang) * len;
          arr.push(x, y);
          if (x < -150 || x > W + 150 || y < -150 || y > H + 150) break;
        }
        paths.push({
          p: new Float32Array(arr), c: pick(), w: 0.7 + r() ** 2.2 * 3.6, a: 0.28 + r() * 0.5,
          t0: 20.02 + (x0 / W) * 0.75 + r() * 0.35, d: 1.5 + r() * 0.9,
        });
      }
    }
    S.init = () => {
      S.root = mk('div', 'scene');
      S.back = mk('div', 'abs', S.root);
      Object.assign(S.back.style, {
        left: '310px', top: '300px', width: '1300px', height: '420px',
        background: 'radial-gradient(closest-side, rgba(12,11,10,.86), rgba(12,11,10,.55) 55%, rgba(12,11,10,0))',
      });
      S.cn = split(mk('div', 'poem-cn', S.root), '以语言为笔，以思想为墨。', [1, 3]);
      S.cn[1].style.textShadow = S.cn[2].style.textShadow = '0 0 30px rgba(217,119,87,.5)';
      S.en = split(mk('div', 'poem-en', S.root), 'Language is the brush. Thought is the ink.');
      S.T = makeTitle(S.root, '05 — CREATIVITY', 'Create.', '创造，让想象成真');
    };
    S.draw = t => {
      g.lineCap = 'round';
      g.lineJoin = 'round';
      g.globalCompositeOperation = 'source-over';
      for (const P of paths) {
        const q = E.outCubic(inv(P.t0, P.t0 + P.d, t));
        if (q <= 0) continue;
        const n = P.p.length / 2, m = Math.max(2, Math.floor(n * q));
        g.globalAlpha = P.a * SA;
        g.strokeStyle = rgba(P.c, 1);
        g.lineWidth = P.w;
        g.beginPath();
        g.moveTo(P.p[0], P.p[1]);
        for (let i = 1; i < m; i++) g.lineTo(P.p[i * 2], P.p[i * 2 + 1]);
        g.stroke();
      }
      g.globalCompositeOperation = 'lighter';
      for (const P of paths) {
        const q = E.outCubic(inv(P.t0, P.t0 + P.d, t));
        if (q <= 0 || q >= 1) continue;
        const n = P.p.length / 2, m = Math.max(1, Math.floor(n * q)) - 1;
        glow(SPR.white, P.p[m * 2], P.p[m * 2 + 1], 3 + P.w * 2.2, 0.55 * (1 - q) * SA);
      }
      g.globalCompositeOperation = 'source-over';
    };
    S.dom = (t, e) => {
      sceneCss(S.root, e.a, (1 - 0.04 * (1 - e.inP)) * (1 + 0.1 * e.outP), e.outP * 8);
      if (e.a <= 0.002) return;
      css(S.back, E.outCubic(inv(20.2, 20.9, t)));
      anim(S.cn, t, { t0: 20.45, st: 0.07, dur: 0.9, dy: 30, blur: 16, ease: E.outQuint });
      anim(S.en, t, { t0: 21.2, st: 0.016, dur: 0.6, dy: 14, blur: 8 });
      titleAnim(S.T, t, 20.3, 23.66);
    };
    return S;
  })();
  scenes.push(create);

  // ---------------------------------------------------------------- 6 · agents
  const agents = (() => {
    const S = {};
    const COLS = 15, ROWS = 8, TS = 92, GAP = 18;
    const GX = (W - (COLS * (TS + GAP) - GAP)) / 2, GY = (H - (ROWS * (TS + GAP) - GAP)) / 2;
    const tiles = [];
    {
      const r = rng(66);
      for (let j = 0; j < ROWS; j++) for (let i = 0; i < COLS; i++) {
        const x = GX + i * (TS + GAP) + TS / 2, y = GY + j * (TS + GAP) + TS / 2;
        const d = Math.hypot((x - CX) / W, (y - CY) / H) / 0.7;
        const ta = 24.0 + d * 0.3 + r() * 0.05;
        tiles.push({ x, y, type: (r() * 8) | 0, ta, td: ta + 0.35 + r() * 0.95, seed: (r() * 1e9) | 0, hue: r() });
      }
    }
    const rr = (x, y, w, h, r) => { g.beginPath(); g.roundRect(x, y, w, h, r); };
    function tileContent(T, x0, y0, s, p, a) {
      const R = rng(T.seed), c = [COL.orange, COL.ink, COL.blue, COL.amber][(T.hue * 4) | 0];
      g.globalAlpha = a * SA;
      const X = u => x0 + u * s, Y = v => y0 + v * s;
      switch (T.type) {
        case 0: // code lines
          for (let k = 0; k < 5; k++) {
            const w = (0.25 + R() * 0.45) * clamp(p * 5 - k);
            g.fillStyle = rgba(k % 2 ? COL.ink : c, 0.75);
            g.fillRect(X(0.14 + (k % 3 === 2 ? 0.1 : 0)), Y(0.24 + k * 0.12), w * s, 0.05 * s);
          }
          break;
        case 1: // bars
          for (let k = 0; k < 5; k++) {
            const h = (0.2 + R() * 0.45) * E.outCubic(clamp(p * 1.6 - k * 0.12));
            g.fillStyle = rgba(k === 3 ? COL.orange : COL.ink, k === 3 ? 0.95 : 0.55);
            g.fillRect(X(0.17 + k * 0.14), Y(0.8) - h * s, 0.09 * s, h * s);
          }
          break;
        case 2: { // wave
          g.strokeStyle = rgba(c, 0.9); g.lineWidth = 1.6; g.beginPath();
          const f = 1 + R() * 2;
          for (let u = 0; u <= p; u += 0.04) {
            const v = 0.5 - 0.22 * Math.sin(u * TAU * f + T.seed);
            u === 0 ? g.moveTo(X(0.12 + u * 0.76), Y(v)) : g.lineTo(X(0.12 + u * 0.76), Y(v));
          }
          g.stroke(); break;
        }
        case 3: // document
          g.fillStyle = rgba(COL.ink, 0.8); g.fillRect(X(0.16), Y(0.22), 0.4 * s * clamp(p * 3), 0.07 * s);
          for (let k = 0; k < 4; k++) { g.fillStyle = rgba(COL.ink, 0.35); g.fillRect(X(0.16), Y(0.4 + k * 0.1), (0.68 - (k === 3 ? 0.3 : 0)) * s * clamp(p * 4 - k), 0.035 * s); }
          break;
        case 4: // ring progress
          g.strokeStyle = rgba(COL.ink, 0.15); g.lineWidth = 0.07 * s;
          g.beginPath(); g.arc(X(0.5), Y(0.52), 0.24 * s, 0, TAU); g.stroke();
          g.strokeStyle = rgba(c, 0.95);
          g.beginPath(); g.arc(X(0.5), Y(0.52), 0.24 * s, -PI / 2, -PI / 2 + TAU * p); g.stroke();
          break;
        case 5: // globe
          g.strokeStyle = rgba(COL.ink, 0.6); g.lineWidth = 1.1;
          g.beginPath(); g.arc(X(0.5), Y(0.52), 0.26 * s, 0, TAU); g.stroke();
          for (let k = -1; k <= 1; k++) { g.beginPath(); g.ellipse(X(0.5), Y(0.52), Math.abs(Math.cos(p * PI + k)) * 0.26 * s + 0.01, 0.26 * s, 0, 0, TAU); g.stroke(); }
          break;
        case 6: // glyph
          g.fillStyle = rgba(c, 0.9); g.font = `300 ${Math.round(0.46 * s)}px "Newsreader"`;
          g.textAlign = 'center'; g.fillText(['∑', '∫', 'π', 'λ', '∂', '∞', '文', 'A'][T.seed % 8], X(0.5), Y(0.66)); g.textAlign = 'left';
          break;
        default: // chat bubbles
          g.fillStyle = rgba(COL.ink, 0.28); rr(X(0.14), Y(0.24), 0.5 * s * clamp(p * 3), 0.16 * s, 0.07 * s); g.fill();
          g.fillStyle = rgba(COL.orange, 0.8); rr(X(0.36), Y(0.5), 0.5 * s * clamp(p * 3 - 1), 0.16 * s, 0.07 * s); g.fill();
      }
    }
    S.init = () => {
      S.root = mk('div', 'scene');
      S.back = mk('div', 'abs', S.root);
      Object.assign(S.back.style, {
        left: '260px', top: '290px', width: '1400px', height: '500px',
        background: 'radial-gradient(closest-side, rgba(12,11,10,.94), rgba(12,11,10,.8) 50%, rgba(12,11,10,0))',
      });
      S.en = split(mk('div', 'big-en', S.root), 'One mind. A thousand hands.', [10, 26]);
      S.cn = split(mk('div', 'big-cn', S.root), '一个心智，千手并行。');
    };
    S.draw = t => {
      const zoom = lerp(1.2, 0.9, E.outCubic(inv(24, 25.6, t)));
      const imp = E.inCubic(inv(25.4, 25.93, t));
      const txt = E.outCubic(inv(24.05, 24.35, t)) * (1 - inv(25.3, 25.6, t));
      g.save();
      g.translate(CX, CY); g.scale(zoom, zoom); g.rotate(lerp(-0.035, 0.02, inv(24, 26, t))); g.translate(-CX, -CY);
      for (const T of tiles) {
        const p = E.outBack(inv(T.ta, T.ta + 0.3, t), 2.4);
        if (p <= 0) continue;
        const k = imp * (0.75 + 0.25 * hash1(T.seed));
        const x = lerp(T.x, CX, k), y = lerp(T.y, CY, k);
        const s = TS * p * (1 - 0.94 * k);
        const done = t >= T.td, fl = done ? Math.exp(-(t - T.td) * 6) : 0;
        const hush = 1 - 0.72 * txt * Math.exp(-(((y - CY) / 150) ** 2)) * (1 - E.inOutSine(inv(1500, 1750, Math.abs(x - CX) * 2)));
        g.globalCompositeOperation = 'source-over';
        g.globalAlpha = SA * (1 - 0.4 * k) * hush;
        g.fillStyle = 'rgba(29,26,23,0.96)';
        rr(x - s / 2, y - s / 2, s, s, 0.16 * s); g.fill();
        g.lineWidth = 1 + fl * 1.5;
        g.strokeStyle = done ? rgba(COL.orange, 0.35 + 0.6 * fl) : rgba(COL.ink, 0.12);
        g.stroke();
        if (k < 0.6 && p > 0.5) {
          tileContent(T, x - s / 2, y - s / 2, s, inv(T.ta + 0.1, T.td, t), (1 - k / 0.6) * clamp(p) * hush);
          const dx = x + s * 0.33, dy = y - s * 0.33;
          g.globalAlpha = SA * (1 - k / 0.6) * hush;
          if (done) {
            g.fillStyle = rgba(COL.orange, 1);
            g.beginPath(); g.arc(dx, dy, 0.06 * s * (1 + fl * 0.6), 0, TAU); g.fill();
          } else {
            g.strokeStyle = rgba(COL.orange2, 0.9); g.lineWidth = 1.4;
            const a0 = t * 9 + T.seed;
            g.beginPath(); g.arc(dx, dy, 0.06 * s, a0, a0 + 4.2); g.stroke();
          }
        }
        if (fl > 0.02) { g.globalCompositeOperation = 'lighter'; glow(SPR.orange, x, y, s * 0.9, fl * 0.35 * SA); }
        if (k > 0.05) { g.globalCompositeOperation = 'lighter'; glow(SPR.orange, x, y, 10 + 20 * k, k * 0.8 * SA); }
      }
      g.restore();
      g.globalCompositeOperation = 'lighter';
      glow(SPR.orange, CX, CY, 60 + 300 * imp, imp * 0.9 * SA);
      glow(SPR.white, CX, CY, 10 + 60 * imp, imp * SA);
      g.globalCompositeOperation = 'source-over';
    };
    S.dom = (t, e) => {
      sceneCss(S.root, e.a);
      if (e.a <= 0.002) return;
      css(S.back, E.outCubic(inv(24.05, 24.4, t)) * (1 - inv(25.4, 25.7, t)));
      const k = E.inCubic(inv(25.35, 25.85, t));
      anim(S.en, t, { t0: 24.08, st: 0.011, dur: 0.42, dy: 30, blur: 14, ease: E.outQuint });
      anim(S.cn, t, { t0: 24.3, st: 0.025, dur: 0.4, dy: 16, blur: 8 });
      if (k > 0) {
        for (const s of [...S.en, ...S.cn]) {
          const [x, y] = [s._cx, s._cy];
          css(s, 1 - k, (CX - x) * k, (CY - y) * k, 1 - 0.8 * k, 6 * k);
        }
      }
    };
    S.layout = () => {
      for (const s of [...S.en, ...S.cn]) { const [x, y] = absPos(s); s._cx = x + s.offsetWidth / 2; s._cy = y + s.offsetHeight / 2; }
    };
    return S;
  })();
  scenes.push(agents);

  // ---------------------------------------------------------------- 7 · finale
  const finale = (() => {
    const S = {};
    const LOCK = { R: 92, sx: 0, sy: 486 };
    const burst = [];
    {
      const r = rng(77), cols = [COL.orange, COL.orange2, COL.ink, COL.amber, COL.white];
      for (let i = 0; i < 900; i++) burst.push({ a: r() * TAU, v: 300 + r() ** 1.6 * 2600, k: 2 + r() * 2.5, s: 0.8 + r() * 2.4, c: cols[(r() * 5) | 0], life: 0.8 + r() * 1.8 });
    }
    const dust = [];
    {
      const r = rng(78);
      for (let i = 0; i < 160; i++) dust.push({ x: r() * W, y: r() * H, z: r(), ph: r() * TAU, sp: 6 + r() * 14 });
    }
    S.init = () => {
      S.root = mk('div', 'scene');
      S.markEl = mk('div', 'mark', S.root);
      S.mark = split(S.markEl, 'Claude');
      S.ten = split(mk('div', 'tag-en', S.root), 'Ignite every idea.', [7, 17]);
      S.tcn = split(mk('div', 'tag-cn', S.root), '点亮每一个想法。');
      S.cred = mk('div', 'credit', S.root,
        'Every frame and every note of this film was generated by Claude, in code.<br><span class="cn">本片的每一帧画面、每一个音符，均由 Claude 以代码生成。</span>');
    };
    S.layout = () => {
      const w = S.markEl.offsetWidth, gap = 50;
      const total = 2 * LOCK.R + gap + w;
      const left = CX - total / 2;
      LOCK.sx = left + LOCK.R;
      S.markEl.style.left = (left + 2 * LOCK.R + gap).toFixed(1) + 'px';
      S.markEl.style.top = (LOCK.sy - 236 * 0.37).toFixed(1) + 'px';
    };
    S.draw = t => {
      if (t < 26) return;
      const lt = t - 26;
      g.globalCompositeOperation = 'lighter';
      // warm bloom
      glow(SPR.fogO, CX, CY - 40, 900 + 200 * Math.exp(-lt * 2), (0.22 + 0.6 * Math.exp(-lt * 3)) * SA);
      glow(SPR.fogD, CX - 500, CY + 300, 700, 0.12 * SA);
      glow(SPR.fogA, CX + 520, CY - 260, 600, 0.08 * SA);
      // burst
      g.lineCap = 'round';
      for (const p of burst) {
        if (lt > p.life) continue;
        const d = (p.v / p.k) * (1 - Math.exp(-p.k * lt)), d0 = (p.v / p.k) * (1 - Math.exp(-p.k * Math.max(0, lt - 0.03)));
        const ca = Math.cos(p.a), sa = Math.sin(p.a);
        const a = (1 - lt / p.life) ** 1.5;
        g.globalAlpha = a * SA * 0.9;
        g.strokeStyle = rgba(p.c, 1);
        g.lineWidth = p.s;
        g.beginPath(); g.moveTo(CX + ca * d0, CY + sa * d0 * 0.8); g.lineTo(CX + ca * d + 0.01, CY + sa * d * 0.8); g.stroke();
      }
      // dust motes
      for (const p of dust) {
        const y = (p.y - lt * p.sp + H) % H, x = p.x + Math.sin(lt * 0.7 + p.ph) * 10;
        glow(SPR.ink, x, y, 2 + p.z * 4, (0.1 + 0.25 * p.z) * inv(26.3, 27.3, t) * SA);
      }
      // shockwaves
      ring(CX, CY, 80 + 1700 * E.outExpo(clamp(lt / 1.3)), 7 * (1 - clamp(lt / 1.3)), 0.9 * (1 - clamp(lt / 1.3)) ** 2);
      ring(CX, CY, 60 + 1100 * E.outExpo(clamp((lt - 0.08) / 1.5)), 3 * (1 - clamp(lt / 1.5)), 0.6 * (1 - clamp(lt / 1.5)) ** 2, COL.ink);
      ring(CX, CY, 40 + 600 * E.outExpo(clamp((lt - 0.16) / 1.6)), 2 * (1 - clamp(lt / 1.6)), 0.4 * (1 - clamp(lt / 1.6)) ** 2);
      g.globalCompositeOperation = 'source-over';
      // spark: bloom at centre, then glide into the lockup
      const mv = E.inOutCubic(inv(26.5, 27.2, t));
      const x = lerp(CX, LOCK.sx, mv), y = lerp(CY, LOCK.sy, mv);
      const R = lerp(150, LOCK.R, mv);
      g.globalCompositeOperation = 'lighter';
      glow(SPR.orange, x, y, R * 2.4, 0.45 * SA);
      g.globalCompositeOperation = 'source-over';
      drawSpark(x, y, R, -0.4 + 0.5 * E.outCubic(clamp(lt / 1.4)) + 0.06 * lt, lt, 1, 0.35 + 0.5 * Math.exp(-lt * 2));
    };
    S.dom = (t, e) => {
      sceneCss(S.root, t >= 26 ? 1 : 0);
      if (t < 26) return;
      anim(S.mark, t, { t0: 26.78, st: 0.06, dur: 0.8, dx: -40, dy: 0, blur: 18, ease: E.outQuint });
      anim(S.ten, t, { t0: 27.45, st: 0.022, dur: 0.6, dy: 20, blur: 10 });
      anim(S.tcn, t, { t0: 27.8, st: 0.045, dur: 0.6, dy: 14, blur: 8 });
      const pc = E.outCubic(inv(28.35, 28.95, t));
      css(S.cred, pc, 0, 12 * (1 - pc), 1, 6 * (1 - pc));
    };
    return S;
  })();
  scenes.push(finale);

  const SCHED = [
    [intro, 0, 4.0], [reason, 4, 8], [code, 8, 12], [math, 12, 16], [lang, 16, 20], [create, 20, 24], [agents, 24, 26.2], [finale, 26, 30],
  ];

  // ------------------------------------------------------------------ HUD
  const hud = {};
  function initHud() {
    const tl = mk('div', 'hud-tl', hudRoot);
    const ic = mk('canvas', '', tl);
    ic.width = ic.height = 52; ic.style.width = ic.style.height = '26px'; ic.style.position = 'static';
    const ix = ic.getContext('2d');
    ix.translate(26, 26);
    sparkPath(ix, 25, 0, null, '#d97757');
    mk('span', '', tl, 'CLAUDE');
    hud.tl = tl; hud.icon = ic;
    hud.tr = mk('div', 'hud-tr', hudRoot);
    const bar = mk('div', 'hud-bar', hudRoot);
    hud.segs = SECTIONS.map(([n, en]) => {
      const s = mk('div', 'seg', bar);
      s.style.flex = en === 'AGENTS' ? '0.55' : '1';
      const tr = mk('div', 'track', s), fill = mk('div', 'fill', tr);
      const lab = mk('div', 'lab', s, `${n} ${en}`);
      return { fill, lab };
    });
    hud.bar = bar;
  }
  function drawHud(t) {
    const a = E.outCubic(inv(4.3, 4.9, t)) * (1 - inv(25.35, 25.75, t));
    css(hudRoot, a);
    if (a <= 0.002) return;
    hud.icon.style.transform = `rotate(${(t * 18) % 360}deg)`;
    let cur = SECTIONS[0];
    for (const s of SECTIONS) if (t >= s[3] - 0.05) cur = s;
    hud.tr.innerHTML = `<b>${cur[0]}</b> / 06 &nbsp;—&nbsp; ${cur[1]} · ${cur[2]}`;
    SECTIONS.forEach((s, i) => {
      const p = inv(s[3], s[4], t);
      hud.segs[i].fill.style.width = (p * 100).toFixed(2) + '%';
      hud.segs[i].fill.style.opacity = p >= 1 ? '0.45' : '1';
      hud.segs[i].lab.style.color = t >= s[3] && t < s[4] ? '#eb9a78' : p >= 1 ? 'rgba(243,237,228,.5)' : '';
    });
  }

  // ------------------------------------------------------------------ FX
  const vignette = (() => {
    const c = document.createElement('canvas');
    c.width = W; c.height = H;
    const x = c.getContext('2d');
    const gr = x.createRadialGradient(CX, CY, 300, CX, CY, 1180);
    gr.addColorStop(0, 'rgba(0,0,0,0)'); gr.addColorStop(0.65, 'rgba(0,0,0,0.25)'); gr.addColorStop(1, 'rgba(0,0,0,0.78)');
    x.fillStyle = gr;
    x.fillRect(0, 0, W, H);
    return c;
  })();
  const grainTiles = [];
  for (let k = 0; k < 6; k++) {
    const c = document.createElement('canvas');
    c.width = c.height = 384;
    const x = c.getContext('2d'), id = x.createImageData(384, 384), r = rng(900 + k);
    for (let i = 0; i < id.data.length; i += 4) {
      const v = (128 + (r() - 0.5) * 110) | 0;
      id.data[i] = id.data[i + 1] = id.data[i + 2] = v;
      id.data[i + 3] = 255;
    }
    x.putImageData(id, 0, 0);
    grainTiles.push(c);
  }
  function drawFx(t) {
    fg.setTransform(DPR, 0, 0, DPR, 0, 0);
    fg.clearRect(0, 0, W, H);
    fg.globalCompositeOperation = 'source-over';
    fg.globalAlpha = 1;
    fg.drawImage(vignette, 0, 0);
    // light sweep at every cut
    for (const c of CUTS) {
      const p = inv(c - 0.2, c + 0.22, t);
      if (p <= 0 || p >= 1) continue;
      const x = lerp(-700, W + 700, E.inOutSine(p));
      const gr = fg.createLinearGradient(x - 380, 0, x + 380, 0);
      gr.addColorStop(0, 'rgba(255,236,220,0)'); gr.addColorStop(0.5, 'rgba(255,226,205,0.2)'); gr.addColorStop(1, 'rgba(255,236,220,0)');
      fg.save();
      fg.globalCompositeOperation = 'lighter';
      fg.translate(x, CY); fg.transform(1, 0, -0.35, 1, 0, 0); fg.translate(-x, -CY);
      fg.fillStyle = gr;
      fg.fillRect(x - 400, -200, 800, H + 400);
      fg.restore();
      const f = Math.exp(-Math.abs(t - c) * 14) * 0.12;
      fg.fillStyle = `rgba(255,232,214,${f.toFixed(3)})`;
      fg.fillRect(0, 0, W, H);
    }
    // impacts
    for (const [ti, amt] of [[4, 0.85], [26, 0.95]]) {
      if (t < ti - 0.02 || t > ti + 1) continue;
      const f = t < ti ? 0 : amt * Math.exp(-(t - ti) * 8.5);
      fg.fillStyle = `rgba(255,238,224,${f.toFixed(3)})`;
      fg.fillRect(0, 0, W, H);
    }
    // the breath before the drop
    const pre = inv(25.78, 25.98, t) * (1 - inv(25.99, 26.0, t));
    if (pre > 0) { fg.fillStyle = `rgba(4,3,3,${(0.9 * pre).toFixed(3)})`; fg.fillRect(0, 0, W, H); }
    // final fade
    const fo = E.inOutSine(inv(29.25, 29.95, t));
    if (fo > 0) { fg.fillStyle = `rgba(5,4,4,${fo.toFixed(3)})`; fg.fillRect(0, 0, W, H); }
    // film grain (deterministic per frame)
    const fr = Math.round(t * FPS);
    gg.setTransform(1, 0, 0, 1, 0, 0);
    const tile = grainTiles[fr % grainTiles.length];
    const ox = -Math.floor(hash1(fr * 2 + 1) * 384), oy = -Math.floor(hash1(fr * 2 + 2) * 384);
    for (let x = ox; x < W * DPR; x += 384) for (let y = oy; y < H * DPR; y += 384) gg.drawImage(tile, x, y);
  }
  function shake(t) {
    let x = 0, y = 0;
    for (const [ti, A] of [[4, 11], [26, 15], [8, 3], [12, 3], [16, 3], [20, 3], [24, 3]]) {
      const d = t - ti;
      if (d < 0 || d > 0.7) continue;
      const k = A * Math.exp(-d * 8);
      x += k * Math.sin(d * 97 + ti); y += k * Math.cos(d * 73 + ti * 2);
    }
    return [x, y];
  }

  // ------------------------------------------------------------------ background
  function drawBackground(t) {
    g.setTransform(DPR, 0, 0, DPR, 0, 0);
    g.globalCompositeOperation = 'source-over';
    g.globalAlpha = 1;
    g.fillStyle = '#0c0b0a';
    g.fillRect(0, 0, W, H);
    const lvl = 0.35 + 0.65 * inv(3, 4.2, t);
    g.globalCompositeOperation = 'lighter';
    glow(SPR.fogD, W * (0.2 + 0.03 * Math.sin(t * 0.21)), H * (0.8 + 0.04 * Math.cos(t * 0.17)), 900, 0.13 * lvl);
    glow(SPR.fogO, W * (0.82 + 0.03 * Math.sin(t * 0.13 + 2)), H * (0.18 + 0.04 * Math.sin(t * 0.19)), 800, 0.08 * lvl);
    glow(SPR.fogB, W * (0.9 + 0.02 * Math.sin(t * 0.1)), H * (0.95), 700, 0.035 * lvl);
    g.globalCompositeOperation = 'source-over';
  }

  // ------------------------------------------------------------------ frame
  function render(t) {
    t = clamp(t, 0, DURATION);
    drawBackground(t);
    for (const [S, t0, t1] of SCHED) {
      const e = S === intro ? { a: t < 4.02 ? 1 : 0, inP: 1, outP: 0 } : S === finale ? { a: t >= 26 ? 1 : 0, inP: 1, outP: 0 } :
        S === agents ? { a: t >= 23.94 && t < 26 ? E.outCubic(inv(23.94, 24.2, t)) : 0, inP: E.outCubic(inv(23.94, 24.2, t)), outP: 0 } : env(t, t0, t1);
      withScene(e, () => S.draw(t), S === reason ? 0.0 : 0.06, S === reason ? 0.7 : 0.14);
      S.dom(t, e);
    }
    drawHud(t);
    drawFx(t);
    const [sx, sy] = shake(t);
    cam.style.transform = `translate(${sx.toFixed(2)}px,${sy.toFixed(2)}px) scale(1.012)`;
  }

  // ------------------------------------------------------------------ boot
  async function boot() {
    for (const [S] of SCHED) S.init();
    initHud();
    // make every scene laid out so fonts get requested, then wait for them
    for (const [S] of SCHED) { S.root.style.display = 'block'; S.root._d = 1; }
    const jobs = [];
    for (const el of document.querySelectorAll('#stage *')) {
      if (!el.firstChild || el.firstChild.nodeType !== 3) continue;
      const cs = getComputedStyle(el);
      jobs.push(document.fonts.load(`${cs.fontStyle} ${cs.fontWeight} 40px ${cs.fontFamily}`, el.textContent).catch(() => {}));
    }
    const canvasFonts = [['400 16px "JetBrains Mono"', 'hypothesis ∴…'], ['400 17px "Noto Sans SC"', '假设证据洞见推演因果反例'], ['300 40px "Newsreader"', '∑∫πλ∂∞文A']];
    for (const [f, s] of canvasFonts) jobs.push(document.fonts.load(f, s).catch(() => {}));
    await Promise.all(jobs);
    await document.fonts.ready;
    for (const [S] of SCHED) if (S.layout) S.layout();
    for (const [S] of SCHED) { S.root.style.display = 'none'; S.root._d = 0; }
    render(0);
    await new Promise(r => requestAnimationFrame(() => requestAnimationFrame(r)));
    window.__ready = true;
  }
  window.renderAt = t => { render(t); return true; };
  window.FILM = { DURATION, FPS, W, H };

  // ------------------------------------------------------------------ preview player
  function fit() {
    const s = Math.min(innerWidth / W, innerHeight / H);
    stage.style.transform = `translate(${(innerWidth - W * s) / 2}px,${(innerHeight - H * s) / 2}px) scale(${s})`;
  }
  boot().then(() => {
    if (RENDER) return;
    const ui = document.getElementById('ui'), seek = document.getElementById('seek'), btn = document.getElementById('play'), tc = document.getElementById('tc');
    ui.style.display = 'flex';
    fit();
    addEventListener('resize', fit);
    const audio = new Audio('build/score.m4a');
    let playing = false, t0 = 0, base = 0;
    const q = new URLSearchParams(location.search);
    let t = parseFloat(q.get('t') || '0');
    const now = () => (audio.readyState >= 2 && !audio.paused ? audio.currentTime : base + (performance.now() - t0) / 1000);
    function toggle() {
      playing = !playing;
      btn.textContent = playing ? '❚❚ Pause' : '▶ Play';
      if (playing) {
        if (t >= DURATION) t = 0;
        base = t; t0 = performance.now();
        audio.currentTime = t;
        audio.play().catch(() => {});
      } else audio.pause();
    }
    btn.onclick = toggle;
    addEventListener('keydown', e => { if (e.code === 'Space') { e.preventDefault(); toggle(); } });
    seek.oninput = () => { t = parseFloat(seek.value); base = t; t0 = performance.now(); audio.currentTime = t; render(t); };
    (function loop() {
      if (playing) {
        t = now();
        if (t >= DURATION) { t = DURATION; toggle(); }
        seek.value = t;
      }
      render(t);
      tc.textContent = t.toFixed(2);
      requestAnimationFrame(loop);
    })();
  });
})();
