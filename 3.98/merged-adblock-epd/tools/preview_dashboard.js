#!/usr/bin/env node
// Offline preview of the dashboard, rendered from the same font tables the
// firmware uses. It exists because the panel costs ~17 s per flash cycle and
// a wrong label is invisible until it is on glass.
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const SRC = path.join(__dirname, '..', 'src');
const W = 768, H = 552;

const BLACK = [0, 0, 0], WHITE = [255, 255, 255], RED = [190, 0, 0], YELLOW = [215, 165, 0];
const COL = { 0x00: BLACK, 0x01: WHITE, 0x02: RED, 0x03: YELLOW };

// ── parse the CJK table ──────────────────────────────────────
const cjkSrc = fs.readFileSync(path.join(SRC, 'cjk_font.cpp'), 'utf8');
const cjkBits = {};
for (const m of cjkSrc.matchAll(/static const uint8_t (g\d+)\[\] = \{([\s\S]*?)\};/g)) {
  cjkBits[m[1]] = m[2].split(',').map(s => parseInt(s.trim(), 16)).filter(v => !isNaN(v));
}
const cjk = {};
for (const m of cjkSrc.matchAll(/\{\s*"(.+?)"\s*,\s*(g\d+)\s*\}/g)) cjk[m[1]] = cjkBits[m[2]];

// ── parse the ASCII table ────────────────────────────────────
const asciiSrc = fs.readFileSync(path.join(SRC, 'display.cpp'), 'utf8');
const asciiBits = {};
for (const m of asciiSrc.matchAll(/static const uint8_t (g_[A-Za-z0-9]+)\[\]\s*=\s*\{([^}]*)\};/g))
  asciiBits[m[1]] = m[2].split(',').map(s => parseInt(s.trim(), 16)).filter(v => !isNaN(v));
const named = { ':': 'g_colon', '-': 'g_dash', '.': 'g_dot', '/': 'g_slash', '!': 'g_exclam', '%': 'g_pct', ',': 'g_comma', ' ': 'g_space' };
function getGlyph(c) {
  if (/[A-Za-z0-9]/.test(c)) return asciiBits['g_' + c];
  if (named[c]) return asciiBits[named[c]];
  return asciiBits.g_space;
}

// ── canvas ───────────────────────────────────────────────────
const buf = Buffer.alloc(W * H * 3, 0xFF);
function px(x, y, c) { if (x < 0 || x >= W || y < 0 || y >= H) return; const o = (y * W + x) * 3; buf[o] = c[0]; buf[o + 1] = c[1]; buf[o + 2] = c[2]; }
function fillRect(x, y, w, h, c) { for (let j = 0; j < h; j++) for (let i = 0; i < w; i++) px(x + i, y + j, c); }

// Mirrors display.cpp drawTextColor(): a line containing a full-width glyph is
// laid out on a CJK_GLYPH_H*scale box with the 5x7 ASCII face centred in it.
function drawText(msg, x, y, scale, c) {
  let asciiDy = 0;
  for (const ch of msg) if (ch.codePointAt(0) >= 0xE0) { asciiDy = Math.floor((16 - 7) * scale / 2); break; }
  let cx = x;
  for (const ch of msg) {
    let adv;
    if (ch.codePointAt(0) >= 0xE0) {
      const g = cjk[ch];
      if (g) for (let row = 0; row < 16; row++) { const hi = g[row * 2], lo = g[row * 2 + 1];
        for (let col = 0; col < 16; col++) { const bit = col < 8 ? (hi >> (7 - col)) : (lo >> (7 - (col - 8)));
          if (bit & 1) for (let dy = 0; dy < scale; dy++) for (let dx = 0; dx < scale; dx++) px(cx + col * scale + dx, y + row * scale + dy, c); } }
      adv = 16 * scale;
    } else {
      const g = getGlyph(ch);
      if (g) for (let col = 0; col < 5; col++) for (let row = 0; row < 7; row++)
        if (g[col] & (1 << row)) for (let dy = 0; dy < scale; dy++) for (let dx = 0; dx < scale; dx++) px(cx + col * scale + dx, y + asciiDy + row * scale + dy, c);
      adv = 6 * scale;
    }
    cx += adv;
  }
}
function textWidthUtf8(msg, scale) { let w = 0; for (const ch of msg) w += (ch.codePointAt(0) >= 0xE0 ? 16 : 6) * scale; return w - scale; }
function textWidth(n, scale) { return n * 6 * scale - scale; }
function textRight(msg, right, y, scale, c) { let x = right - textWidth(msg.length, scale); if (x < 0) x = 0; drawText(msg, x, y, scale, c); }
function textCentered(msg, y, scale, c) { let x = Math.floor((W - textWidthUtf8(msg, scale)) / 2); if (x < 2) x = 2; drawText(msg, x, y, scale, c); }
function sectionTitle(x, y, text) { fillRect(x, y + 8, 6, 16, BLACK); drawText(text, x + 16, y, 2, BLACK); }
function hRule(x, y, w) { fillRect(x, y, w, 2, BLACK); }

