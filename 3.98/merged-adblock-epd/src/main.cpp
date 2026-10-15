// AdBlock DNS sinkhole + 四色电子纸仪表盘
//
// ESP32-S2 mini，768x552 JDY79668 面板。
//
// DNS 引擎来自 9M2PJU 的 ESP32-C3 AdBlocker：flash 中存有排序好的
// 40-bit FNV-1a 哈希，二分查找，外加一个 RAM Bloom 预过滤器。
// 渲染部分来自 InkSight 的电子纸驱动与字体。
//
// InkSight 原来走网络的一切（各种模式、LLM 内容、云后端）都已移除。
// 面板现在展示的是 sinkhole 自己统计出来的数据：今日拦截的查询数、
// 拦截率、按小时的直方图以及被拦截最多的域名。

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
#include "esp_system.h"        // 提供 esp_reset_reason()
#include "esp_wifi.h"          // 提供 esp_wifi_get_config()，用于 NVS 凭据回退

#include "config.h"
#include "display.h"
#include "dashboard.h"
#include "epd_driver.h"
#include "tracker.h"
#include "secrets.h"
#include "dashboard_html.h"

// ---- 配置 ----
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
static const char* DASHBOARD_ETAG = "adblk-console-v3";

// ---- 日志 ----
#ifdef NDEBUG
#define LOG(...) ((void)0)
#define LOGN(...) ((void)0)
#else
#define LOG(...) Serial.printf(__VA_ARGS__)
#define LOGN(...) Serial.println(__VA_ARGS__)
#endif

// ---- 分带画布 ----
// colorBuf 指向 epd_driver 当前正在流式输出的那个分带；display.cpp 中的
// 绘图辅助函数都以它为裁剪边界，因此 105,984 字节的完整画布
// 在任何地方都不会被分配。
uint8_t* colorBuf = nullptr;

// ---- 前向声明 ----
WebServer web(80);
static const char* COLLECTED_HEADERS[] = {"Authorization", "If-None-Match", "Origin", "Referer"};
String updateUrl, updateStatus = "never";
#define DASH_SETSTATUS(s) do { updateStatus = (s); } while (0)
uint32_t updateIntervalH = 24, lastCheckMs = 0;
static uint32_t lastDnsActivityMs = 0;
static bool fetchBlocklist(String url);

// 在 setup() 中一次性捕获；随心跳上报，这样无需接 UART 探针就能把
// 自发重启与 flash/OTA 复位区分开来。
static uint8_t g_bootReason = 0;

// RTC 定时器在软件复位之后仍在继续计时，因此复位或 OTA 后
// millis() 的起点可能已经是几千秒。所以运行时长以 setup() 中
// 首次读到的 millis() 为基准上报。
static uint32_t g_bootMillis = 0;

uint32_t deviceUptimeSec() { return (millis() - g_bootMillis) / 1000; }
const char* deviceIpStr() {
  static char s[20];
  if (WiFi.status() != WL_CONNECTED) { snprintf(s, sizeof(s), "offline"); return s; }
  snprintf(s, sizeof(s), "%s", WiFi.localIP().toString().c_str());
  return s;
}

// ---- 哈希 ----
static uint64_t fnv64(const char* s, size_t n) {
  uint64_t h = 0xcbf29ce484222325ULL;
  for (size_t i = 0; i < n; i++) { h ^= (uint8_t)s[i]; h *= 0x100000001b3ULL; }
  return h;
}
static inline uint64_t fnv40(const char* s, size_t n) { return fnv64(s, n) & HASH_MASK; }

// ---- 黑名单（flash 哈希表 + RAM Bloom 过滤器）----
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

// ---- 自定义域名 ----
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
// ---- 持久化：自定义域名 ----
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

// ---- 已封禁 IP ----
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

// ---- 客户端表 ----
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

// ---- DNS 解析 ----
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
// ---- 异步 DNS 转发 ----
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

