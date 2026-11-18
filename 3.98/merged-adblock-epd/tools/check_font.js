const fs=require('fs');
const path=require('path');
const src=file=>fs.readFileSync(path.join(__dirname,'..','src',file),'utf8');
const d=src('dashboard.cpp');
const m=src('main.cpp');
const f=src('cjk_font.cpp');
const have=new Set();
for(const x of f.matchAll(/\{\s*\"(.+?)\"\s*,\s*g\d+\s*\}/g)) for(const ch of x[1]) have.add(ch);
const want=new Map();
function scan(src){ for(const s of src.matchAll(/\"([^\"\\n]*[\u4e00-\u9fff][^\"\\n]*)\"/g)){ for(const ch of s[1]){ if(ch.codePointAt(0)>=0x4e00&&ch.codePointAt(0)<=0x9fff){ if(!want.has(ch)) want.set(ch,[]); want.get(ch).push(s[1]); } } } }
scan(d);
// main.cpp 中只有 dashboardMessage 的字面量直接绘制在面板上。
for (const call of m.matchAll(/\bdashboardMessage\([^;]*\);/g)) scan(call[0]);
const missing=[...want.keys()].filter(c=>!have.has(c));
console.log('font glyphs', have.size);
console.log('missing', JSON.stringify(missing));
console.log(missing.map(c=>c+' <- '+want.get(c).slice(0,3).join(' | ')).join('\n'));

if (missing.length) process.exitCode=1;
