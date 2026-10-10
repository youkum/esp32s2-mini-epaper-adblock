// AdBlock DNS sinkhole + four-colour e-paper dashboard
//
// ESP32-S2 mini, 768x552 JDY79668 panel.
//
// The DNS engine is 9M2PJU's ESP32-C3 AdBlocker: sorted 40-bit FNV-1a
// hashes in flash, binary-searched, with a RAM Bloom pre-filter.
// The renderer is InkSight's e-paper driver and font.
//
// Everything InkSight did over the network (modes, LLM content, the
// cloud backend) is gone. The panel now shows what the sinkhole itself
// counted: blocked queries today, block rate, a per-hour histogram and
// the most-blocked domains.

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WiFiServer.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <Update.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoOTA.h>
#include <time.h>
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "esp_heap_caps.h"
#include "esp_system.h"        // esp_reset_reason()
#include "esp_wifi.h"          // esp_wifi_get_config(), for the NVS credentials fallback

#include "config.h"
#include "display.h"
#include "dashboard.h"
#include "epd_driver.h"
#include "tracker.h"
#include "secrets.h"
#include "dashboard_html.h"

// ---- config ----
static const IPAddress UPSTREAM(9, 9, 9, 9);
static const uint16_t DNS_PORT = 53;
static const char* BLOCKLIST_PATH = "/blocklist.bin";
static const int HASH_BYTES = 5;
static const uint64_t HASH_MASK = (1ULL << (HASH_BYTES * 8)) - 1;
static const int DNS_BUF_SIZE = 1232;
static const int MAX_PENDING = 32;
static const int MAX_CLIENTS = 96;
static const int MAX_CUSTOM = 200;
static const int MAX_BAN = 32;
static const uint32_t UPSTREAM_TIMEOUT_MS = 2000;
static const uint32_t REMOTE_FETCH_IDLE_MS = 3000;
static const char* DASHBOARD_ETAG = "adblk-console-v2";

// ---- logging ----
#ifdef NDEBUG
#define LOG(...) ((void)0)
#define LOGN(...) ((void)0)
#else
#define LOG(...) Serial.printf(__VA_ARGS__)
#define LOGN(...) Serial.println(__VA_ARGS__)
#endif

// ---- banded canvas ----
// colorBuf points at the band epd_driver is currently streaming; the
// drawing helpers in display.cpp clip to it, so the 105,984-byte full
// canvas is never allocated anywhere.
uint8_t* colorBuf = nullptr;

// ---- forward declarations ----
WebServer web(80);
static const char* COLLECTED_HEADERS[] = {"Authorization", "If-None-Match", "Origin", "Referer"};
String updateUrl, updateStatus = "never";
#define DASH_SETSTATUS(s) do { updateStatus = (s); } while (0)
uint32_t updateIntervalH = 24, lastCheckMs = 0;
static uint32_t lastDnsActivityMs = 0;
static bool fetchBlocklist(String url);

// Captured once in setup(); reported in the heartbeat so a spontaneous
// reboot can be told apart from a flash/OTA reset without a UART probe.
static uint8_t g_bootReason = 0;

// The RTC timer keeps running across a software reset, so millis() can start
// in the thousands of seconds after a reset or an OTA. Uptime is therefore
// reported relative to the first millis() seen in setup().
static uint32_t g_bootMillis = 0;

uint32_t deviceUptimeSec() { return (millis() - g_bootMillis) / 1000; }
const char* deviceIpStr() {
  static char s[20];
  if (WiFi.status() != WL_CONNECTED) { snprintf(s, sizeof(s), "offline"); return s; }
  snprintf(s, sizeof(s), "%s", WiFi.localIP().toString().c_str());
  return s;
}

// ---- hashing ----
static uint64_t fnv64(const char* s, size_t n) {
  uint64_t h = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 0x100000001b3ULL; }
  return h;
}
static inline uint64_t fnv40(const char* s, size_t n) { return fnv64(s, n) & HASH_MASK; }

// ---- blocklist (flash hash table + RAM bloom filter) ----
static const uint8_t BL_MAGIC[4] = {'C','A','D','B'};
static const uint8_t BL_VERSION = 1;
static const int BL_HEADER_SIZE = 20;

File blocklist;
uint32_t numHashes = 0;
uint32_t hashTableOffset = 0;

uint8_t* bloomFilter = nullptr;
uint32_t bloomBits = 0;
uint8_t bloomK = 0;

static bool bloomMayContain(uint64_t h64) {
  if (!bloomFilter || bloomBits == 0 || bloomK == 0) return true;
  uint32_t h1 = (uint32_t)(h64 & 0xFFFFFFFF);
  uint32_t h2 = (uint32_t)(h64 >> 32);
  if (h2 == 0) h2 = 0xFFFFFFFF;
  for (uint8_t i = 0; i < bloomK; i++) {
    uint32_t pos = (h1 + i * h2) % bloomBits;
    if (!(bloomFilter[pos >> 3] & (1 << (pos & 7)))) return false;
  }
  return true;
}

static bool inFlash(uint64_t h) {
  if (!blocklist || numHashes == 0) return false;
  int32_t lo = 0, hi = (int32_t)numHashes - 1;
  uint8_t b[HASH_BYTES];
  while (lo <= hi) {
    int32_t mid = (lo + hi) >> 1;
    blocklist.seek(hashTableOffset + (uint32_t)mid * HASH_BYTES);
    blocklist.read(b, HASH_BYTES);
    uint64_t v = 0;
    for (int k = 0; k < HASH_BYTES; k++) v |= (uint64_t)b[k] << (8 * k);
    if (v < h) lo = mid + 1;
    else if (v > h) hi = mid - 1;
    else return true;
  }
  return false;
}

// ---- custom domains ----
String customDom[MAX_CUSTOM];
uint64_t customHash[MAX_CUSTOM];
int numCustom = 0;

static void sortCustom() {
  for (int i = 1; i < numCustom; i++) {
    uint64_t h = customHash[i]; String d = customDom[i];
    int j = i - 1;
    while (j >= 0 && customHash[j] > h) { customHash[j+1] = customHash[j]; customDom[j+1] = customDom[j]; j--; }
    customHash[j+1] = h; customDom[j+1] = d;
  }
}
static bool inCustom(uint64_t h) {
  int lo = 0, hi = numCustom - 1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1;
    if (customHash[mid] < h) lo = mid + 1;
    else if (customHash[mid] > h) hi = mid - 1;
    else return true;
  }
  return false;
}