// 每轮在客户端套接字（handleDns）和上游套接字（pollUpstream）上各
// 处理一小批数据报。这并不会抬高突发上限：SDK 的 lwIP UDP 接收邮箱
// 只有 CONFIG_LWIP_UDP_RECVMBOX_SIZE（6）深，客户端一次性发出一整页
// 的查询就会把它灌满，多余的数据报在循环还没开始处理之前就被 lwIP
// 丢弃了——排空得再快也救不回已经丢掉的部分（实测：三种处理开销
// 截然不同的查询类型，20 个都同样只能到达约 6 个；参见 AGENTS.md
// 中的突发上限说明）。批量处理买到的是延迟：每一轮把当前排队的
// 全部清空，而不是 loop() 每转一圈只取一个数据报。上限保持得很小，
// 这样某个繁忙瞬间不会饿死共享这个 loop() 的 web 服务器、OTA 和
// 面板服务。
static const int DNS_DRAIN_MAX = 8;

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
  // 排空一小批：上游套接字与客户端套接字共用同一个深度为 6 的
  // lwIP 接收邮箱，因此当转发出去的一批一起返回时，每轮 loop()
  // 只处理一条应答就会把它灌满，多余的应答会被丢弃（客户端只能
  // 干等到超时）。与客户端排空一样设有上限，这样某个繁忙瞬间
  // 不会饿死 web 服务器、OTA 和面板服务。
  for (int i = 0; i < DNS_DRAIN_MAX; i++) {
    int sz = upstream.parsePacket();
    if (sz <= 0) return;
    int rlen = upstream.read(buf, bufSize);
    if (rlen < 2) continue;
    uint16_t replyID = (buf[0] << 8) | buf[1];
    if (replyID >= MAX_PENDING || !pending[replyID].active) continue;
    buf[0] = (pending[replyID].originalID >> 8) & 0xFF;
    buf[1] = pending[replyID].originalID & 0xFF;
    dnsServer.beginPacket(pending[replyID].clientIP, pending[replyID].clientPort);
    dnsServer.write(buf, rlen);
    dnsServer.endPacket();
    pending[replyID].active = false;
    pendingCount--;
  }
}

static uint8_t dnsBuf[DNS_BUF_SIZE];

// 只处理一个数据报。邮箱为空时返回 false。
static bool serveOneDns() {
  int sz = dnsServer.parsePacket();
  if (sz <= 0) return false;
  lastDnsActivityMs = millis();
  IPAddress cip = dnsServer.remoteIP();
  uint16_t cport = dnsServer.remotePort();
  int qlen = dnsServer.read(dnsBuf, sizeof(dnsBuf));
  if (qlen < 13) return true;

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
    if (slot < 0) return true;
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

  // 喂给仪表盘。只有带域名的查询才计入统计，这样畸形包就
  // 无法扭曲统计数字。
  if (dl) trackerRecord(domain, blocked);
  return true;
}

static void handleDns() {
  for (int i = 0; i < DNS_DRAIN_MAX; i++) {
    if (!serveOneDns()) break;
  }
}

// ---- TCP DNS ----
// 以非阻塞状态机方式服务。TCP DNS 客户端或缓慢的上游绝不能卡住
// loop()：过去全部内联应答的做法最多要耗费两个 UPSTREAM_TIMEOUT_MS
// 窗口的 delay(1) 空转——一个等请求体，一个等上游应答——期间面板
// 服务、web 控制台和 UDP DNS 全都冻结。现在这个处理器每轮只前进一步，
// 步与步之间让出控制权，与 epdServiceRefresh() 服务面板的方式完全一致。
//
// 一次只服务一条连接，且每条连接只承载一个查询，这与上一个实现的
// 行为相同（它在每次应答后都会调用 stop()）。被转发查询的上游应答
// 带着哨兵 ID 0xFFFE，它不会与 UDP 路径的 pending 槽位
// （0..MAX_PENDING-1）冲突，因为那条路径使用的是另一个套接字。
WiFiServer tcpDnsServer(DNS_PORT);
WiFiClient tcpDnsClient;
static uint8_t tcpBuf[DNS_BUF_SIZE];

enum TcpState { TCP_IDLE, TCP_READ, TCP_FORWARD };
static TcpState  tcpState    = TCP_IDLE;
static uint8_t   tcpLenBuf[2];
static int       tcpLenGot   = 0;
static uint16_t  tcpMsgLen   = 0;
static int       tcpGot      = 0;
static uint32_t  tcpDeadline = 0;
static uint16_t  tcpQueryID  = 0;