// ── layout constants, mirrored from src/dashboard.cpp ────────
const PAD = 24, HEADER_H = 48, COL_L_X = 24, COL_L_W = 320, COL_R_X = 400;
const COL_R_W = W - COL_R_X - PAD;
const SECTION_Y = 68, RULE_TOP_Y = 176, RULE_MID_Y = 278;
const LIST_TITLE_Y = 294, LIST_Y0 = 334, LIST_PITCH = 30, LIST_ROWS = 5;
const FOOTER_RULE_Y = 500, FOOTER_Y = 512;

// ── the dashboard, mirroring src/dashboard.cpp ───────────────
const IP = '192.168.10.190';
fillRect(0, 0, W, HEADER_H, BLACK);
drawText('ADBLOCK DNS', 20, 17, 2, WHITE);
textRight(IP, W - 20, 17, 2, WHITE);

sectionTitle(COL_L_X, SECTION_Y, '今日拦截');
drawText('1,234', COL_L_X, 102, 8, BLACK);
hRule(COL_L_X, RULE_TOP_Y, COL_L_W);
drawText('查询总数', COL_L_X, 190, 2, BLACK);
textRight('3702', COL_L_X + COL_L_W, 196, 3, BLACK);
drawText('拦截率', COL_L_X, 234, 2, BLACK);
textRight('33.3%', COL_L_X + COL_L_W, 240, 3, BLACK);

sectionTitle(COL_R_X, SECTION_Y, '每小时拦截');
const y0 = 104, h = 68, axisY = y0 + h;
hRule(COL_R_X, axisY, COL_R_W);
const hist = [0,0,0,0,0,0,0,1,2,0,0,3,5,4,2,7,9,6,4,8,3,1,0,0];
const peak = Math.max(...hist);
const slotW = Math.floor(COL_R_W / 24), gap = 3;
for (let i = 0; i < 24; i++) {
  const v = hist[i];
  let bh = Math.round(v * (h - 4) / peak);
  if (v > 0 && bh < 3) bh = 3;
  if (bh > 0) fillRect(COL_R_X + i * slotW, axisY - bh, slotW - gap, bh, BLACK);
}
drawText('00', COL_R_X, axisY + 6, 1, BLACK);
drawText('12', COL_R_X + Math.floor(COL_R_W / 2) - 5, axisY + 6, 1, BLACK);
textRight('23', COL_R_X + COL_R_W, axisY + 6, 1, BLACK);

hRule(COL_L_X, RULE_MID_Y, W - 2 * PAD);
sectionTitle(COL_L_X, LIST_TITLE_Y, '域名排行榜');
const top = [['doubleclick.net', 42], ['googleads.g.doubleclick.net', 17], ['ads.example.com', 15], ['tracker.net', 13], ['cdn.foo.co', 11],
             ['fonts.gstatic.com', 9], ['analytics.vendor.io', 8], ['scorecardresearch.com', 6], ['google-analytics.com', 5], ['adservice.google.com', 4]];
for (let i = 0; i < top.length; i++) {
  const col = Math.floor(i / LIST_ROWS), row = i % LIST_ROWS;
  const x0 = col ? COL_R_X : COL_L_X, w = col ? COL_R_W : COL_L_W;
  const y = LIST_Y0 + row * LIST_PITCH;
  drawText(String(i + 1), x0, y, 2, BLACK);
  const cnt = top[i][1].toLocaleString('en-US');
  textRight(cnt, x0 + w, y, 2, BLACK);
  let name = top[i][0];
  const nameX = x0 + 30;
  let maxChars = Math.floor((x0 + w - textWidth(cnt.length, 2) - 12 - nameX) / 12);
  if (maxChars < 4) maxChars = 4;
  if (name.length > maxChars) name = name.slice(0, maxChars);
  drawText(name, nameX, y, 2, BLACK);
}

hRule(COL_L_X, FOOTER_RULE_Y, W - 2 * PAD);
drawText('累计 123,456', COL_L_X, FOOTER_Y, 2, BLACK);
textCentered('运行 3天7时', FOOTER_Y, 2, BLACK);
const upd = '更新 21:15';
let ux = W - PAD - textWidthUtf8(upd, 2); if (ux < 500) ux = 500;
drawText(upd, ux, FOOTER_Y, 2, BLACK);

// ── PNG ──────────────────────────────────────────────────────
function chunk(type, data) {
  const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
  const body = Buffer.concat([Buffer.from(type, 'ascii'), data]);
  const crc = Buffer.alloc(4); crc.writeUInt32BE(zlib.crc32(body) >>> 0);
  return Buffer.concat([len, body, crc]);
}
const raw = Buffer.alloc(H * (W * 3 + 1));
for (let y = 0; y < H; y++) { raw[y * (W * 3 + 1)] = 0; buf.copy(raw, y * (W * 3 + 1) + 1, y * W * 3, (y + 1) * W * 3); }
const ihdr = Buffer.alloc(13);
ihdr.writeUInt32BE(W, 0); ihdr.writeUInt32BE(H, 4); ihdr[8] = 8; ihdr[9] = 2;
const png = Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
const out = path.join(__dirname, '..', 'preview-dashboard.png');
fs.writeFileSync(out, png);
console.log('wrote', out, png.length, 'bytes; cjk glyphs', Object.keys(cjk).length);