static bool isBlocked(const char* domain, size_t dlen) {
  const char* p = domain;
  size_t remaining = dlen;
  while (remaining > 0) {
    uint64_t h64 = fnv64(p, remaining);
    uint64_t h40 = h64 & HASH_MASK;
    bool checkFlash = bloomMayContain(h64);
    if (checkFlash && (inFlash(h40) || inCustom(h40))) return true;
    const char* dot = (const char*)memchr(p, '.', remaining);
    if (!dot) break;
    size_t consumed = (size_t)(dot - p) + 1;
    remaining -= consumed;
    p = dot + 1;
    if (!memchr(p, '.', remaining)) break;
  }
  return false;
}
// ---- persistence: custom domains ----
static void loadCustom() {
  numCustom = 0;
  File f = LittleFS.open("/custom.txt", "r");
  if (!f) return;
  while (f.available() && numCustom < MAX_CUSTOM) {
    String l = f.readStringUntil('\n'); l.trim(); l.toLowerCase();
    if (l.length() && l.indexOf('.') > 0) {
      customDom[numCustom] = l;
      customHash[numCustom] = fnv40(l.c_str(), l.length());
      numCustom++;
    }
  }
  f.close();
  sortCustom();
}
static void saveCustom() {
  File f = LittleFS.open("/custom.txt", "w");
  if (!f) return;
  for (int i = 0; i < numCustom; i++) f.println(customDom[i]);
  f.close();
}
static bool addCustom(String d) {
  d.trim(); d.toLowerCase();
  if (d.startsWith("www.")) d = d.substring(4);
  if (!d.length() || d.indexOf('.') < 0 || numCustom >= MAX_CUSTOM) return false;
  for (int i = 0; i < numCustom; i++) if (customDom[i] == d) return false;
  customDom[numCustom] = d;
  customHash[numCustom] = fnv40(d.c_str(), d.length());
  numCustom++;
  sortCustom();
  saveCustom();
  return true;
}
static void removeCustom(String d) {
  d.toLowerCase();
  if (d.startsWith("www.")) d = d.substring(4);
  for (int i = 0; i < numCustom; i++) if (customDom[i] == d) {
    for (int j = i; j < numCustom - 1; j++) { customDom[j] = customDom[j+1]; customHash[j] = customHash[j+1]; }
    numCustom--;
    saveCustom();
    return;
  }
}

// ---- banned IPs ----
uint32_t bannedIP[MAX_BAN]; int numBanned = 0;
static bool isBannedIP(uint32_t ip) { for (int i = 0; i < numBanned; i++) if (bannedIP[i] == ip) return true; return false; }
static bool addBannedIP(uint32_t ip) {
  if (isBannedIP(ip) || numBanned >= MAX_BAN) return false;
  bannedIP[numBanned++] = ip;
  return true;
}
static bool removeBannedIP(uint32_t ip) {
  for (int i = 0; i < numBanned; i++) if (bannedIP[i] == ip) {
    for (int j = i; j < numBanned - 1; j++) bannedIP[j] = bannedIP[j+1];
    numBanned--;
    return true;
  }
  return false;
}
static void loadBanned() {
  numBanned = 0;
  File f = LittleFS.open("/banned.txt", "r");
  if (!f) return;
  while (f.available() && numBanned < MAX_BAN) {
    String l = f.readStringUntil('\n'); l.trim();
    IPAddress ip;
    if (l.length() && ip.fromString(l)) bannedIP[numBanned++] = (uint32_t)ip;
  }
  f.close();
}
static void saveBanned() {
  File f = LittleFS.open("/banned.txt", "w");
  if (!f) return;
  for (int i = 0; i < numBanned; i++) { IPAddress ip(bannedIP[i]); f.println(ip.toString()); }
  f.close();
}

// ---- client table ----
struct Dev { uint32_t ip; uint8_t mac[6]; uint32_t blocked, allowed, lastSeen; bool banned; };
Dev clients[MAX_CLIENTS]; int numClients = 0;
uint32_t totalBlocked = 0, totalAllowed = 0;

static void getMac(uint32_t ip, uint8_t* mac) {
  memset(mac, 0, 6);
  ip4_addr_t ipa; ipa.addr = ip;
  struct eth_addr* eth = nullptr; const ip4_addr_t* ipret = nullptr;
  for (struct netif* nif = netif_list; nif; nif = nif->next)
    if (etharp_find_addr(nif, &ipa, &eth, &ipret) >= 0 && eth) { memcpy(mac, eth->addr, 6); return; }
}
static Dev* getClient(uint32_t ip) {
  for (int i = 0; i < numClients; i++)
    if (clients[i].ip == ip) { clients[i].lastSeen = millis(); return &clients[i]; }
  int slot = numClients;
  if (numClients >= MAX_CLIENTS) {
    int oldest = 0;
    for (int i = 1; i < numClients; i++)
      if (clients[i].lastSeen < clients[oldest].lastSeen) oldest = i;
    slot = oldest;
  } else {
    numClients++;
  }
  Dev* c = &clients[slot];
  c->ip = ip; c->blocked = c->allowed = 0; c->lastSeen = millis();
  c->banned = isBannedIP(ip);
  getMac(ip, c->mac);
  return c;
}

// ---- DNS parsing ----
static size_t parseQuery(const uint8_t* pkt, int len, char* out, uint16_t* qtype, int* qend) {
  if (len < 13) return 0;
  int i = 12; size_t o = 0;
  while (i < len) {
    uint8_t l = pkt[i++];
    if (l == 0) break;
    if (l & 0xC0) return 0;
    if (o + l + 1 >= 250 || i + l > len) return 0;
    if (o) out[o++] = '.';
    for (uint8_t k = 0; k < l; k++) out[o++] = tolower(pkt[i++]);
  }
  out[o] = 0;
  if (i + 4 > len) return 0;
  *qtype = (pkt[i] << 8) | pkt[i + 1];
  *qend = i + 4;
  if (o > 4 && strncmp(out, "www.", 4) == 0) { memmove(out, out + 4, o - 3); o -= 4; }
  return o;
}