static void tcpDnsReset() {
  tcpDnsClient.stop();
  tcpState  = TCP_IDLE;
  tcpLenGot = 0;
  tcpMsgLen = 0;
  tcpGot    = 0;
}

// DNS-over-TCP 的每条报文在两个方向上都以 2 字节长度前缀封帧。
// 发送完响应后关闭连接。
static void tcpDnsReply(const uint8_t* msg, int len) {
  if (len > 0) {
    uint8_t lenOut[2] = { (uint8_t)(len >> 8), (uint8_t)(len & 0xFF) };
    tcpDnsClient.write(lenOut, 2);
    tcpDnsClient.write(msg, len);
  }
  tcpDnsReset();
}

static void handleTcpDns() {
  // 空闲：若有等待中的连接，就接手下一条。
  if (tcpState == TCP_IDLE) {
    if (!tcpDnsClient || !tcpDnsClient.connected()) {
      tcpDnsClient = tcpDnsServer.accept();
      if (!tcpDnsClient) return;
    }
    tcpState    = TCP_READ;
    tcpLenGot   = 0;
    tcpGot      = 0;
    tcpDeadline = millis() + UPSTREAM_TIMEOUT_MS;
  }

  if (!tcpDnsClient.connected()) { tcpDnsReset(); return; }

  if (tcpState == TCP_READ) {
    // 先读 2 字节长度前缀。
    while (tcpLenGot < 2) {
      int n = tcpDnsClient.read(tcpLenBuf + tcpLenGot, 2 - tcpLenGot);
      if (n <= 0) {
        if ((int32_t)(millis() - tcpDeadline) >= 0) tcpDnsReset();
        return;   // 暂无可读数据；把控制权让给 loop() 的其余部分
      }
      tcpLenGot += n;
    }
    tcpMsgLen = (tcpLenBuf[0] << 8) | tcpLenBuf[1];
    if (tcpMsgLen < 12 || tcpMsgLen > DNS_BUF_SIZE) { tcpDnsReset(); return; }

    // 报文体，读到刚取出的长度为止。
    while (tcpGot < tcpMsgLen) {
      int n = tcpDnsClient.read(tcpBuf + tcpGot, tcpMsgLen - tcpGot);
      if (n <= 0) {
        if ((int32_t)(millis() - tcpDeadline) >= 0) tcpDnsReset();
        return;   // 报文尚不完整；让出控制权，下一轮继续
      }
      tcpGot += n;
    }

    char domain[256]; uint16_t qtype = 0; int qend = tcpGot;
    size_t dl = parseQuery(tcpBuf, tcpGot, domain, &qtype, &qend);
    Dev* c = getClient((uint32_t)tcpDnsClient.remoteIP());
    bool ban = c && c->banned;
    bool blocked = ban || (dl && numHashes && isBlocked(domain, dl));

    if (blocked) {
      int rlen = buildBlocked(tcpBuf, qend, qtype);
      totalBlocked++; if (c) c->blocked++;
      if (dl) trackerRecord(domain, blocked);
      tcpDnsReply(tcpBuf, rlen);
      return;
    }

    // 转发：把 ID 改写成哨兵值，记住客户端原来的 ID，然后转入
    // 等待状态。应答会在之后的某一轮被取回。
    tcpQueryID = (tcpBuf[0] << 8) | tcpBuf[1];
    tcpBuf[0] = 0xFF; tcpBuf[1] = 0xFE;
    tcpUpstream.beginPacket(UPSTREAM, 53);
    tcpUpstream.write(tcpBuf, tcpGot);
    tcpUpstream.endPacket();
    totalAllowed++; if (c) c->allowed++;
    if (dl) trackerRecord(domain, blocked);
    tcpState    = TCP_FORWARD;
    tcpDeadline = millis() + UPSTREAM_TIMEOUT_MS;
    return;
  }

  // TCP_FORWARD：上游应答到达的是 tcpUpstream 而不是客户端套接字，
  // 因此轮询它并让出控制权，直到应答出现或超过截止时间。
  int sz = tcpUpstream.parsePacket();
  if (sz > 0) {
    int n = tcpUpstream.read(tcpBuf, DNS_BUF_SIZE);
    if (n >= 12) {
      tcpBuf[0] = (tcpQueryID >> 8) & 0xFF;
      tcpBuf[1] = tcpQueryID & 0xFF;
      tcpDnsReply(tcpBuf, n);
    } else {
      tcpDnsReset();
    }
    return;
  }
  if ((int32_t)(millis() - tcpDeadline) >= 0) tcpDnsReset();
}
// ---- 认证（Basic Auth + CSRF token）----
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
    // 有意每次开机都打印 admin 账号密码。控制台有 Basic Auth 把守，
    // 否则忘记密码就意味着要经串口擦除 /auth.cfg；而这个端口本身
    // 就等同于对设备的物理接触。
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

