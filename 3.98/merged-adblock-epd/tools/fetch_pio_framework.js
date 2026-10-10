#!/usr/bin/env node
// Fetch the espressif32 Arduino framework tarball straight into
// PlatformIO's download cache, over several parallel HTTP range
// connections.
//
// Why: dl.registry.platformio.org gives us ~36 KB/s per connection, so a
// single 246 MB download takes over an hour and frequently stalls.
// PlatformIO caches a download as .cache/downloads/<sha1(url+checksum)>
// and skips the transfer entirely when that file already exists, so
// filling the cache out-of-band is enough to make "pio run" proceed.
//
// Usage:
//   node fetch_pio_framework.js <url> <destPath> <sha256> <totalBytes> [connections]

const https = require("https");
const fs = require("fs");
const path = require("path");
const crypto = require("crypto");

const [url, destPath, sha256, totalStr, connStr] = process.argv.slice(2);
const total = parseInt(totalStr, 10);
const CONNS = parseInt(connStr || "8", 10);
const PART_DIR = destPath + ".parts";
const LOG = destPath + ".log";

fs.mkdirSync(PART_DIR, { recursive: true });

function log(m) {
  const line = "[" + new Date().toISOString().slice(11, 19) + "] " + m;
  console.log(line);
  fs.appendFileSync(LOG, line + "\n");
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// dl.registry.platformio.org answers with a 302 to object storage, so
// redirects have to be followed by hand; Range survives the hop.
function openRange(url, from, end, depth) {
  return new Promise((resolve) => {
    const req = https.get(url, { headers: { Range: "bytes=" + from + "-" + end } }, (res) => {
      if (res.statusCode >= 300 && res.statusCode < 400 && res.headers.location) {
        res.resume();
        if (depth <= 0) return resolve({ err: "too many redirects" });
        const next = new URL(res.headers.location, url).toString();
        return resolve(openRange(next, from, end, depth - 1));
      }
      if (res.statusCode !== 206) {
        res.resume();
        return resolve({ err: "status " + res.statusCode });
      }
      resolve({ res });
    });
    req.setTimeout(45000, () => req.destroy(new Error("idle timeout")));
    req.on("error", (e) => resolve({ err: e.message }));
  });
}

async function fetchRange(url, from, end, partPath) {
  const opened = await openRange(url, from, end, 5);
  if (opened.err) return { ok: false, reason: opened.err };
  const res = opened.res;
  return new Promise((resolve) => {
    let settled = false;
    const done = (ok, reason) => { if (!settled) { settled = true; resolve({ ok, reason }); } };
    const ws = fs.createWriteStream(partPath, { flags: "a" });
    res.pipe(ws);
    res.on("error", (e) => { ws.destroy(); done(false, "read " + e.message); });
    ws.on("error", (e) => { res.destroy(); done(false, "write " + e.message); });
    ws.on("finish", () => done(true, "ok"));
    res.on("aborted", () => { ws.destroy(); done(false, "aborted"); });
  });
}

async function fetchPart(i, start, end) {
  const partPath = path.join(PART_DIR, "part" + String(i).padStart(2, "0") + ".bin");
  const want = end - start + 1;
  for (let attempt = 1; attempt <= 80; attempt++) {
    let have = fs.existsSync(partPath) ? fs.statSync(partPath).size : 0;
    if (have > want) { fs.truncateSync(partPath, want); have = want; }
    if (have >= want) return true;
    const res = await fetchRange(url, start + have, end, partPath);
    const now = fs.existsSync(partPath) ? fs.statSync(partPath).size : 0;
    log("part" + String(i).padStart(2, "0") + " attempt " + attempt + " " + (res.ok ? "ok" : "fail(" + res.reason + ")") +
        " " + (now / 1048576).toFixed(2) + "/" + (want / 1048576).toFixed(2) + " MB");
    if (now >= want) return true;
    await sleep(res.ok ? 500 : 2500);
  }
  return false;
}

(async () => {
  log("start conns=" + CONNS + " total=" + (total / 1048576).toFixed(1) + " MB");
  const chunk = Math.ceil(total / CONNS);
  const jobs = [];
  for (let i = 0; i < CONNS; i++) {
    const start = i * chunk;
    if (start >= total) break;
    const end = Math.min(start + chunk - 1, total - 1);
    jobs.push(fetchPart(i, start, end));
  }
  const results = await Promise.all(jobs);
  if (results.some((r) => !r)) { log("ABORT: some parts incomplete"); process.exit(1); }

  const out = fs.openSync(destPath, "w");
  const hash = crypto.createHash("sha256");
  for (let i = 0; i < CONNS; i++) {
    const p = path.join(PART_DIR, "part" + String(i).padStart(2, "0") + ".bin");
    if (!fs.existsSync(p)) continue;
    const buf = fs.readFileSync(p);
    fs.writeSync(out, buf);
    hash.update(buf);
  }
  fs.closeSync(out);
  const got = hash.digest("hex");
  const size = fs.statSync(destPath).size;
  log("assembled " + size + " bytes, sha256 " + got);
  if (got !== sha256) { log("CHECKSUM MISMATCH, expected " + sha256); process.exit(2); }
  log("OK - cache primed at " + destPath);
})();
