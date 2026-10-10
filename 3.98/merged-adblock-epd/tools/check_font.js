const fs=require('fs');
const d=fs.readFileSync('src/dashboard.cpp','utf8');
const m=fs.readFileSync('src/main.cpp','utf8');
const f=fs.readFileSync('src/cjk_font.cpp','utf8');
const have=new Set();
for(const x of f.matchAll(/\{\s*\"(.+?)\"\s*,\s*g\d+\s*\}/g)) for(const ch of x[1]) have.add(ch);
const want=new Map();
function scan(src){ for(const s of src.matchAll(/\"([^\"\\n]*[\u4e00-\u9fff][^\"\\n]*)\"/g)){ for(const ch of s[1]){ if(ch.codePointAt(0)>=0x4e00&&ch.codePointAt(0)<=0x9fff){ if(!want.has(ch)) want.set(ch,[]); want.get(ch).push(s[1]); } } } }
scan(d); scan(m);
const missing=[...want.keys()].filter(c=>!have.has(c));
console.log('font glyphs', have.size);
console.log('missing', JSON.stringify(missing));
console.log(missing.map(c=>c+' <- '+want.get(c).slice(0,3).join(' | ')).join('\n'));