// 把管理员密码与 CSRF token 持久化到 /auth.cfg（第 1 行为密码，
// 第 2 行为 token）。用户在控制台里修改密码时调用，让新密码在
// 重启后依然有效。
static void saveAuth() {
  File f = LittleFS.open("/auth.cfg", "w");
  if (f) { f.println(authPassword); f.println(authToken); f.close(); }
}

// ---- web 服务器 ----
static String macStr(const uint8_t* m) {
  char s[18]; snprintf(s, sizeof(s), "%02x:%02x:%02x:%02x:%02x:%02x", m[0],m[1],m[2],m[3],m[4],m[5]);
  return String(s);
}
static String jesc(const String& s) {
  String o;
  for (char ch : s) { if (ch == '"' || ch == '\\') o += '\\'; o += ch; }
  return o;
}

// 设备处于 AP 模式、等待 WiFi 凭据时为 true。仪表盘有 Basic Auth
// 保护，但没有网络的设备无法通过已知地址访问到，因此配网页面
// 必须保持开放。
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

  // 通过拼接一百多个小片段构建（96 个客户端加 200 个自定义域名时
  // 体积可达约 17 KB）。用这种方式增长 String 会反复重新分配并拷贝，
  // 恰是造成 ESP32 堆碎片化的那种折腾。改为按实际计数一次性
  // 预留好容量。
  size_t est = 320 + (size_t)numClients * 96 + (size_t)numCustom * 34 +
               (size_t)TOPN_SLOTS * 48 + (size_t)HIST_SLOTS * 8;
  String j;
  j.reserve(est);
  j = "{\"ip\":\"" + WiFi.localIP().toString() + "\",\"blocked\":" + totalBlocked +
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

// 强制面板重绘，让屏幕能够按需刷新，而不必干等刷新间隔结束。
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
// ---- 远程黑名单配置（持久化在 /update.cfg）----
// 该 URL 必须返回一个已经是本固件二进制格式的文件，因此实际上它
// 指向用户托管在某处的一份 blocklist.bin 副本。通过控制台上传
// .bin 文件则覆盖其余情况。
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

