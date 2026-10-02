// Procedural sprite-atlas generator for the "blue-maid" dsh-tauri-pet.
// Renders an 8-col x 11-row atlas (192x208 per cell) with Node built-ins only.
import { deflateSync } from 'node:zlib';
import { writeFileSync, mkdirSync } from 'node:fs';

const SS = 3;                    // supersample factor
const CW = 192, CH = 208;        // cell size
const COLS = 8, ROWS = 11;
const W = CW * COLS, H = CH * ROWS;
const FRAMES = [6, 8, 8, 4, 5, 8, 6, 6, 6, 8, 8];

// ---------------- affine ----------------
const mT = (x, y) => ({ a: 1, b: 0, c: 0, d: 1, e: x, f: y });
const mR = (t) => { const c = Math.cos(t), s = Math.sin(t); return { a: c, b: s, c: -s, d: c, e: 0, f: 0 }; };
const mS = (x, y) => ({ a: x, b: 0, c: 0, d: y, e: 0, f: 0 });
function mul(m, n) {
  return {
    a: m.a * n.a + m.c * n.b, b: m.b * n.a + m.d * n.b,
    c: m.a * n.c + m.c * n.d, d: m.b * n.c + m.d * n.d,
    e: m.a * n.e + m.c * n.f + m.e, f: m.b * n.e + m.d * n.f + m.f,
  };
}
function invert(m) {
  const det = m.a * m.d - m.b * m.c;
  return { a: m.d / det, b: -m.b / det, c: -m.c / det, d: m.a / det,
           e: (m.c * m.f - m.d * m.e) / det, f: (m.b * m.e - m.a * m.f) / det };
}

// ---------------- surface (premultiplied RGBA) ----------------
class Surf {
  constructor() {
    this.b = new Uint8ClampedArray(CW * SS * CH * SS * 4);
    this.M = mT(0, 0);
    this.inv = invert(this.M);
  }
  set(M) { this.M = M; this.inv = invert(M); }
  fill(test, bbox, rgba) {
    const M = this.M, inv = this.inv, sw = CW * SS, sh = CH * SS;
    const cs = [[bbox[0], bbox[1]], [bbox[2], bbox[1]], [bbox[0], bbox[3]], [bbox[2], bbox[3]]]
      .map(([x, y]) => ({ x: (M.a * x + M.c * y + M.e) * SS, y: (M.b * x + M.d * y + M.f) * SS }));
    let x0 = Infinity, x1 = -Infinity, y0 = Infinity, y1 = -Infinity;
    for (const c of cs) { x0 = Math.min(x0, c.x); x1 = Math.max(x1, c.x); y0 = Math.min(y0, c.y); y1 = Math.max(y1, c.y); }
    x0 = Math.max(0, Math.floor(x0) - 1); x1 = Math.min(sw - 1, Math.ceil(x1) + 1);
    y0 = Math.max(0, Math.floor(y0) - 1); y1 = Math.min(sh - 1, Math.ceil(y1) + 1);
    const a = rgba[3] / 255, sr = rgba[0] * a, sg = rgba[1] * a, sb = rgba[2] * a, b = this.b;
    for (let sy = y0; sy <= y1; sy++) {
      const cy = (sy + 0.5) / SS;
      for (let sx = x0; sx <= x1; sx++) {
        const cx = (sx + 0.5) / SS;
        const lx = inv.a * cx + inv.c * cy + inv.e, ly = inv.b * cx + inv.d * cy + inv.f;
        if (!test(lx, ly)) continue;
        const i = (sy * sw + sx) * 4;
        if (a >= 0.999) { b[i] = rgba[0]; b[i + 1] = rgba[1]; b[i + 2] = rgba[2]; b[i + 3] = 255; continue; }
        const ia = 1 - a;
        b[i] = sr + b[i] * ia; b[i + 1] = sg + b[i + 1] * ia; b[i + 2] = sb + b[i + 2] * ia;
        b[i + 3] = 255 * a + b[i + 3] * ia;
      }
    }
  }
}