static int buildBlocked(uint8_t* pkt, int qend, uint16_t qtype) {
  pkt[2] = 0x81; pkt[3] = 0x80;
  pkt[6] = 0; pkt[7] = (qtype == 1 || qtype == 28) ? 1 : 0;
  pkt[8] = 0; pkt[9] = 0; pkt[10] = 0; pkt[11] = 0;
  if (qtype == 1) {
    const uint8_t ans[] = {0xC0,0x0C, 0,1, 0,1, 0,0,1,0x2C, 0,4, 0,0,0,0};
    memcpy(pkt + qend, ans, sizeof(ans));
    return qend + sizeof(ans);
  }
  if (qtype == 28) {
    const uint8_t ans[] = {0xC0,0x0C, 0,28, 0,1, 0,0,1,0x2C, 0,16, 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
    memcpy(pkt + qend, ans, sizeof(ans));
    return qend + sizeof(ans);
  }
  return qend;
}
// ---- async DNS forwarding ----
WiFiUDP dnsServer;
WiFiUDP upstream;
WiFiUDP tcpUpstream;

struct Pending {
  uint32_t clientIP;
  uint16_t clientPort;
  uint16_t originalID;
  uint32_t sentMs;
  bool active;
};
Pending pending[MAX_PENDING];
int pendingCount = 0;

static int findFreeSlot() {
  for (int i = 0; i < MAX_PENDING; i++) if (!pending[i].active) return i;
  return -1;
}
static void expirePending() {
  uint32_t now = millis();
  for (int i = 0; i < MAX_PENDING; i++)
    if (pending[i].active && (now - pending[i].sentMs) > UPSTREAM_TIMEOUT_MS) {
      pending[i].active = false;
      pendingCount--;
    }
}
static void pollUpstream(uint8_t* buf, int bufSize) {
  int sz = upstream.parsePacket();
  if (sz <= 0) return;
  int rlen = upstream.read(buf, bufSize);
  if (rlen < 2) return;
  uint16_t replyID = (buf[0] << 8) | buf[1];
  if (replyID >= MAX_PENDING || !pending[replyID].active) return;
  buf[0] = (pending[replyID].originalID >> 8) & 0xFF;
  buf[1] = pending[replyID].originalID & 0xFF;
  dnsServer.beginPacket(pending[replyID].clientIP, pending[replyID].clientPort);
  dnsServer.write(buf, rlen);
  dnsServer.endPacket();
  pending[replyID].active = false;
  pendingCount--;
}

static uint8_t dnsBuf[DNS_BUF_SIZE];

static void handleDns() {
  int sz = dnsServer.parsePacket();
  if (sz <= 0) return;
  lastDnsActivityMs = millis();
  IPAddress cip = dnsServer.remoteIP();
  uint16_t cport = dnsServer.remotePort();
  int qlen = dnsServer.read(dnsBuf, sizeof(dnsBuf));
  if (qlen < 13) return;

  char domain[256]; uint16_t qtype = 0; int qend = qlen;
  size_t dl = parseQuery(dnsBuf, qlen, domain, &qtype, &qend);

  Dev* c = getClient((uint32_t)cip);
  bool ban = c && c->banned;
  bool blocked = ban || (dl && numHashes && isBlocked(domain, dl));

  if (blocked) {
    int rlen = buildBlocked(dnsBuf, qend, qtype);
    totalBlocked++;
    if (c) c->blocked++;
    dnsServer.beginPacket(cip, cport);
    dnsServer.write(dnsBuf, rlen);
    dnsServer.endPacket();
  } else {
    int slot = findFreeSlot();
    if (slot < 0) return;
    uint16_t origID = (dnsBuf[0] << 8) | dnsBuf[1];
    dnsBuf[0] = (slot >> 8) & 0xFF;
    dnsBuf[1] = slot & 0xFF;
    pending[slot].clientIP = (uint32_t)cip;
    pending[slot].clientPort = cport;
    pending[slot].originalID = origID;
    pending[slot].sentMs = millis();
    pending[slot].active = true;
    pendingCount++;
    upstream.beginPacket(UPSTREAM, 53);
    upstream.write(dnsBuf, qlen);
    upstream.endPacket();
    totalAllowed++;
    if (c) c->allowed++;
  }

  // Feed the dashboard. Only name-bearing queries are counted, so a
  // malformed packet cannot skew the statistics.
  if (dl) trackerRecord(domain, blocked);
}

// ---- TCP DNS ----
WiFiServer tcpDnsServer(DNS_PORT);
WiFiClient tcpDnsClient;
static uint8_t tcpBuf[DNS_BUF_SIZE];

static int forwardUpstreamSync(uint8_t* pkt, int qlen) {
  uint16_t origID = (pkt[0] << 8) | pkt[1];
  pkt[0] = 0xFF; pkt[1] = 0xFE;
  tcpUpstream.beginPacket(UPSTREAM, 53);
  tcpUpstream.write(pkt, qlen);
  tcpUpstream.endPacket();
  pkt[0] = (origID >> 8) & 0xFF; pkt[1] = origID & 0xFF;
  uint32_t t0 = millis();
  while (millis() - t0 < UPSTREAM_TIMEOUT_MS) {
    int sz = tcpUpstream.parsePacket();
    if (sz > 0) {
      int n = tcpUpstream.read(pkt, DNS_BUF_SIZE);
      if (n >= 2) {
        pkt[0] = (origID >> 8) & 0xFF; pkt[1] = origID & 0xFF;
        return n;
      }
      return 0;
    }
    delay(1);
  }
  return 0;
}

static void handleTcpDns() {
  if (!tcpDnsClient || !tcpDnsClient.connected()) {
    tcpDnsClient = tcpDnsServer.accept();
    if (!tcpDnsClient) return;
  }
  if (!tcpDnsClient.connected()) return;
  if (tcpDnsClient.available() < 2) return;

  uint8_t lenBuf[2];
  tcpDnsClient.read(lenBuf, 2);
  uint16_t msgLen = (lenBuf[0] << 8) | lenBuf[1];
  if (msgLen > DNS_BUF_SIZE) { tcpDnsClient.stop(); return; }

  int total = 0;
  uint32_t t0 = millis();
  while (total < msgLen && tcpDnsClient.connected()) {
    if (tcpDnsClient.available()) {
      int n = tcpDnsClient.read(tcpBuf + total, msgLen - total);
      if (n <= 0) break;
      total += n;
    } else {
      if (millis() - t0 > UPSTREAM_TIMEOUT_MS) break;
      delay(1);
    }
  }
  if (total < msgLen) { tcpDnsClient.stop(); return; }

  char domain[256]; uint16_t qtype = 0; int qend = total;
  size_t dl = parseQuery(tcpBuf, total, domain, &qtype, &qend);
  Dev* c = getClient((uint32_t)tcpDnsClient.remoteIP());
  bool ban = c && c->banned;
  bool blocked = ban || (dl && numHashes && isBlocked(domain, dl));

  int rlen;
  if (blocked) {
    rlen = buildBlocked(tcpBuf, qend, qtype);
    totalBlocked++; if (c) c->blocked++;
  } else {
    rlen = forwardUpstreamSync(tcpBuf, total);
    totalAllowed++; if (c) c->allowed++;
  }
  if (dl) trackerRecord(domain, blocked);

  if (rlen > 0) {
    uint8_t lenOut[2] = { (uint8_t)(rlen >> 8), (uint8_t)(rlen & 0xFF) };
    tcpDnsClient.write(lenOut, 2);
    tcpDnsClient.write(tcpBuf, rlen);
  }
  tcpDnsClient.stop();
}
// ---- auth (Basic Auth + CSRF token) ----
String authPassword;
String authToken;

static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static String b64encode(const String& s) {
  String o;
  const uint8_t* in = (const uint8_t*)s.c_str();
  int len = s.length();
  for (int i = 0; i < len; i += 3) {
    uint32_t n = (uint32_t)in[i] << 16;
    if (i + 1 < len) n |= (uint32_t)in[i+1] << 8;
    if (i + 2 < len) n |= in[i+2];
    o += B64[(n >> 18) & 0x3F];
    o += B64[(n >> 12) & 0x3F];
    o += (i + 1 < len) ? B64[(n >> 6) & 0x3F] : '=';
    o += (i + 2 < len) ? B64[n & 0x3F] : '=';
  }
  return o;
}
static void genRandomPassword() {
  const char chars[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghjkmnpqrstuvwxyz23456789";
  authPassword = "";
  for (int i = 0; i < 10; i++) authPassword += chars[esp_random() % (sizeof(chars) - 1)];
  authToken = String((uint32_t)(esp_random() & 0xFFFFFF), HEX);
}
static void loadAuth() {
  File f = LittleFS.open("/auth.cfg", "r");
  if (f) {
    authPassword = f.readStringUntil('\n'); authPassword.trim();
    authToken = f.readStringUntil('\n'); authToken.trim();
    f.close();
  }
  if (authPassword.length() == 0) {
    genRandomPassword();
    File fo = LittleFS.open("/auth.cfg", "w");
    if (fo) { fo.println(authPassword); fo.println(authToken); fo.close(); }
    LOG("[AUTH] created admin account: user admin password %s\n", authPassword.c_str());
  } else {
    // Printed on every boot on purpose. The console is behind Basic Auth, so
    // a forgotten password would otherwise mean erasing /auth.cfg over
    // serial; the port is already physical access to the device.
    LOG("[AUTH] admin account: user admin password %s\n", authPassword.c_str());
  }
}
static bool checkAuth() {
  String expected = "Basic " + b64encode("admin:" + authPassword);
  return web.header("Authorization") == expected;
}
static bool checkToken() {
  return web.hasArg("token") && web.arg("token") == authToken;
}
static void requireAuth() {
  web.sendHeader("WWW-Authenticate", "Basic realm=\"AdBlock\"");
  web.send(401, "text/plain", "auth required");
}

// ---- web server ----
static String macStr(const uint8_t* m) {
  char s[18]; snprintf(s, sizeof(s), "%02x:%02x:%02x:%02x:%02x:%02x", m[0],m[1],m[2],m[3],m[4],m[5]);
  return String(s);
}
static String jesc(const String& s) {
  String o;
  for (char ch : s) { if (ch == '"' || ch == '\\') o += '\\'; o += ch; }
  return o;
}

// True while the device sits in AP mode waiting for WiFi credentials. The
// dashboard is behind Basic Auth, but a device with no network cannot be
// reached at a known address, so the setup page has to stay open.
static bool portalMode = false;

static void servePortal() {
  web.send(200, "text/html; charset=utf-8", R"PORTAL(<!doctype html><html lang=zh-CN><head>
<meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>AdBlock DNS 配网</title><style>
body{margin:0;min-height:100vh;display:grid;place-items:center;background:#0b0e13;color:#e7eef8;
font:14px/1.6 -apple-system,"PingFang SC","Microsoft YaHei",sans-serif}
.c{width:min(92vw,380px);background:#141922;border:1px solid #242e3c;border-radius:18px;padding:26px 24px}
h1{margin:0 0 6px;font-size:19px}p{color:#8697ab;margin:0 0 18px;font-size:13px}
label{display:block;font-size:12px;color:#8697ab;margin:14px 0 6px}
input{width:100%;box-sizing:border-box;background:#1b2330;border:1px solid #242e3c;color:#e7eef8;
border-radius:10px;padding:11px 12px;font:inherit}
button{width:100%;margin-top:20px;background:#4d8dff;border:0;color:#fff;font:inherit;font-weight:600;
border-radius:10px;padding:12px;cursor:pointer}
</style></head><body><div class="c"><h1>AdBlock DNS</h1>
<p>设备还没有连上网络，请填写它要加入的 WiFi。</p>
<form method=POST action=/setwifi>
<label>WiFi 名称</label><input name=ssid required>
<label>WiFi 密码</label><input name=pass type=password>
<button>保存并重启</button></form></div></body></html>)PORTAL");
}
static void handleRoot() {
  if (portalMode) { servePortal(); return; }
  if (!checkAuth()) { requireAuth(); return; }
  if (web.header("If-None-Match") == DASHBOARD_ETAG) { web.send(304); return; }
  web.sendHeader("Cache-Control", "max-age=3600");
  web.sendHeader("ETag", DASHBOARD_ETAG);
  web.send_P(200, "text/html", PAGE);
}

static void handleStats() {
  if (!checkAuth()) { requireAuth(); return; }
  uint32_t up = deviceUptimeSec();
  char ut[24];
  snprintf(ut, sizeof(ut), "%lud %luh %lum", up/86400, (up%86400)/3600, (up%3600)/60);
  String bloomStr = bloomFilter ? (String(bloomBits / 8 / 1024) + " KB") : String("off");

  String j = "{\"ip\":\"" + WiFi.localIP().toString() + "\",\"blocked\":" + totalBlocked +
             ",\"allowed\":" + totalAllowed + ",\"todayBlocked\":" + trackerTodayBlocked() +
             ",\"todayAllowed\":" + trackerTodayTotal() + ",\"domains\":" + numHashes +
             ",\"rssi\":" + WiFi.RSSI() + ",\"temp\":" + String(temperatureRead(), 1) +
             ",\"heap\":" + ESP.getFreeHeap() + ",\"uptime\":\"" + ut + "\"" +
             ",\"bloom\":\"" + jesc(bloomStr) + "\",\"pending\":" + pendingCount +
             ",\"wifi\":\"" + jesc(WiFi.SSID()) + "\"" +
             ",\"token\":\"" + jesc(authToken) + "\"" +
             ",\"upurl\":\"" + jesc(updateUrl) + "\",\"upiv\":" + updateIntervalH +
             ",\"upstat\":\"" + jesc(updateStatus) + "\",\"top\":[";

  TopEntry t[TOPN_SLOTS];
  trackerGetTop(t, TOPN_SLOTS);
  for (int i = 0; i < TOPN_SLOTS; i++) {
    if (!t[i].domain[0]) continue;
    j += (i ? "," : "");
    j += "{\"d\":\"" + jesc(String(t[i].domain)) + "\",\"n\":" + t[i].count + "}";
  }
  j += "],\"hours\":[";
  for (int i = 0; i < HIST_SLOTS; i++) { j += (i ? "," : ""); j += String(trackerHourBlocked(i)); }
  j += "],\"clients\":[";

  for (int i = 0; i < numClients; i++) {
    Dev& c = clients[i]; IPAddress ip(c.ip);
    j += (i ? "," : "");
    j += "{\"ip\":\"" + ip.toString() + "\",\"mac\":\"" + macStr(c.mac) +
         "\",\"blocked\":" + c.blocked + ",\"allowed\":" + c.allowed +
         ",\"banned\":" + (c.banned ? "true" : "false") + "}";
  }
  j += "],\"custom\":[";
  for (int i = 0; i < numCustom; i++) { j += (i ? "," : ""); j += "\"" + jesc(customDom[i]) + "\""; }
  j += "]}";
  web.send(200, "application/json", j);
}

// Force a panel redraw, so the screen can be refreshed on demand
// without waiting out the refresh interval.
static void handleRefreshScreen() {
  if (!checkAuth()) { requireAuth(); return; }
  if (!checkToken()) { web.send(403, "text/plain", "bad token"); return; }
  web.send(200, "text/plain", "refreshing");
  delay(100);
  dashboardDraw();
}

static void handleBan() {
  if (!checkAuth()) { requireAuth(); return; }
  if (!checkToken()) { web.send(403, "text/plain", "bad token"); return; }
  IPAddress ip;
  if (ip.fromString(web.arg("ip"))) {
    uint32_t ipraw = (uint32_t)ip;
    Dev* c = getClient(ipraw);
    if (c) {
      c->banned = !c->banned;
      if (c->banned) addBannedIP(ipraw); else removeBannedIP(ipraw);
      saveBanned();
    }
  }
  web.send(200, "text/plain", "ok");
}
static void handleAddBlock() {
  if (!checkAuth()) { requireAuth(); return; }
  if (!checkToken()) { web.send(403, "text/plain", "bad token"); return; }
  addCustom(web.arg("d"));
  web.send(200, "text/plain", "ok");
}
static void handleUnblock() {
  if (!checkAuth()) { requireAuth(); return; }
  if (!checkToken()) { web.send(403, "text/plain", "bad token"); return; }
  removeCustom(web.arg("d"));
  web.send(200, "text/plain", "ok");
}
// ---- remote blocklist config (persisted in /update.cfg) ----
// The URL has to serve a file already in this firmware's binary format, so
// in practice it points at a copy of a blocklist.bin the user hosts
// somewhere. Uploading the .bin through the console covers the rest.
static void saveUpdateCfg() {
  File f = LittleFS.open("/update.cfg", "w");
  if (f) { f.println(updateUrl); f.println((unsigned long)updateIntervalH); f.close(); }
}
static void loadUpdateCfg() {
  File f = LittleFS.open("/update.cfg", "r");
  if (!f) return;
  updateUrl = f.readStringUntil('\n'); updateUrl.trim();
  long iv = f.readStringUntil('\n').toInt();
  if (iv >= 0) updateIntervalH = (uint32_t)iv;
  f.close();
  if (updateUrl.length() > 0) {
    LOG("[UPD] auto-update every %lu h from %s\n",
        (unsigned long)updateIntervalH, updateUrl.c_str());
  }
}
static void handleSetUpdate() {
  if (!checkAuth()) { requireAuth(); return; }
  if (!checkToken()) { web.send(403, "text/plain", "bad token"); return; }
  updateUrl = web.arg("url"); updateUrl.trim();
  long iv = web.arg("iv").toInt();
  updateIntervalH = (iv < 0) ? 0 : (uint32_t)iv;
  saveUpdateCfg();
  web.send(200, "text/plain", "ok");
}

static void handleSetWifi() {
  if (!portalMode) {
    if (!checkAuth()) { requireAuth(); return; }
    if (!checkToken()) { web.send(403, "text/plain", "bad token"); return; }
  }
  String ssid = web.arg("ssid");
  String pass = web.arg("pass");
  if (ssid.length() == 0) { web.send(400, "text/plain", "missing ssid"); return; }
  File f = LittleFS.open("/wifi.cfg", "w");
  if (f) { f.println(ssid); f.println(pass); f.close(); }
  web.send(200, "text/plain", "saved - rebooting...");
  delay(500);
  ESP.restart();
}
// ---- blocklist loading ----
static bool loadBlocklist() {
  if (blocklist) blocklist.close();
  if (!LittleFS.exists(BLOCKLIST_PATH)) {
    LOGN("[BL] no blocklist file in the filesystem");
    numHashes = 0; return false;
  }
  blocklist = LittleFS.open(BLOCKLIST_PATH, "r");
  if (!blocklist) { LOGN("[BL] open failed"); numHashes = 0; return false; }
  LOG("[BL] file is %u bytes\n", (unsigned)blocklist.size());

  uint8_t hdr[BL_HEADER_SIZE];
  if (blocklist.read(hdr, BL_HEADER_SIZE) != BL_HEADER_SIZE) {
    LOGN("[BL] header read short");
    blocklist.close(); numHashes = 0; return false;
  }
  if (memcmp(hdr, BL_MAGIC, 4) != 0 || hdr[4] != BL_VERSION || hdr[5] != HASH_BYTES) {
    LOGN("[BL] bad header"); blocklist.close(); numHashes = 0; return false;
  }
  numHashes = hdr[6] | (hdr[7] << 8) | ((uint32_t)hdr[8] << 16) | ((uint32_t)hdr[9] << 24);
  bloomBits = hdr[10] | (hdr[11] << 8) | ((uint32_t)hdr[12] << 16) | ((uint32_t)hdr[13] << 24);
  bloomK = hdr[14];

  uint32_t bloomBytes = (bloomBits + 7) / 8;
  if (bloomBits > 0 && (bloomK == 0 || bloomBytes > (4u << 20))) {
    LOGN("[BL] bad bloom header");
    blocklist.close(); numHashes = 0; bloomBits = 0; bloomK = 0;
    return false;
  }
  if (bloomFilter) { free(bloomFilter); bloomFilter = nullptr; }
  if (bloomBits > 0) {
    // 128 KB of randomly-addressed table: PSRAM is the right home for it.
    // DRAM is only 176 KB total and WiFi wants most of it.
    // PSRAM only, on purpose. A DRAM fallback would pull 128 KB out of the
    // 176 KB internal pool that WiFi, lwIP and the web server all share, and
    // the device panics as soon as the network stack comes up.
    bloomFilter = (uint8_t*)heap_caps_malloc(bloomBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (bloomFilter) {
      uint32_t rd = blocklist.read(bloomFilter, bloomBytes);
      if (rd != bloomBytes) { free(bloomFilter); bloomFilter = nullptr; }
    }
    if (!bloomFilter) {
      // No room for the pre-filter: every lookup now goes to flash, which
      // is slower but still correct. bloomMayContain() fails open.
      LOG("[BL] bloom %u KB not in PSRAM - flash-only lookup\n",
          (unsigned)(bloomBytes >> 10));
    }
  }
  // The hash table sits after the Bloom filter whether or not we managed to
  // keep a copy of it in RAM, so this offset never depends on the alloc.
  hashTableOffset = BL_HEADER_SIZE + bloomBytes;
  LOG("[BL] %u domains, bloom %u KB\n", numHashes, bloomFilter ? bloomBytes / 1024 : 0);
  return numHashes > 0;
}

struct BlocklistInfo { uint32_t count; uint32_t size; time_t mtime; bool loaded; };
static BlocklistInfo blocklistInfo() {
  BlocklistInfo info = { numHashes, 0, 0, blocklist || numHashes > 0 };
  File f = LittleFS.open(BLOCKLIST_PATH, "r");
  if (f) {
    size_t sz = f.size();
    time_t t = 0;
    f.seek(0);
    info.size = (uint32_t)sz;
    info.loaded = sz > BL_HEADER_SIZE;
  }
  return info;
}

// ---- remote blocklist fetch ----
static bool fetchBlocklist(String url) {
  if (url.length() == 0) return false;
  web.send(200, "text/plain", "fetching...");
  delay(50);

  bool https = url.startsWith("https://");
  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure secure;
  secure.setCACert(nullptr);  // rely on the embedded bundle

  bool begun = https ? http.begin(secure, url) : http.begin(plain, url);
  if (!begun) { DASH_SETSTATUS("bad url"); return false; }
  http.setTimeout(HTTP_TIMEOUT_MS);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int code = http.GET();
  if (code != 200) {
    http.end();
    DASH_SETSTATUS("http " + String(code));
    return false;
  }

  File tmp = LittleFS.open("/blocklist.tmp", "w");
  if (!tmp) { http.end(); DASH_SETSTATUS("fs full"); return false; }

  uint8_t hdr[4];
  http.getStream().readBytes(hdr, 4);
  if (memcmp(hdr, BL_MAGIC, 4) != 0) {
    tmp.close(); LittleFS.remove("/blocklist.tmp");
    http.end(); DASH_SETSTATUS("bad magic");
    return false;
  }
  tmp.write(hdr, 4);
  uint8_t buf[1024];
  WiFiClient* stream = http.getStreamPtr();
  int remaining = http.getSize();
  int written = 4;
  uint32_t lastData = millis();
  while (http.connected() && (remaining < 0 || written < remaining)) {
    size_t avail = stream->available();
    if (avail) {
      int n = stream->read(buf, sizeof(buf));
      if (n > 0) { tmp.write(buf, n); written += n; remaining -= n; lastData = millis(); }
    } else {
      if (millis() - lastData > REMOTE_FETCH_IDLE_MS) break;
      delay(10);
    }
  }
  tmp.close();
  http.end();

  if (written < BL_HEADER_SIZE) {
    LittleFS.remove("/blocklist.tmp");
    DASH_SETSTATUS("short file");
    return false;
  }
  LittleFS.remove(BLOCKLIST_PATH);
  LittleFS.rename("/blocklist.tmp", BLOCKLIST_PATH);
  if (!loadBlocklist()) { DASH_SETSTATUS("load failed"); return false; }
  DASH_SETSTATUS("never");
  return true;
}
// ---- config portal (AP mode) ----
static void startConfigPortal() {
  portalMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID_DEFAULT);
  dashboardMessage("配网模式", AP_SSID_DEFAULT, "浏览器打开 192.168.4.1");
  LOG("[SETUP] AP %s open at http://%s/\n",
      AP_SSID_DEFAULT, WiFi.softAPIP().toString().c_str());
}

// Leave the portal and go back to being a station. Called once the setup
// form has been saved, so the board does not sit in AP mode serving a form
// nobody is looking at while the DNS engine is what the user actually
// wanted. The credentials are already in /wifi.cfg at this point.
static void stopConfigPortal() {
  portalMode = false;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  LOG("[SETUP] portal closed, rejoining as a station\n");
}

// ---- OTA ----
static void handleUpdate() {
  if (!checkAuth()) { requireAuth(); return; }
  HTTPUpload& up = web.upload();
  if (up.status == UPLOAD_FILE_START) {
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_END) {
    if (Update.end(true)) Serial.println("OTA ok");
  }
}
static void handleUpdateDone() {
  if (!checkAuth()) { requireAuth(); return; }
  web.send(200, "text/plain", Update.hasError() ? "fail" : "ok - rebooting");
  delay(500);
  ESP.restart();
}
static void handleBlocklistUpload() {
  if (!checkAuth()) { requireAuth(); return; }
  HTTPUpload& up = web.upload();
  if (up.status == UPLOAD_FILE_START) {
    LittleFS.remove(BLOCKLIST_PATH);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    File f = LittleFS.open(BLOCKLIST_PATH, "a");
    if (f) { f.write(up.buf, up.currentSize); f.close(); }
  } else if (up.status == UPLOAD_FILE_END) {
    loadBlocklist();
  }
}
static void handleBlocklistDone() {
  if (!checkAuth()) { requireAuth(); return; }
  web.send(200, "text/plain", "ok");
}

// ---- WiFi ----
// The compile-time values in secrets.h are still just a placeholder until
// someone fills them in, so treat the shipped strings as "not configured".
static bool isPlaceholderSecret(const char* s) {
  return !s || !*s || strcmp(s, "your-ssid") == 0 || strcmp(s, "your-password") == 0;
}

static bool connectWifi(uint32_t timeoutMs) {
  String ssid, pass;
  const char* source = nullptr;

  File f = LittleFS.open("/wifi.cfg", "r");
  if (f) {
    ssid = f.readStringUntil('\n'); ssid.trim();
    pass = f.readStringUntil('\n'); pass.trim();
    f.close();
    if (ssid.length() > 0) source = "from /wifi.cfg";
  }
  if (!source && !isPlaceholderSecret(WIFI_SSID)) {
    ssid = WIFI_SSID; pass = WIFI_PASS; source = "from secrets.h";
  }

  WiFi.mode(WIFI_STA);

  if (source) {
    LOG("[WIFI] ssid '%s' (%s), starting\n", ssid.c_str(), source);
    WiFi.begin(ssid.c_str(), pass.c_str());
  } else {
    // Nothing configured here, so fall back to the WiFi driver's own NVS
    // store. This is not a corner case: the panel this firmware came from
    // saved its network through its web app, so a board that has already
    // been in service joins on its own with no setup at all.
    wifi_config_t stored;
    const bool have = (esp_wifi_get_config(WIFI_IF_STA, &stored) == ESP_OK) &&
                      stored.sta.ssid[0] != 0;
    if (!have) {
      LOGN("[WIFI] no credentials in /wifi.cfg, secrets.h or NVS");
      return false;
    }
    LOG("[WIFI] no /wifi.cfg, trying the NVS network '%s'\n", (const char*)stored.sta.ssid);
    WiFi.begin();
  }
  uint32_t t0 = millis();
  uint32_t lastReport = 0;
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) {
    delay(100);
    digitalWrite(PIN_LED, (millis() / 500) % 2);
    if (millis() - t0 - lastReport >= 2000) {
      lastReport = millis() - t0;
      LOG("[WIFI] status %d at %us\n", (int)WiFi.status(), lastReport / 1000);
    }
  }
  digitalWrite(PIN_LED, LOW);
  return WiFi.status() == WL_CONNECTED;
}
// ---- setup / loop ----
static uint32_t lastDashMs = 0;
static uint32_t lastPeriodicPullMs = 0;
static bool epdReady = false;

void setup() {
  Serial.begin(115200);
  g_bootReason = (uint8_t)esp_reset_reason();
  g_bootMillis = millis();
  // The board's only console is the native USB port, and TinyUSB discards
  // anything written before the host attaches the CDC. Wait for it briefly
  // (bounded, so a standalone battery boot is not held up).
  uint32_t serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < 5000) delay(10);
  Serial.printf("AdBlock EPD booting (reset=%u heap=%u psram=%u)\n",
                (unsigned)g_bootReason, (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getFreePsram());

  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_CFG_BTN, INPUT_PULLUP);


  // Mount without formatting first: LittleFS.begin(true) formats whenever the
  // mount fails, which silently erases a freshly flashed blocklist.
  if (!LittleFS.begin(false, "/littlefs", 10, LFS_PARTITION)) {
    LOGN("[FS] mount failed - formatting");
    if (!LittleFS.begin(true, "/littlefs", 10, LFS_PARTITION))
      LOGN("[FS] format failed - no persistence this boot");
  }
  LOG("[FS] mounted total=%u used=%u heap=%u\n", (unsigned)LittleFS.totalBytes(),
      (unsigned)LittleFS.usedBytes(), (unsigned)ESP.getFreeHeap());
  Serial.flush();

  gpioInit();
  epdInit();
  epdReady = true;

  trackerInit();
  trackerLoad();
  loadCustom();
  loadBanned();
  loadAuth();
  loadUpdateCfg();
  LOG("[FS] persistence loaded heap=%u psram=%u\n", (unsigned)ESP.getFreeHeap(),
      (unsigned)ESP.getFreePsram());
  Serial.flush();

  loadBlocklist();
  LOG("[BL] step done hashes=%u bloom=%u heap=%u psram=%u\n", (unsigned)numHashes,
      (unsigned)(bloomFilter != nullptr), (unsigned)ESP.getFreeHeap(),
      (unsigned)ESP.getFreePsram());
  Serial.flush();

  // On-device check that the flash hash table answers correctly. The host
  // tests cover the builder; this covers the firmware's own suffix walk,
  // Bloom pre-filter and binary search against the real flashed image.
  {
    static const char* kBlocked[] = {"doubleclick.net", "ads.doubleclick.net",
                                     "googleadservices.com", "scorecardresearch.com"};
    static const char* kClean[]   = {"example.com", "github.com", "cloudflare.com"};
    int hit = 0, miss = 0;
    for (const char* d : kBlocked) {
      bool b = isBlocked(d, strlen(d));
      hit += b;
      LOG("[BL] check %s -> %s\n", d, b ? "BLOCKED" : "MISS");
    }
    for (const char* d : kClean) {
      bool b = isBlocked(d, strlen(d));
      miss += !b;
      LOG("[BL] check %s -> %s\n", d, b ? "BLOCKED (false positive)" : "clean");
    }
    LOG("[BL] selftest %d/%d blocked, %d/%d clean\n", hit, 4, miss, 3);
  }
  Serial.flush();

  LOGN("[BOOT] blocklist/persistence loaded, painting boot screen");
  dashboardMessage("启动中", "正在连接 WiFi", "");
  LOGN("[BOOT] boot screen painted, starting web server");
  LOG("[BOOT] heap after boot screen=%u\n", (unsigned)ESP.getFreeHeap());
  Serial.flush();

  // The network stack has to be initialised before anything opens a socket:
  // WiFiServer::begin() calls socket(), which posts to the lwIP tcpip thread,
  // and doing that before esp_netif_init() aborts in tcpip.c with
  // "assert failed: tcpip_send_msg_wait_sem (Invalid mbox)".
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);

  // Registered before the WiFi branch so the setup portal works too: a fresh
  // device has no /wifi.cfg, and the page served in AP mode is the only way
  // to give it one.
  web.on("/", handleRoot);
  web.on("/stats", handleStats);
  web.on("/refresh-screen", handleRefreshScreen);
  web.on("/ban", handleBan);
  web.on("/addblock", handleAddBlock);
  web.on("/unblock", handleUnblock);
  web.on("/setupdate", handleSetUpdate);
  web.on("/setwifi", HTTP_POST, handleSetWifi);
  web.on("/update", HTTP_POST, handleUpdateDone, handleUpdate);
  web.on("/blocklist-upload", HTTP_POST, handleBlocklistDone, handleBlocklistUpload);
  web.collectHeaders(COLLECTED_HEADERS, sizeof(COLLECTED_HEADERS) / sizeof(COLLECTED_HEADERS[0]));
  web.begin();
  LOGN("[BOOT] routes registered, connecting to WiFi");

  bool ok = connectWifi(WIFI_TIMEOUT);
  LOG("[WIFI] connect %s after %lus\n", ok ? "ok" : "failed",
      (unsigned long)deviceUptimeSec());
  if (!ok) {
    startConfigPortal();
  } else {
    configTime(NTP_UTC_OFFSET, 0, "pool.ntp.org", "time.nist.gov");
    MDNS.begin(DNS_HOSTNAME);
    MDNS.addService("http", "tcp", 80);

    dnsServer.begin(DNS_PORT);
    upstream.begin(0);
    tcpUpstream.begin(0);
    tcpDnsServer.begin();
    tcpDnsServer.setNoDelay(true);
    lastDnsActivityMs = millis();

    ArduinoOTA.setHostname(DNS_HOSTNAME);
    ArduinoOTA.begin();

    dashboardDraw();
  }
  lastDashMs = millis();
  lastPeriodicPullMs = millis();
}

void loop() {
  // Finish a panel refresh that is still in flight. The glass needs ~15 s
  // after a frame has been streamed, and this is where that time is spent
  // instead of inside the drawing call, so DNS keeps being answered.
  epdServiceRefresh();

  web.handleClient();
  ArduinoOTA.handle();

  if (WiFi.status() == WL_CONNECTED) {
    handleDns();
    pollUpstream(dnsBuf, sizeof(dnsBuf));
    handleTcpDns();
    expirePending();
  }

  uint32_t now = millis();

  // Rotate a daily summary once the clock has rolled over. `lastDay`
  // starts at -1 so the first tick after NTP lands only records the
  // date; otherwise the clock jumping from 1970 to today on the very
  // first sync would look like a rollover and wipe today's counts.
  static int lastDay = -1;
  struct tm tmNow;
  time_t t = time(nullptr);
  if (t > 1700000000 && localtime_r(&t, &tmNow)) {
    if (lastDay == -1) lastDay = tmNow.tm_mday;
    if (lastDay != -1 && lastDay != tmNow.tm_mday) {
      trackerRollDay();
      trackerSave();
    }
    lastDay = tmNow.tm_mday;
  }

  // Panel: full redraw on the interval, and immediately when the
  // config button is held (a manual "show me the numbers" gesture).
  if (now - lastDashMs >= DASH_REFRESH_MS) {
    dashboardDraw();
    lastDashMs = now;
  }
  if (digitalRead(PIN_CFG_BTN) == LOW) {
    delay(SHORT_PRESS_MIN_MS);
    if (digitalRead(PIN_CFG_BTN) == LOW) {
      // Hold length decides the action. Short: redraw the dashboard, a
      // manual "show me the numbers". Long: open the setup portal again -
      // the board's only route back to the WiFi form once it is out of
      // range of the network it was set up for.
      uint32_t heldMs = 0;
      bool longPress = false;
      while (digitalRead(PIN_CFG_BTN) == LOW) {
        delay(BTN_POLL_MS);
        heldMs += BTN_POLL_MS;
        if (!longPress && heldMs >= CFG_BTN_HOLD_MS) {
          longPress = true;
          break;
        }
      }
      if (longPress) {
        LOGN("[BTN] hold detected - opening the setup portal");
        startConfigPortal();
      } else {
        dashboardDraw();
        lastDashMs = now;
        while (digitalRead(PIN_CFG_BTN) == LOW) delay(10);
      }
    }
  }

  // Periodic blocklist pull, only once DNS has gone quiet.
  if (updateIntervalH > 0 && updateUrl.length() &&
      now - lastPeriodicPullMs >= updateIntervalH * 3600000UL &&
      now - lastDnsActivityMs > REMOTE_FETCH_IDLE_MS) {
    fetchBlocklist(updateUrl);
    lastPeriodicPullMs = now;
  }

  // A dropped link has to be picked up again or the device goes quiet:
  // the reconnect used to happen at exactly one place, before the first
  // pass through here, so every later disassociation (router reboot,
  // the AP it roamed to going away, the brief drop during a panel
  // refresh) left it stuck on "ip offline" until the next power cycle.
  // WL_CONNECTED only covers the first patch of the tree; every later
  // status means the station is down and should be restarted.
  //
  // The retries are also counted: a network the board cannot get back onto
  // (password changed, router replaced, board moved) would otherwise be
  // retried forever with no way for anyone to correct it from the device
  // itself. After WIFI_RETRY_BEFORE_PORTAL consecutive failures the board
  // gives up on the stored credentials and opens the setup portal, which
  // is what "let me reconnect the WiFi" has to mean when the person doing
  // the reconnecting is standing in front of the panel rather than
  // attached to a serial console.
  static uint32_t lastWifiRetryMs = 0;
  static uint8_t  wifiFailCount  = 0;
  if (!portalMode && WiFi.status() != WL_CONNECTED &&
      now - lastWifiRetryMs >= LIVE_WIFI_RETRY_MS) {
    lastWifiRetryMs = now;
    wifiFailCount++;
    LOG("[WIFI] link down (status %d), reconnect attempt %u/%u\n",
        (int)WiFi.status(), (unsigned)wifiFailCount, (unsigned)WIFI_RETRY_BEFORE_PORTAL);

    if (wifiFailCount >= WIFI_RETRY_BEFORE_PORTAL) {
      wifiFailCount = 0;
      LOGN("[WIFI] cannot reach the stored network - opening the setup portal");
      startConfigPortal();
    } else {
      // A full radio restart, not just an association retry: the
      // credentials are unchanged, so WiFi.reconnect() alone re-runs the
      // same handshake that has already failed N times.
      WiFi.disconnect(false, false);
      delay(100);
      connectWifi(WIFI_TIMEOUT);
    }
  }
  if (WiFi.status() == WL_CONNECTED) wifiFailCount = 0;

  // Heartbeat. There is exactly one console (the USB CDC port), and in AP
  // mode nothing else is logged, so a status line keeps the device
  // observable without attaching a debugger.
  static uint32_t lastBeatMs = 0;
  if (now - lastBeatMs >= 5000UL) {
    lastBeatMs = now;
    TopEntry top;
    trackerGetTop(&top, 1);
    LOG("[HB] up %lus ip %s wl %d boot %u dom %lu bl %uKB q %lu blk %lu/%lu top %s %lu heap %u ps %u\n",
        (unsigned long)deviceUptimeSec(), deviceIpStr(), (int)WiFi.status(),
        (unsigned)g_bootReason,
        (unsigned long)numHashes, (unsigned)(bloomBits / 8 / 1024),
        (unsigned long)trackerAllTimeTotal(), (unsigned long)trackerAllTimeBlocked(),
        (unsigned long)trackerTodayBlocked(), top.domain[0] ? top.domain : "-",
        (unsigned long)top.count, (unsigned)ESP.getFreeHeap(),
        (unsigned)ESP.getFreePsram());
  }

  static uint32_t lastSave = 0;
  if (now - lastSave >= 300000UL) { trackerSave(); lastSave = now; }
}