// ---- 修改管理员密码 ----
// 由控制台调用，而控制台受 Basic Auth 保护：能走到这里的请求
// 已经用当前密码验证过了，因此无需再次提交旧密码。新密码直接
// 写入 /auth.cfg 并在下一次请求时生效——浏览器仍缓存着旧凭据，
// 会重新弹出认证提示。错误响应体保持 ASCII（与其他端点一致）；
// 控制台会把它们转成中文 toast，因此不需要为此准备面板字形。
static void handleSetPass() {
  if (!checkAuth()) { requireAuth(); return; }
  if (!checkToken()) { web.send(403, "text/plain", "bad token"); return; }
  String np = web.arg("new");
  if (np.length() < 6 || np.length() > 64) {
    web.send(400, "text/plain", "length");
    return;
  }
  // 保持 /auth.cfg 恰好是一行有效内容：读取时按行拆分，换行符会
  // 把密码切断，首尾空白则会被裁掉。不含空格的可打印 ASCII 字符
  // 能把两个问题都避开。
  for (size_t i = 0; i < np.length(); i++) {
    char c = np[i];
    if (c < 0x21 || c > 0x7E) {
      web.send(400, "text/plain", "charset");
      return;
    }
  }
  if (np == authPassword) {
    web.send(400, "text/plain", "same");
    return;
  }
  authPassword = np;
  saveAuth();
  LOG("[AUTH] admin password changed, user admin password %s\n", authPassword.c_str());
  web.send(200, "text/plain", "ok");
}
// ---- 黑名单（blocklist.bin）加载 ----
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
    // 128 KB 的随机寻址表：放 PSRAM 才是正解。DRAM 总共只有
    // 176 KB，而 WiFi 就要占去大半。
    // 有意只允许 PSRAM。若回退到 DRAM，等于从 WiFi、lwIP 与 web
    // 服务器共享的 176 KB 内部内存池里再抽走 128 KB，网络栈一起来
    // 设备就会 panic。
    bloomFilter = (uint8_t*)heap_caps_malloc(bloomBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (bloomFilter) {
      uint32_t rd = blocklist.read(bloomFilter, bloomBytes);
      if (rd != bloomBytes) { free(bloomFilter); bloomFilter = nullptr; }
    }
    if (!bloomFilter) {
      // 预过滤器没有容身之处了：此后每次查询都直接落到 flash，
      // 慢一些但结果仍然正确。bloomMayContain() 会放行（fail open）。
      LOG("[BL] bloom %u KB not in PSRAM - flash-only lookup\n",
          (unsigned)(bloomBytes >> 10));
    }
  }
  // 无论 Bloom 过滤器是否成功在 RAM 中留有副本，哈希表都位于
  // 它之后，因此这个偏移量从不依赖那次分配的结果。
  hashTableOffset = BL_HEADER_SIZE + bloomBytes;
  LOG("[BL] %u domains, bloom %u KB\n", numHashes, bloomFilter ? bloomBytes / 1024 : 0);
  return numHashes > 0;
}

// ---- 远程黑名单拉取 ----
static bool fetchBlocklist(String url) {
  if (url.length() == 0) return false;
  web.send(200, "text/plain", "fetching...");
  delay(50);

  bool https = url.startsWith("https://");
  HTTPClient http;
  WiFiClient plain;
  WiFiClientSecure secure;
  secure.setCACert(nullptr);  // 依赖固件内置的证书包

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
// ---- 配网门户（AP 模式）----
static void startConfigPortal() {
  portalMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID_DEFAULT);
  dashboardMessage("配网模式", AP_SSID_DEFAULT, "浏览器打开 192.168.4.1");
  LOG("[SETUP] AP %s open at http://%s/\n",
      AP_SSID_DEFAULT, WiFi.softAPIP().toString().c_str());
}