// ---------------- shapes ----------------
function ell(cx, cy, rx, ry, rot = 0) {
  const c = Math.cos(-rot), s = Math.sin(-rot), rmax = Math.max(rx, ry);
  return {
    test: (x, y) => { const dx = x - cx, dy = y - cy; const u = c * dx - s * dy, v = s * dx + c * dy;
      return (u * u) / (rx * rx) + (v * v) / (ry * ry) <= 1; },
    bbox: [cx - rmax - 1, cy - rmax - 1, cx + rmax + 1, cy + rmax + 1],
  };
}
function ellHalfY(cx, cy, rx, ry, yMax, rot = 0) {
  const e = ell(cx, cy, rx, ry, rot);
  return { test: (x, y) => y <= yMax && e.test(x, y), bbox: e.bbox };
}
function cap(x1, y1, x2, y2, w) {
  const r = w / 2, dx = x2 - x1, dy = y2 - y1, l2 = dx * dx + dy * dy;
  return {
    test: (x, y) => { let t = l2 > 0 ? ((x - x1) * dx + (y - y1) * dy) / l2 : 0; t = t < 0 ? 0 : t > 1 ? 1 : t;
      const px = x1 + t * dx, py = y1 + t * dy, ax = x - px, ay = y - py; return ax * ax + ay * ay <= r * r; },
    bbox: [Math.min(x1, x2) - r - 1, Math.min(y1, y2) - r - 1, Math.max(x1, x2) + r + 1, Math.max(y1, y2) + r + 1],
  };
}
function poly(pts) {
  let x0 = Infinity, x1 = -Infinity, y0 = Infinity, y1 = -Infinity;
  for (const p of pts) { x0 = Math.min(x0, p[0]); x1 = Math.max(x1, p[0]); y0 = Math.min(y0, p[1]); y1 = Math.max(y1, p[1]); }
  return {
    test: (x, y) => { let inside = false;
      for (let i = 0, j = pts.length - 1; i < pts.length; j = i++) {
        const xi = pts[i][0], yi = pts[i][1], xj = pts[j][0], yj = pts[j][1];
        if ((yi > y) !== (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) inside = !inside;
      } return inside; },
    bbox: [x0 - 1, y0 - 1, x1 + 1, y1 + 1],
  };
}
function taper(pts, wids, grow = 0) {
  const L = [], R = [];
  for (let i = 0; i < pts.length; i++) {
    const a = pts[Math.max(0, i - 1)], z = pts[Math.min(pts.length - 1, i + 1)], q = pts[i];
    let dx = z[0] - a[0], dy = z[1] - a[1]; const l = Math.hypot(dx, dy) || 1; dx /= l; dy /= l;
    const nx = -dy, ny = dx, w = (wids[i] !== undefined ? wids[i] : wids[wids.length - 1]) / 2 + grow;
    L.push([q[0] + nx * w, q[1] + ny * w]); R.push([q[0] - nx * w, q[1] - ny * w]);
  }
  return poly([...L, ...R.reverse()]);
}
function rrect(cx, cy, w, h, r) {
  const hx = w / 2, hy = h / 2;
  return {
    test: (x, y) => { const dx = Math.abs(x - cx) - (hx - r), dy = Math.abs(y - cy) - (hy - r);
      if (dx <= 0 && dy <= 0) return true; const ax = Math.max(dx, 0), ay = Math.max(dy, 0);
      return ax * ax + ay * ay <= r * r; },
    bbox: [cx - hx - 1, cy - hy - 1, cx + hx + 1, cy + hy + 1],
  };
}

// ---------------- palette ----------------
const C = {
  skin: [255, 227, 205, 255], skinSh: [243, 200, 172, 255],
  hair: [112, 170, 248, 255], hairSh: [80, 130, 216, 255], hairHi: [182, 214, 255, 255],
  dress: [58, 66, 110, 255], dressSh: [44, 50, 88, 255],
  apron: [248, 250, 255, 255], apronSh: [220, 227, 242, 255],
  ribbon: [92, 201, 246, 255], ribbonSh: [56, 164, 214, 255],
  shoe: [37, 42, 70, 255],
  eyeRim: [42, 54, 96, 255], iris: [80, 126, 220, 255], pupil: [26, 34, 58, 255],
  white: [255, 255, 255, 255], mouth: [214, 110, 122, 255],
};
const OUT = [46, 54, 92, 255];

// ---------------- character ----------------
function drawFace(p, P) {
  const eyes = [[78, 86], [114, 86]], opens = [P.eyeL, P.eyeR];
  for (let i = 0; i < 2; i++) {
    const ex = eyes[i][0], ey = eyes[i][1], o = opens[i];
    if (o < 0.22) { p.fill(cap(ex - 8, ey, ex + 8, ey, 3.2), C.eyeRim); continue; }
    const rx = 9.5, ry = 11 * o, pdx = P.pupilDx, pdy = P.pupilDy;
    p.fill(ell(ex, ey, rx + 1.4, ry + 1.4), C.eyeRim);
    p.fill(ell(ex, ey, rx, ry), C.iris);
    p.fill(ell(ex + pdx * 0.9, ey + pdy * 0.9, rx * 0.5, ry * 0.62), C.pupil);
    p.fill(ell(ex + pdx * 0.9 - 3.2, ey + pdy * 0.9 - 4, 3, 3.4), C.white);
    p.fill(ell(ex + pdx * 0.9 + 3, ey + pdy * 0.9 + 4, 1.6, 1.8), [255, 255, 255, 190]);
  }
  const bs = P.browSad;
  p.fill(cap(70, 68 + bs * 4, 86, 65 + bs * 6, 3), C.hairSh);
  p.fill(cap(106, 65 + bs * 6, 122, 68 + bs * 4, 3), C.hairSh);
  if (P.blush > 0) {
    p.fill(ell(67, 99, 10, 5), [255, 150, 170, Math.round(95 * P.blush)]);
    p.fill(ell(125, 99, 10, 5), [255, 150, 170, Math.round(95 * P.blush)]);
  }
  const y = 104, m = P.mouth;
  if (m === 'open') { p.fill(ell(96, y, 4.5, 5.5), C.mouth); p.fill(ell(96, y + 1.5, 3, 3), [176, 68, 88, 255]); }
  else if (m === 'flat') { p.fill(cap(90, y, 102, y, 2.4), C.mouth); }
  else if (m === 'frown') { p.fill(cap(90, y + 3, 96, y, 2.4), C.mouth); p.fill(cap(96, y, 102, y + 3, 2.4), C.mouth); }
  else if (m === 'o') { p.fill(ell(96, y, 3, 4), C.mouth); }
  else { p.fill(cap(90, y, 96, y + 3.5, 2.4), C.mouth); p.fill(cap(96, y + 3.5, 102, y, 2.4), C.mouth); }
}

function drawHairFront(p) {
  p.fill({ test: (x, y) => y <= 66 && (((x - 96) / 45) ** 2 + ((y - 68) / 40) ** 2 <= 1), bbox: [50, 26, 142, 108] }, C.hair);
  // highlight lens
  p.fill({ test: (x, y) => y >= 40 && y <= 57 && (((x - 96) / 34) ** 2 + ((y - 58) / 22) ** 2 <= 1), bbox: [58, 38, 134, 58] }, C.hairHi);
  // bang locks
  const locks = [[64, 78, 22], [78, 74, 18], [96, 84, 20], [114, 74, 18], [128, 78, 22]];
  for (const [lx, endY, w] of locks) {
    const topY = 46, midY = (topY + endY) / 2, side = lx < 96 ? -1 : 1;
    p.fill(taper([[lx, topY], [lx + side * 2, midY], [lx + side * 3, endY]], [w, w * 0.75, 5]), C.hair);
  }
  // side locks
  p.fill(taper([[58, 60], [54, 88], [58, 120]], [18, 15, 7]), C.hair);
  p.fill(taper([[134, 60], [138, 88], [134, 120]], [18, 15, 7]), C.hair);
}

function drawHeaddress(p) {
  const pts = [], wids = [];
  for (let i = 0; i <= 6; i++) { const t = i / 6; pts.push([60 + t * 72, 50 - Math.sin(t * Math.PI) * 13]); wids.push(11); }
  p.fill(taper(pts, wids), C.apron);
  for (let i = 0; i <= 8; i++) { const t = i / 8; const x = 58 + t * 76, y = 52 - Math.sin(t * Math.PI) * 13 + 5; p.fill(ell(x, y, 4.5, 4.5), C.white); }
  p.fill(ell(88, 35, 9, 7, 0.5), C.ribbon); p.fill(ell(104, 35, 9, 7, -0.5), C.ribbon);
  p.fill(ell(96, 36, 4.5, 4.5), C.ribbonSh);
}

function drawTail(p, x, y, dir, sway) {
  const pts = [], wids = [];
  for (let i = 0; i <= 5; i++) { const t = i / 5; pts.push([x + dir * (t * 14 + Math.sin(t * 2.2) * 5) + sway * t * 5, y + t * 66]); wids.push(20 - 12 * t); }
  p.fill(taper(pts, wids, 1), OUT);
  p.fill(taper(pts, wids, 0), C.hair);
  p.fill(ell(x, y + 3, 7, 7), OUT);
  p.fill(ell(x, y + 3, 6, 6), C.ribbon);
}

function drawArm(p, sh, hand) {
  const mx = (sh[0] + hand[0]) / 2, my = (sh[1] + hand[1]) / 2;
  const ex = mx + (mx < 96 ? -6 : 6), ey = my + 2;
  const pts = [[sh[0], sh[1]], [ex, ey], [hand[0], hand[1]]];
  p.fill(taper(pts, [15, 13, 11], 1), OUT);
  p.fill(taper(pts, [13, 11, 9], 0), C.skin);
  p.fill(ell(hand[0], hand[1], 7.5, 7.5), OUT);
  p.fill(ell(hand[0], hand[1], 6.5, 6.5), C.skin);
}

function drawLeg(p, hip, foot) {
  const knee = [(hip[0] + foot[0]) / 2 + (foot[0] > 96 ? 1 : -1), (hip[1] + foot[1]) / 2];
  const pts = [hip, knee, [foot[0], foot[1] - 4]];
  p.fill(taper(pts, [15, 13, 10], 1), OUT);
  p.fill(taper(pts, [13, 11, 8], 0), C.skin);
  p.fill(cap(foot[0], foot[1] - 16, foot[0], foot[1] - 6, 12), C.apron);
  p.fill(ell(foot[0], foot[1] + 2, 12, 7), OUT);
  p.fill(ell(foot[0], foot[1] + 2, 11, 6), C.shoe);
}

function drawCharacter(p, P) {
  let core = mul(mT(96, 196), mul(mR(P.lean), mul(mS(1, P.scaleY), mT(-96, -196))));
  if (P.flip) core = mul(core, mul(mT(96, 0), mul(mS(-1, 1), mT(-96, 0))));
  const Mbody = mul(core, mT(P.dx, P.dy));
  p.set(Mbody);

  const bx = 96 + P.headDx, by = 80 + P.headDy;
  p.fill(ell(bx, by, 49, 52), OUT);
  p.fill(ell(bx, by, 47, 50), C.hairSh);
  drawTail(p, bx - 40, by + 4, -1, P.tailSway);
  drawTail(p, bx + 40, by + 4, +1, -P.tailSway);

  drawLeg(p, [85, 152], P.footL);
  drawLeg(p, [107, 152], P.footR);

  p.fill(cap(96, 98, 96, 128, 15), C.skinSh);

  p.fill(poly([[78, 142], [114, 142], [132, 186], [96, 190], [60, 186]]), OUT);
  p.fill(poly([[78, 144], [114, 144], [130, 184], [96, 188], [62, 184]]), C.dress);
  p.fill(poly([[86, 146], [106, 146], [114, 184], [96, 186], [78, 184]]), C.apron);
  for (let i = 0; i < 5; i++) { const t = i / 4; p.fill(ell(80 + t * 32, 182 + Math.sin(t * Math.PI) * 3, 5, 4), C.apronSh); }

  p.fill(poly([[79, 118], [113, 118], [115, 148], [77, 148]]), OUT);
  p.fill(poly([[81, 120], [111, 120], [113, 146], [79, 146]]), C.dress);
  p.fill(rrect(96, 133, 28, 30, 5), C.apron);
  p.fill(ell(96, 120, 20, 7), C.apronSh);

  p.fill(ell(75, 126, 13, 12), OUT); p.fill(ell(75, 126, 12, 11), C.dress);
  p.fill(ell(117, 126, 13, 12), OUT); p.fill(ell(117, 126, 12, 11), C.dress);

  drawArm(p, [80, 128], P.handL);
  drawArm(p, [112, 128], P.handR);

  if (P.extra === 'magnifier') {
    const h = P.handR, mx = h[0] + 8, my = h[1] - 12;
    p.fill(cap(mx + 10, my + 10, mx + 22, my + 21, 7), OUT);
    p.fill(cap(mx + 9, my + 9, mx + 21, my + 20, 4.5), [150, 110, 70, 255]);
    p.fill(ell(mx, my, 17, 17), OUT);
    p.fill(ell(mx, my, 14, 14), [196, 224, 248, 255]);
    p.fill(ell(mx - 4, my - 5, 5, 4, -0.5), [255, 255, 255, 200]);
  }

  const Mhead = mul(Mbody, mul(mT(P.headDx, P.headDy), mul(mT(96, 74), mul(mR(P.headRot), mT(-96, -74)))));
  p.set(Mhead);
  p.fill(ell(96, 74, 42, 44), OUT);
  p.fill(ell(96, 74, 40, 42), C.skin);
  drawFace(p, P);
  drawHairFront(p);
  drawHeaddress(p);
  if (P.extra === 'sweat') {
    const sx = 138, sy = 44 + P.sweatDy;
    p.fill(poly([[sx, sy - 10], [sx + 6, sy + 2], [sx, sy + 9], [sx - 6, sy + 2]]), [150, 210, 255, 240]);
  }
}

// ---------------- poses ----------------
function base() {
  return { dx: 0, dy: 0, lean: 0, flip: false, scaleY: 1, headDx: 0, headDy: 0, headRot: 0,
    eyeL: 1, eyeR: 1, pupilDx: 0, pupilDy: 0, browSad: 0, mouth: 'smile', blush: 1,
    handL: [64, 158], handR: [128, 158], footL: [84, 192], footR: [108, 192],
    tailSway: 0, extra: null, sweatDy: 0 };
}
const TAU = Math.PI * 2;

function poseFor(row, f) {
  const P = base();
  const T = FRAMES[row];
  const ph = (f / T) * TAU;
  if (row === 0) {                       // idle
    const b = Math.sin(ph);
    P.dy = -1.3 * b; P.headDy = -1.1 * b; P.lean = 0.012 * b; P.tailSway = 0.8 * b;
    P.handL = [64, 158 + 0.7 * b]; P.handR = [128, 158 + 0.7 * b];
    if (f === 4) { P.eyeL = P.eyeR = 0.12; }
    if (f === 1) { P.pupilDx = 1.5; }
  } else if (row === 1) {                // move right
    P.lean = 0.06; P.dy = -1.6 * Math.abs(Math.sin(ph)); P.headDx = 2; P.tailSway = -2.4;
    P.footL = [84 + 11 * Math.sin(ph), 192 - 7 * Math.max(0, Math.sin(ph))];
    P.footR = [108 + 11 * Math.sin(ph + Math.PI), 192 - 7 * Math.max(0, Math.sin(ph + Math.PI))];
    P.handL = [62 - 9 * Math.sin(ph), 156]; P.handR = [130 + 9 * Math.sin(ph), 156];
    P.mouth = 'smile';
  } else if (row === 2) {                // move left
    const q = poseFor(1, f); q.flip = true; Object.assign(P, q); P.tailSway = 2.4;
  } else if (row === 3) {                // wave
    P.lean = 0.03; P.mouth = 'open'; P.blush = 1.3; P.dy = -1.2 * Math.sin(ph);
    P.handL = [64, 158];
    const w = Math.sin(f * 0.9);
    P.handR = [140 + 6 * w, 96 + 8 * w];
    P.tailSway = 1.2 * Math.sin(ph);
  } else if (row === 4) {                // jump
    const seq = [[6, 1.06, 0.9, 'open'], [-6, 0.94, 1.0, 'open'], [-26, 1.0, 1.0, 'open'], [-12, 0.98, 1.0, 'smile'], [4, 1.08, 0.9, 'smile']];
    const s = seq[f];
    P.dy = s[0]; P.scaleY = s[1]; P.mouth = s[3]; P.tailSway = -1.5;
    if (f <= 1) { P.handL = [70, 150]; P.handR = [122, 150]; P.footL = [86, 190]; P.footR = [106, 190]; }
    else if (f === 2) { P.handL = [58, 112]; P.handR = [134, 112]; P.footL = [80, 178]; P.footR = [112, 178]; P.eyeL = P.eyeR = 1; }
    else if (f === 3) { P.handL = [56, 132]; P.handR = [136, 132]; P.footL = [82, 186]; P.footR = [110, 186]; }
    else { P.handL = [66, 156]; P.handR = [126, 156]; P.footL = [86, 192]; P.footR = [106, 192]; }
  } else if (row === 5) {                // failure
    P.lean = -0.015 * f; P.headDy = 0.9 * f; P.headRot = 0.03 * f + 0.02;
    P.eyeL = P.eyeR = f === 2 ? 1.15 : Math.max(0.18, 1 - 0.11 * f);
    P.browSad = Math.min(1, f / 5); P.mouth = 'frown'; P.blush = 0.5;
    P.dy = 0.55 * f; P.tailSway = -0.6 * f; P.pupilDy = 2;
    P.handL = [62, 158 + 1.4 * f]; P.handR = [130, 158 + 1.4 * f];
    P.footL = [84, 192]; P.footR = [108, 192];
    if (f >= 3) { P.extra = 'sweat'; P.sweatDy = (f - 3) * 3; }
  } else if (row === 6) {                // waiting
    P.lean = 0.04 * Math.sin(ph); P.headDx = 2.5 * Math.sin(ph); P.tailSway = 1.4 * Math.sin(ph);
    P.pupilDx = 4 * Math.sin(ph); P.mouth = 'flat';
    P.handL = [64, 158]; P.handR = [128, 158];
    P.footR = [108, 192 - (f % 2 === 0 ? 3 : 0)];
    P.headRot = 0.02 * Math.sin(ph);
  } else if (row === 7) {                // run
    P.lean = 0.13; P.dy = -2.6 * Math.abs(Math.sin(ph)); P.headDx = 3; P.tailSway = -4;
    P.footL = [84 + 15 * Math.sin(ph), 192 - 12 * Math.max(0, Math.sin(ph))];
    P.footR = [108 + 15 * Math.sin(ph + Math.PI), 192 - 12 * Math.max(0, Math.sin(ph + Math.PI))];
    P.handL = [60 - 13 * Math.sin(ph), 150]; P.handR = [132 + 13 * Math.sin(ph), 150];
    P.mouth = 'open'; P.blush = 1.2;
  } else if (row === 8) {                // review
    P.headRot = 0.10 + 0.05 * Math.sin(ph); P.headDy = 2.5; P.pupilDy = 3; P.pupilDx = 1.5 * Math.sin(ph * 2);
    P.mouth = 'flat'; P.handR = [114, 134]; P.handL = [72, 150]; P.extra = 'magnifier';
    P.lean = 0.02; P.dy = -0.8 * Math.abs(Math.sin(ph)); P.tailSway = 0.6 * Math.sin(ph);
  } else {                               // look rows 9 & 10
    const idx = (row === 9 ? f : f + 8);
    const ang = (idx * 22.5) * Math.PI / 180;
    const dx = Math.sin(ang), dy = -Math.cos(ang);
    P.pupilDx = 4.4 * dx; P.pupilDy = 4.4 * dy;
    P.headDx = 6 * dx; P.headDy = 5 * dy; P.headRot = 0.14 * dx;
    P.lean = 0.06 * dx; P.tailSway = 1.8 * dx;
    P.handL = [64 + 4 * dx, 158]; P.handR = [128 + 4 * dx, 158];
    P.dy = -0.5;
  }
  return P;
}

// ---------------- render ----------------
function renderCell(img, col, row, P) {
  const surf = new Surf();
  const p = { set: (M) => surf.set(M), fill: (s, c) => surf.fill(s.test, s.bbox, c) };
  drawCharacter(p, P);
  const sw = CW * SS;
  const ox = col * CW, oy = row * CH, n = SS * SS;
  for (let y = 0; y < CH; y++) {
    for (let x = 0; x < CW; x++) {
      let r = 0, g = 0, b = 0, a = 0;
      for (let sy = 0; sy < SS; sy++) {
        for (let sx = 0; sx < SS; sx++) {
          const i = ((y * SS + sy) * sw + (x * SS + sx)) * 4;
          r += surf.b[i]; g += surf.b[i + 1]; b += surf.b[i + 2]; a += surf.b[i + 3];
        }
      }
      r /= n; g /= n; b /= n; a /= n;
      const o = ((oy + y) * W + (ox + x)) * 4;
      if (a <= 0.5) { img[o] = 0; img[o + 1] = 0; img[o + 2] = 0; img[o + 3] = 0; continue; }
      const af = a / 255;
      img[o] = r / af; img[o + 1] = g / af; img[o + 2] = b / af; img[o + 3] = Math.round(a);
    }
  }
}

// ---------------- PNG ----------------
function crc32(buf) {
  let crc = 0xFFFFFFFF;
  for (let n = 0; n < buf.length; n++) {
    crc ^= buf[n];
    for (let k = 0; k < 8; k++) crc = (crc >>> 1) ^ (0xEDB88320 & -(crc & 1));
  }
  return (crc ^ 0xFFFFFFFF) >>> 0;
}
function chunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length, 0);
  const t = Buffer.from(type, 'latin1');
  const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(Buffer.concat([t, data])), 0);
  return Buffer.concat([len, t, data, crc]);
}
function encodePNG(w, h, rgba) {
  const raw = Buffer.alloc((w * 4 + 1) * h);
  for (let y = 0; y < h; y++) {
    const off = y * (w * 4 + 1); raw[off] = 0;
    for (let i = 0; i < w * 4; i++) raw[off + 1 + i] = rgba[y * w * 4 + i];
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4);
  ihdr[8] = 8; ihdr[9] = 6; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A]),
    chunk('IHDR', ihdr), chunk('IDAT', deflateSync(raw, { level: 9 })), chunk('IEND', Buffer.alloc(0)),
  ]);
}

// ---------------- main ----------------
const outDir = process.argv[2] || '.';
mkdirSync(outDir, { recursive: true });
const img = new Uint8ClampedArray(W * H * 4);
for (let row = 0; row < ROWS; row++) {
  for (let col = 0; col < FRAMES[row]; col++) renderCell(img, col, row, poseFor(row, col));
}
writeFileSync(outDir + '/spritesheet.png', encodePNG(W, H, img));
if (process.argv.includes('--rows')) {
  for (let row = 0; row < ROWS; row++) {
    const crop = new Uint8ClampedArray(W * CH * 4);
    crop.set(img.subarray(row * CH * W * 4, (row + 1) * CH * W * 4));
    writeFileSync(outDir + '/row' + String(row).padStart(2, '0') + '.png', encodePNG(W, CH, crop));
  }
}
console.log('wrote ' + outDir + '/spritesheet.png ' + W + 'x' + H);