// 离开配网门户，回到 station 角色。配网表单保存完成后调用一次，
// 免得开发板一直停在 AP 模式、对着一个没人再看的表单空耗——
// 用户真正想要的是 DNS 引擎在工作。此时凭据已经写入 /wifi.cfg。
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
// secrets.h 中的编译期取值在有人填入真实内容之前只是占位符，因此
// 把出厂字符串视为「未配置」。
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
    // 这里没有任何配置，于是回退到 WiFi 驱动自带的 NVS 存储。
    // 这并非罕见的边角情况：本固件源自的面板是通过它的 web 应用
    // 保存网络的，因此一块已经在服役的板子无需任何设置就能
    // 自行入网。
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
  // 这块板子唯一的控制台是原生 USB 端口，而 TinyUSB 会丢弃主机
  // 挂载 CDC 之前写入的所有内容。稍微等它一会儿（有上限，纯电池
  // 启动时不会被拖住）。
  uint32_t serialWaitStart = millis();
  while (!Serial && millis() - serialWaitStart < 5000) delay(10);
  Serial.printf("AdBlock EPD booting (reset=%u heap=%u psram=%u)\n",
                (unsigned)g_bootReason, (unsigned)ESP.getFreeHeap(),
                (unsigned)ESP.getFreePsram());

  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_CFG_BTN, INPUT_PULLUP);


  // 先尝试不格式化直接挂载：LittleFS.begin(true) 只要挂载失败就会
  // 格式化，那会无声无息地擦掉刚烧录好的黑名单。
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

  // 在设备上自检 flash 哈希表应答是否正确。主机侧测试覆盖的是
  // 构建器；这里覆盖的是固件自身的后缀逐级检查、Bloom 预过滤与
  // 对真实烧录镜像的二分查找。
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

  // 必须先初始化网络栈，之后才能有代码打开套接字：
  // WiFiServer::begin() 会调用 socket()，向 lwIP 的 tcpip 线程投递消息；
  // 若在 esp_netif_init() 之前就这么做，tcpip.c 会以
  // "assert failed: tcpip_send_msg_wait_sem (Invalid mbox)" 中止。
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);

  // 在 WiFi 分支之前注册，这样配网门户也能用：全新设备没有
  // /wifi.cfg，而 AP 模式下提供的这个页面是给它写入凭据的唯一途径。
  web.on("/", handleRoot);
  web.on("/stats", handleStats);
  web.on("/refresh-screen", handleRefreshScreen);
  web.on("/ban", handleBan);
  web.on("/addblock", handleAddBlock);
  web.on("/unblock", handleUnblock);
  web.on("/setupdate", handleSetUpdate);
  web.on("/setpass", HTTP_POST, handleSetPass);
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
  // 收尾仍在进行中的面板刷新。一帧流式写入完成后，玻璃面板还需要
  // 约 15 s 才能稳定，这段时间就花在这里而不是绘图调用内部，
  // 因此 DNS 仍能持续得到应答。
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

  // 时钟跨过一天后就轮转一次当日汇总。`lastDay` 初值取 -1，让 NTP
  // 对时落地后的第一次 tick 只记录日期；否则首次同步时时钟从
  // 1970 年跳到今天会被当成跨日，清掉当天的计数。
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

  // 面板：按间隔整幅重绘；按住配置按键时立即重绘
  // （手动「给我看数据」的手势）。
  if (now - lastDashMs >= DASH_REFRESH_MS) {
    dashboardDraw();
    lastDashMs = now;
  }
  if (digitalRead(PIN_CFG_BTN) == LOW) {
    delay(SHORT_PRESS_MIN_MS);
    if (digitalRead(PIN_CFG_BTN) == LOW) {
      // 按住时长决定动作。短按：重绘仪表盘，即手动「给我看数据」。
      // 长按：再次打开配网门户——一旦开发板离开当初配网所用网络的
      // 覆盖范围，这是回到 WiFi 表单的唯一入口。
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

  // 定期拉取黑名单，只在 DNS 已经安静下来之后执行。
  if (updateIntervalH > 0 && updateUrl.length() &&
      now - lastPeriodicPullMs >= updateIntervalH * 3600000UL &&
      now - lastDnsActivityMs > REMOTE_FETCH_IDLE_MS) {
    fetchBlocklist(updateUrl);
    lastPeriodicPullMs = now;
  }

  // 链路掉线后必须重新接上，否则设备就会悄无声息：过去重连只在
  // 进入这里的第一轮之前发生一次，此后任何一次断连（路由器重启、
  // 漫游到的 AP 消失、面板刷新期间的短暂掉线）都会让设备卡在
  // 「ip offline」上，直到下一次断电重启。WL_CONNECTED 只覆盖
  // 状态树的第一段分支；之后出现的任何其他状态都说明 station 已
  // 掉线，应当把它重新拉起。
  //
  // 重试也要计数：一块板子怎么也连不回去的网络（密码改了、路由器
  // 换了、板子挪了地方）否则会被永远重试下去，而没有人能从设备
  // 本身纠正它。连续失败 WIFI_RETRY_BEFORE_PORTAL 次后，开发板放弃
  // 存储的凭据并打开配网门户——当负责重连的人就站在面板前而不是
  // 接着串口控制台时，「帮我重连 WiFi」就该是这个意思。
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
      // 完整地重启射频，而不只是重试关联：凭据并没有变，仅靠
      // WiFi.reconnect() 只会把那条已经失败过 N 次的握手原样
      // 重跑一遍。
      WiFi.disconnect(false, false);
      delay(100);
      connectWifi(WIFI_TIMEOUT);
    }
  }
  if (WiFi.status() == WL_CONNECTED) wifiFailCount = 0;

  // 心跳。控制台只有一个（USB CDC 端口），AP 模式下也没有任何
  // 其他日志，因此这行状态信息让设备在没有调试器的情况下依然
  // 保持可观测。
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