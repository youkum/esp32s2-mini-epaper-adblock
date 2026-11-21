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
#include <WiFiClientSecure.h>
#include <atomic>
#include "esp_ota_ops.h"
#include "esp_image_format.h"
#include "mbedtls/sha256.h"
#include "mbedtls/x509_crt.h"
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
#include "bloom_hash.h"
#include "dns_cache.h"
#include "http_reader.h"
#include "secrets.h"
#include "dashboard_html.h"

// ---- 配置 ----
static IPAddress dnsPrimary(9,9,9,9), dnsSecondary(1,1,1,1);
static DnsCache dnsCache;
static uint32_t dnsRetries=0, dnsTimeouts=0, dnsRejected=0;
static const uint16_t DNS_PORT = 53;
static const char* BLOCKLIST_PATH = "/blocklist.bin";
static const char* BLOCKLIST_TMP = "/blocklist.tmp";
static const int HASH_BYTES = 5;
static const uint64_t HASH_MASK = (1ULL << (HASH_BYTES * 8)) - 1;
static const int DNS_BUF_SIZE = 1232;
static const int MAX_PENDING = 32;
static const int MAX_CLIENTS = 96;
static const int MAX_CUSTOM = 200;
static const int MAX_BAN = 32;
static const uint32_t UPSTREAM_TIMEOUT_MS = 2000;
static const uint32_t REMOTE_FETCH_IDLE_MS = 3000;
static const char* DASHBOARD_ETAG = "adblk-console-v5";

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
static const char* COLLECTED_HEADERS[] = {"Authorization", "If-None-Match", "Origin", "Referer", "X-CSRF-Token"};
String updateUrl, updateStatus = "never";
uint32_t updateIntervalH = 24;
static uint32_t lastDnsActivityMs = 0;
static bool fetchBlocklist(String url);
static uint32_t restartAt=0, maxLoopUs=0;
enum class UpdateOwner { None, Remote, Rules, Firmware };
static UpdateOwner updateOwner=UpdateOwner::None;
static std::atomic<uint32_t> transferBytes{0}, transferTotal{0};
static std::atomic<bool> cancelDownload{false};
static QueueHandle_t downloadQueue=nullptr;
struct DownloadResult { bool staged; char message[128]; };
static uint32_t transferStarted=0;
static const char* CA_PATH="/blocklist-ca.pem";
static String otaStatus="未开始";
static uint32_t otaBytes=0, otaExpected=0;


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
  for (uint8_t i = 0; i < bloomK; i++) {
    uint32_t pos = bloomPosition(h64, bloomBits, i);
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
    if (!blocklist.seek(hashTableOffset + (uint32_t)mid * HASH_BYTES) ||
        blocklist.read(b, HASH_BYTES) != HASH_BYTES) return false;
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
    if (inCustom(h40) || (checkFlash && inFlash(h40))) return true;
    const char* dot = (const char*)memchr(p, '.', remaining);
    if (!dot) break;
    size_t consumed = (size_t)(dot - p) + 1;
    remaining -= consumed;
    p = dot + 1;
    if (!memchr(p, '.', remaining)) break;
  }
  return false;
}
// 配置文件先写同目录临时文件，再原子替换；校验失败不截断旧配置。
static bool writeConfig(const char* path,const String& text) {
  String staged=String(path)+".tmp";
  File f=LittleFS.open(staged,"w");
  bool ok=f && f.print(text)==text.length();
  if(f) { f.flush(); f.close(); }
  ok=ok && LittleFS.rename(staged,path);
  if(!ok) LittleFS.remove(staged);
  return ok;
}
// ---- 持久化：自定义域名 ----
static bool validCustomDomain(const String& d) {
  if(d.length()<3 || d.length()>253 || d.indexOf('.')<1) return false;
  size_t label=0;
  for(unsigned char ch:d) {
    if(ch=='.') { if(!label || label>63) return false; label=0; }
    else { if(!isalnum(ch) || ch>=128) { if(ch!='-' && ch!='_') return false; } ++label; }
  }
  return label>0 && label<=63;
}
static void loadCustom() {
  numCustom = 0;
  File f = LittleFS.open("/custom.txt", "r");
  if (!f) return;
  while (f.available() && numCustom < MAX_CUSTOM) {
    String l = f.readStringUntil('\n'); l.trim(); l.toLowerCase();
    if (validCustomDomain(l)) {
      customDom[numCustom] = l;
      customHash[numCustom] = fnv40(l.c_str(), l.length());
      numCustom++;
    }
  }
  f.close();
  sortCustom();
}
static bool saveCustom() {
  String text;
  for(int i=0;i<numCustom;++i) text+=customDom[i]+"\n";
  return writeConfig("/custom.txt",text);
}
static bool addCustom(String d) {
  d.trim(); d.toLowerCase();
  if (d.startsWith("www.")) d = d.substring(4);
  if(d.endsWith(".")) d.remove(d.length()-1);
  if (!validCustomDomain(d) || numCustom >= MAX_CUSTOM) return false;
  for (int i = 0; i < numCustom; i++) if (customDom[i] == d) return false;
  customDom[numCustom] = d;
  customHash[numCustom] = fnv40(d.c_str(), d.length());
  numCustom++;
  sortCustom();
  dnsCache.clear();
  if(!saveCustom()) { loadCustom(); return false; }
  return true;
}
static bool removeCustom(String d) {
  d.toLowerCase();
  if (d.startsWith("www.")) d = d.substring(4);
  for (int i = 0; i < numCustom; i++) if (customDom[i] == d) {
    for (int j = i; j < numCustom - 1; j++) { customDom[j] = customDom[j+1]; customHash[j] = customHash[j+1]; }
    numCustom--;
    dnsCache.clear();
    if(!saveCustom()) { loadCustom(); return false; }
    return true;
  }
  return false;
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
static bool saveBanned() {
  String text;
  for(int i=0;i<numBanned;++i) text+=IPAddress(bannedIP[i]).toString()+"\n";
  return writeConfig("/banned.txt",text);
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
  if (!dnsQuestionEnd(pkt, len) || (pkt[2]&0x80)) return 0;
  int i = 12; size_t o = 0;
  while (i < len) {
    uint8_t l = pkt[i++];
    if (l == 0) break;
    if (l & 0xC0) return 0;
    if (o + l + 1 >= 255 || i + l > len) return 0;
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
  bool backup;
  uint16_t wireID, queryLen;
};
Pending pending[MAX_PENDING];
int pendingCount = 0;
static uint8_t* pendingQueries = nullptr;
static int pendingCapacity = MAX_PENDING;
static uint16_t nextDnsID = 0;
static uint8_t tcpQuery[DNS_BUF_SIZE];
static bool tcpBackup = false;
static uint16_t tcpWireID = 0;
static void initDnsMemory() {
  pendingQueries = (uint8_t*)heap_caps_malloc(MAX_PENDING*DNS_BUF_SIZE, MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if (!pendingQueries) {
    pendingCapacity=4;
    pendingQueries=(uint8_t*)heap_caps_malloc(4*DNS_BUF_SIZE,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
  }
  if (!pendingQueries) pendingCapacity=0;
  nextDnsID=uint16_t(esp_random());
}
static void sendPending(int i) {
  Pending& p=pending[i];
  upstream.beginPacket(p.backup ? dnsSecondary : dnsPrimary,DNS_PORT);
  upstream.write(pendingQueries+i*DNS_BUF_SIZE,p.queryLen); upstream.endPacket(); p.sentMs=millis();
}
static void dnsFailure(uint8_t* q,int len) {
  q[2]=(q[2]&1)|0x80; q[3]=0x82;
  memset(q+6,0,6);
  // 保留一个问题，丢弃 EDNS 及其他附加记录。
  (void)len;
}


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
  for (int i = 0; i < pendingCapacity; i++) if (!pending[i].active) return i;
  return -1;
}
static void expirePending() {
  uint32_t now=millis();
  for(int i=0;i<pendingCapacity;++i) {
    Pending& p=pending[i];
    if(!p.active || now-p.sentMs<UPSTREAM_TIMEOUT_MS) continue;
    if(!p.backup && dnsSecondary!=dnsPrimary) { p.backup=true; ++dnsRetries; sendPending(i); }
    else {
      uint8_t* q=pendingQueries+i*DNS_BUF_SIZE;
      q[0]=p.originalID>>8; q[1]=p.originalID; dnsFailure(q,p.queryLen);
      dnsServer.beginPacket(p.clientIP,p.clientPort); dnsServer.write(q,dnsQuestionEnd(q,p.queryLen)); dnsServer.endPacket();
      p.active=false; --pendingCount; ++dnsTimeouts;
    }
  }
}
static void pollUpstream(uint8_t* buf,int bufSize) {
  for(int k=0;k<DNS_DRAIN_MAX;++k) {
    int sz=upstream.parsePacket(); if(sz<=0) return;
    IPAddress source=upstream.remoteIP(); uint16_t port=upstream.remotePort();
    int len=upstream.read(buf,bufSize);
    if(sz>bufSize || len<12 || port!=DNS_PORT) { ++dnsRejected; continue; }
    int i=0;
    for(;i<pendingCapacity;++i) if(pending[i].active && pending[i].wireID==dns16(buf)) break;
    if(i==pendingCapacity) { ++dnsRejected; continue; }
    Pending& p=pending[i]; uint8_t* q=pendingQueries+i*DNS_BUF_SIZE;
    if(source!=(p.backup?dnsSecondary:dnsPrimary) || !dnsReplyMatches(q,p.queryLen,buf,len)) { ++dnsRejected; continue; }
    dnsCache.put(q,p.queryLen,buf,len,millis());
    buf[0]=p.originalID>>8; buf[1]=p.originalID;
    dnsServer.beginPacket(p.clientIP,p.clientPort); dnsServer.write(buf,len); dnsServer.endPacket();
    p.active=false; --pendingCount;
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
  if (sz > DNS_BUF_SIZE || qlen < 13) return true;

  char domain[256]; uint16_t qtype = 0; int qend = qlen;
  size_t dl = parseQuery(dnsBuf, qlen, domain, &qtype, &qend);
  if (!dnsQuestionEnd(dnsBuf,qlen) || (dnsBuf[2]&0x80) || qend+28>DNS_BUF_SIZE) { ++dnsRejected; return true; }

  Dev* c = getClient((uint32_t)cip);
  bool ban = c && c->banned;
  bool blocked = ban || (dl && isBlocked(domain, dl));

  if (blocked) {
    int rlen = buildBlocked(dnsBuf, qend, qtype);
    totalBlocked++;
    if (c) c->blocked++;
    dnsServer.beginPacket(cip, cport);
    dnsServer.write(dnsBuf, rlen);
    dnsServer.endPacket();
  } else {
    totalAllowed++; if(c) c->allowed++;
    size_t cachedLen=0;
    uint8_t reply[512];
    if(dnsCache.get(dnsBuf,qlen,millis(),reply,cachedLen)) {
      dnsServer.beginPacket(cip,cport); dnsServer.write(reply,cachedLen); dnsServer.endPacket();
    } else {
      int slot=findFreeSlot();
      if(slot<0) {
        dnsFailure(dnsBuf,qlen); dnsServer.beginPacket(cip,cport);
        dnsServer.write(dnsBuf,qend); dnsServer.endPacket(); ++dnsTimeouts;
      } else {
        Pending& p=pending[slot];
        p.clientIP=uint32_t(cip); p.clientPort=cport; p.originalID=dns16(dnsBuf);
        p.wireID=++nextDnsID; p.queryLen=qlen; p.backup=false; p.active=true;
        dnsBuf[0]=p.wireID>>8; dnsBuf[1]=p.wireID;
        memcpy(pendingQueries+slot*DNS_BUF_SIZE,dnsBuf,qlen); ++pendingCount; sendPending(slot);
      }
    }
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
    if(!dnsQuestionEnd(tcpBuf,tcpGot) || (tcpBuf[2]&0x80) || qend+28>DNS_BUF_SIZE) { ++dnsRejected; tcpDnsReset(); return; }
    Dev* c = getClient((uint32_t)tcpDnsClient.remoteIP());
    bool ban = c && c->banned;
    bool blocked = ban || (dl && isBlocked(domain, dl));

    if (blocked) {
      int rlen = buildBlocked(tcpBuf, qend, qtype);
      totalBlocked++; if (c) c->blocked++;
      if (dl) trackerRecord(domain, blocked);
      tcpDnsReply(tcpBuf, rlen);
      return;
    }

    totalAllowed++; if(c) c->allowed++;
    if(dl) trackerRecord(domain,false);
    size_t cachedLen=0;
    uint8_t cached[512];
    if(dnsCache.get(tcpBuf,tcpGot,millis(),cached,cachedLen)) { tcpDnsReply(cached,cachedLen); return; }
    tcpQueryID=dns16(tcpBuf); tcpWireID=++nextDnsID; tcpBackup=false;
    tcpBuf[0]=tcpWireID>>8; tcpBuf[1]=tcpWireID;
    memcpy(tcpQuery,tcpBuf,tcpGot);
    tcpUpstream.beginPacket(dnsPrimary,DNS_PORT); tcpUpstream.write(tcpQuery,tcpGot); tcpUpstream.endPacket();
    tcpState=TCP_FORWARD; tcpDeadline=millis()+UPSTREAM_TIMEOUT_MS; return;
  }
  int sz=tcpUpstream.parsePacket();
  if(sz>0) {
    IPAddress source=tcpUpstream.remoteIP(); uint16_t port=tcpUpstream.remotePort();
    int n=tcpUpstream.read(tcpBuf,DNS_BUF_SIZE);
    if(sz<=DNS_BUF_SIZE && port==DNS_PORT && source==(tcpBackup?dnsSecondary:dnsPrimary) &&
       dnsReplyMatches(tcpQuery,tcpGot,tcpBuf,n)) {
      dnsCache.put(tcpQuery,tcpGot,tcpBuf,n,millis());
      tcpBuf[0]=tcpQueryID>>8; tcpBuf[1]=tcpQueryID; tcpDnsReply(tcpBuf,n); return;
    }
    ++dnsRejected;
  }
  if(int32_t(millis()-tcpDeadline)>=0) {
    if(!tcpBackup && dnsPrimary!=dnsSecondary) {
      tcpBackup=true; ++dnsRetries;
      tcpUpstream.beginPacket(dnsSecondary,DNS_PORT); tcpUpstream.write(tcpQuery,tcpGot); tcpUpstream.endPacket();
      tcpDeadline=millis()+UPSTREAM_TIMEOUT_MS;
    } else {
      tcpQuery[0]=tcpQueryID>>8; tcpQuery[1]=tcpQueryID; dnsFailure(tcpQuery,tcpGot);
      ++dnsTimeouts; tcpDnsReply(tcpQuery,dnsQuestionEnd(tcpQuery,tcpGot));
    }
  }
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
  authToken="";
  for(int i=0;i<4;++i) { char h[9]; snprintf(h,sizeof(h),"%08lx",(unsigned long)esp_random()); authToken+=h; }
}
static bool saveAuth();
static void loadAuth() {
  File f = LittleFS.open("/auth.cfg", "r");
  if (f) {
    authPassword = f.readStringUntil('\n'); authPassword.trim();
    authToken = f.readStringUntil('\n'); authToken.trim();
    f.close();
  }
  if (authPassword.length() && authToken.length()<32) {
    authToken=""; for(int i=0;i<4;++i) { char h[9]; snprintf(h,sizeof(h),"%08lx",(unsigned long)esp_random()); authToken+=h; } saveAuth();
  }
  if (authPassword.length() == 0) {
    genRandomPassword();
    if(!saveAuth()) Serial.println("[AUTH] 凭据保存失败，本次密码仅在当前启动有效");
    Serial.printf("[AUTH] 已创建管理员，用户名 admin，密码 %s\n",authPassword.c_str());
  } else {
    // 有意每次开机都打印 admin 账号密码。控制台有 Basic Auth 把守，
    // 否则忘记密码就意味着要经串口擦除 /auth.cfg；而这个端口本身
    // 就等同于对设备的物理接触。
    Serial.printf("[AUTH] 管理员用户名 admin，密码 %s\n",authPassword.c_str());
  }
}
static bool checkAuth() {
  String expected = "Basic " + b64encode("admin:" + authPassword);
  return web.header("Authorization") == expected;
}
static bool checkToken() {
  return authToken.length() >= 32 && ((web.hasArg("token") && web.arg("token") == authToken) || web.header("X-CSRF-Token") == authToken);
}
static void requireAuth() {
  web.sendHeader("WWW-Authenticate", "Basic realm=\"AdBlock\"");
  web.send(401, "text/plain", "auth required");
}

// 把管理员密码与 CSRF token 持久化到 /auth.cfg（第 1 行为密码，
// 第 2 行为 token）。用户在控制台里修改密码时调用，让新密码在
// 重启后依然有效。
static bool saveAuth() { return writeConfig("/auth.cfg",authPassword+"\n"+authToken+"\n"); }

// ---- web 服务器 ----
static String macStr(const uint8_t* m) {
  char s[18]; snprintf(s, sizeof(s), "%02x:%02x:%02x:%02x:%02x:%02x", m[0],m[1],m[2],m[3],m[4],m[5]);
  return String(s);
}
static String jesc(const String& s) {
  String o;
  for (unsigned char ch : s) {
    if (ch == '"' || ch == '\\') o += '\\';
    if (ch < 32) { char b[7]; snprintf(b,sizeof(b),"\\u%04x",ch); o+=b; }
    else o += char(ch);
  }
  return o;
}

// 设备处于 AP 模式、等待 WiFi 凭据时为 true。仪表盘有 Basic Auth
// 保护，但没有网络的设备无法通过已知地址访问到，因此配网页面
// 必须保持开放。
static bool portalMode = false;
static bool pendingPortalMessage = false;

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
  size_t est = 1400 + (size_t)numClients * 96 + (size_t)numCustom * 34 +
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
    j += "{\"d\":\"" + jesc(String(t[i].domain)) + "\",\"n\":" + t[i].count + ",\"error\":" + t[i].error + "}";
  }
  j += "],\"dnsPrimary\":\""+dnsPrimary.toString()+"\",\"dnsSecondary\":\""+dnsSecondary.toString()+"\"";
  j += ",\"cacheHits\":"+String(dnsCache.hits)+",\"cacheMisses\":"+String(dnsCache.misses);
  j += ",\"dnsRetries\":"+String(dnsRetries)+",\"dnsTimeouts\":"+String(dnsTimeouts)+",\"dnsRejected\":"+String(dnsRejected);
  j += ",\"panel\":\""+String(epdStatus())+"\",\"panelError\":\""+String(epdError())+"\",\"panelFaults\":"+String(epdFaults());
  j += ",\"panelRows\":"+String(epdRowsSent())+",\"maxLoopUs\":"+String(maxLoopUs);
  j += ",\"updateBytes\":"+String(transferBytes.load())+",\"updateTotal\":"+String(transferTotal.load());
  j += ",\"otaStatus\":\""+jesc(otaStatus)+"\",\"otaBytes\":"+String(otaBytes)+",\"otaTotal\":"+String(otaExpected);
  const esp_partition_t* slot=esp_ota_get_next_update_partition(nullptr);
  j += ",\"otaCapacity\":"+String(slot?slot->size:0)+",\"fsFree\":"+String(LittleFS.totalBytes()-LittleFS.usedBytes());
  j += ",\"caConfigured\":"+String(LittleFS.exists(CA_PATH)?"true":"false")+",\"hours\":[";
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
static bool pendingRefresh = false;
static void handleRefreshScreen() {
  if (!checkAuth()) { requireAuth(); return; }
  if (!checkToken()) { web.send(403, "text/plain", "bad token"); return; }
  pendingRefresh = true;
  web.send(202, "text/plain; charset=utf-8", "已加入刷新队列");
}

static void handleBan() {
  if (!checkAuth()) { requireAuth(); return; }
  if (!checkToken()) { web.send(403, "text/plain", "bad token"); return; }
  IPAddress ip;
  if (ip.fromString(web.arg("ip"))) {
    uint32_t ipraw = (uint32_t)ip;
    Dev* c = getClient(ipraw);
    if (c) {
      bool next=!c->banned;
      if(next && !addBannedIP(ipraw)) { web.send(409,"text/plain; charset=utf-8","封禁列表已满"); return; }
      if(!next) removeBannedIP(ipraw);
      c->banned=next;
      dnsCache.clear();
      if(!saveBanned()) {
        loadBanned(); for(int i=0;i<numClients;++i) clients[i].banned=isBannedIP(clients[i].ip);
        web.send(500,"text/plain; charset=utf-8","封禁配置保存失败"); return;
      }
    }
  }
  web.send(200, "text/plain", "ok");
}
static void handleAddBlock() {
  if (!checkAuth()) { requireAuth(); return; }
  if (!checkToken()) { web.send(403, "text/plain", "bad token"); return; }
  bool ok=addCustom(web.arg("d"));
  web.send(ok?200:400, "text/plain; charset=utf-8", ok?"已添加":"无效、重复、已满或保存失败");
}
static void handleUnblock() {
  if (!checkAuth()) { requireAuth(); return; }
  if (!checkToken()) { web.send(403, "text/plain", "bad token"); return; }
  bool ok=removeCustom(web.arg("d"));
  web.send(ok?200:400, "text/plain; charset=utf-8", ok?"已移除":"规则不存在或保存失败");
}
// ---- 远程黑名单配置（持久化在 /update.cfg）----
// 该 URL 必须返回一个已经是本固件二进制格式的文件，因此实际上它
// 指向用户托管在某处的一份 blocklist.bin 副本。通过控制台上传
// .bin 文件则覆盖其余情况。
static bool saveUpdateCfg() { return writeConfig("/update.cfg",updateUrl+"\n"+String(updateIntervalH)+"\n"); }
static void loadUpdateCfg() {
  File f = LittleFS.open("/update.cfg", "r");
  if (!f) return;
  updateUrl = f.readStringUntil('\n'); updateUrl.trim();
  long iv = f.readStringUntil('\n').toInt();
  if (iv >= 0 && iv <= 720) updateIntervalH = (uint32_t)iv;
  f.close();
  if (updateUrl.length() > 0) {
    LOG("[UPD] auto-update every %lu h from %s\n",
        (unsigned long)updateIntervalH, updateUrl.c_str());
  }
}
static void handleSetUpdate() {
  if (!checkAuth()) { requireAuth(); return; }
  if (!checkToken()) { web.send(403, "text/plain", "bad token"); return; }
  if(updateOwner!=UpdateOwner::None) { web.send(409,"text/plain; charset=utf-8","正在更新，请稍后修改配置"); return; }
  String url=web.arg("url"); url.trim();
  if(url.length()>1024 || (url.length() && !url.startsWith("http://") && !url.startsWith("https://"))) { web.send(400,"text/plain; charset=utf-8","URL 无效"); return; }
  long iv = web.arg("iv").toInt();
  if(iv<0 || iv>720) { web.send(400,"text/plain; charset=utf-8","间隔必须为 0..720 小时"); return; }
  String oldUrl=updateUrl; uint32_t oldInterval=updateIntervalH;
  updateUrl=url; updateIntervalH = (uint32_t)iv;
  if(!saveUpdateCfg()) { updateUrl=oldUrl; updateIntervalH=oldInterval; web.send(500,"text/plain; charset=utf-8","更新配置保存失败"); return; }
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
  if(ssid.length()>32 || pass.length()>63 || ssid.indexOf('\n')>=0 || pass.indexOf('\n')>=0 || ssid.indexOf('\r')>=0 || pass.indexOf('\r')>=0) {
    web.send(400,"text/plain; charset=utf-8","WiFi 配置无效"); return;
  }
  if(!writeConfig("/wifi.cfg",ssid+"\n"+pass+"\n")) { web.send(500,"text/plain; charset=utf-8","WiFi 配置写入失败"); return; }
  web.send(200,"text/plain; charset=utf-8","已保存，即将重启"); restartAt=millis()+700;
}

// ---- 修改管理员密码 ----
// 由控制台调用，而控制台受 Basic Auth 保护：能走到这里的请求
// 已经用当前密码验证过了，因此无需再次提交旧密码。新密码直接
// 写入 /auth.cfg 并在下一次请求时生效——浏览器仍缓存着旧凭据，
// 会重新弹出认证提示；控制台错误信息无需新增面板字形。
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
  String previous=authPassword; authPassword=np;
  if(!saveAuth()) { authPassword=previous; web.send(500,"text/plain; charset=utf-8","密码保存失败，旧密码仍有效"); return; }
  LOG("[AUTH] admin password changed, user admin password %s\n", authPassword.c_str());
  web.send(200, "text/plain", "ok");
}
// ---- 黑名单（blocklist.bin）加载 ----
static uint32_t readLe32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
         (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

static bool readBlocklistHeader(File& f, uint32_t& count, uint32_t& bits, uint8_t& k) {
  uint8_t hdr[BL_HEADER_SIZE];
  if (!f || f.read(hdr, sizeof(hdr)) != sizeof(hdr) ||
      memcmp(hdr, BL_MAGIC, sizeof(BL_MAGIC)) != 0 ||
      hdr[4] != BL_VERSION || hdr[5] != HASH_BYTES) return false;
  count = readLe32(hdr + 6);
  bits = readLe32(hdr + 10);
  k = hdr[14];
  const uint64_t bloomBytes = (uint64_t(bits) + 7) / 8;
  const uint64_t expected = BL_HEADER_SIZE + bloomBytes + uint64_t(count) * HASH_BYTES;
  return count > 0 && (bits == 0 || (k > 0 && k <= 32)) && expected == f.size();
}

static bool loadBlocklist() {
  if (blocklist) blocklist.close();
  free(bloomFilter);
  bloomFilter = nullptr;
  numHashes = hashTableOffset = bloomBits = 0;
  bloomK = 0;
  blocklist = LittleFS.open(BLOCKLIST_PATH, "r");
  uint32_t count, bits;
  uint8_t k;
  if (!readBlocklistHeader(blocklist, count, bits, k)) {
    LOGN("[BL] 黑名单缺失、头部无效或文件长度不符");
    if (blocklist) blocklist.close();
    return false;
  }
  numHashes = count;
  bloomBits = bits;
  bloomK = k;
  const uint32_t bloomBytes = (uint64_t(bloomBits) + 7) / 8;
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

// 先校验临时文件；LittleFS rename 会替换目标，无需先删除旧列表。
static bool installStagedBlocklist() {
  File candidate = LittleFS.open(BLOCKLIST_TMP, "r");
  uint32_t count, bits;
  uint8_t k;
  const bool valid = readBlocklistHeader(candidate, count, bits, k);
  if (candidate) candidate.close();
  if (!valid) {
    LittleFS.remove(BLOCKLIST_TMP);
    return false;
  }
  if (blocklist) blocklist.close();
  if (!LittleFS.rename(BLOCKLIST_TMP, BLOCKLIST_PATH)) {
    LittleFS.remove(BLOCKLIST_TMP);
    loadBlocklist();
    return false;
  }
  dnsCache.clear();
  return loadBlocklist();
}

// 下载任务只拥有网络连接和临时文件；绝不改全局 Bloom 或旧黑名单句柄。
// 终态通过队列交给主循环，安装只在任务释放临时文件之后发生。
struct DownloadJob { String url, ca; uint32_t capacity; };
class DownloadIO {
  WiFiClient& client; File& file;
  uint32_t start, lastRead, capacity;
public:
  bool failed=false;
  DownloadIO(WiFiClient& c,File& f,uint32_t cap):client(c),file(f),start(millis()),lastRead(start),capacity(cap) {}
  int read(uint8_t* out,size_t want) {
    for(;;) {
      uint32_t now=millis();
      if(cancelDownload.load() || now-start>=120000 || now-lastRead>=5000) { failed=true; return -1; }
      int available=client.available();
      if(available>0) {
        int n=client.read(out,min(want,size_t(available)));
        if(n>0) { lastRead=now; return n; }
      } else if(!client.connected()) return 0;
      vTaskDelay(1);
    }
  }
  bool write(const uint8_t* b,size_t n) {
    uint32_t done=transferBytes.load();
    if(done>capacity || n>capacity-done || file.write(b,n)!=n) { failed=true; return false; }
    transferBytes=done+n; vTaskDelay(1); return true;
  }
  void total(uint32_t n) { transferTotal=n; }
};
static bool parseDownloadUrl(const String& url,String& host,String& path,uint16_t& port,bool& https) {
  https=url.startsWith("https://");
  if(!https && !url.startsWith("http://")) return false;
  for(unsigned char c:url) if(c<=32 || c==127) return false;
  int start=https?8:7, slash=url.indexOf('/',start), query=url.indexOf('?',start);
  int end=slash<0?url.length():slash;
  if(query>=0 && query<end) end=query;
  String authority=url.substring(start,end);
  if(!authority.length() || authority.indexOf('@')>=0 || authority.indexOf('#')>=0 || authority.indexOf('[')>=0) return false;
  port=https?443:80;
  int colon=authority.indexOf(':');
  if(colon>=0) {
    String value=authority.substring(colon+1); if(!value.length()) return false;
    for(char c:value) if(c<'0'||c>'9') return false;
    long n=value.toInt(); if(n<1||n>65535) return false; port=n;
    authority=authority.substring(0,colon);
  }
  if(!authority.length() || authority.length()>253) return false;
  for(char c:authority) if(!isalnum(c) && c!='.' && c!='-') return false;
  host=authority; path=end==int(url.length())?"/":url.substring(end);
  if(path[0]=='?') path="/"+path;
  return path.indexOf('#')<0;
}
static void downloadTask(void* arg) {
  DownloadJob* job=static_cast<DownloadJob*>(arg);
  DownloadResult result{};
  WiFiClient plain; WiFiClientSecure secure;
  String host,path; uint16_t port=0; bool https=false;
  bool parsed=parseDownloadUrl(job->url,host,path,port,https);
  if(https) { secure.setCACert(job->ca.c_str()); secure.setHandshakeTimeout(10); }
  WiFiClient& client=https?static_cast<WiFiClient&>(secure):plain;
  bool connected=parsed && !cancelDownload.load() && client.connect(host.c_str(),port,5000);
  if(!connected) snprintf(result.message,sizeof(result.message),"连接失败，请检查 URL、网络、CA 与时间");
  else {
    client.setTimeout(5);
    String request="GET "+path+" HTTP/1.1\r\nHost: "+host+":"+String(port)+"\r\nConnection: close\r\nAccept-Encoding: identity\r\nUser-Agent: AdBlock-EPD/1.3\r\n\r\n";
    if(client.write((const uint8_t*)request.c_str(),request.length())!=request.length()) {
      snprintf(result.message,sizeof(result.message),"HTTP 请求发送失败");
    } else {
      File tmp=LittleFS.open(BLOCKLIST_TMP,"w");
      if(!tmp) snprintf(result.message,sizeof(result.message),"无法写入临时文件");
      else {
        DownloadIO io(client,tmp,job->capacity); HttpReader<DownloadIO> reader(io);
        result.staged=reader.run() && !io.failed && !cancelDownload.load() && transferBytes.load()>=BL_HEADER_SIZE;
        tmp.flush(); tmp.close();
        snprintf(result.message,sizeof(result.message),"%s",result.staged?"下载完成，等待校验安装":"HTTP 响应无效、空间不足、传输中断或超时");
      }
    }
  }
  client.stop(); delete job;
  if(!result.staged) LittleFS.remove(BLOCKLIST_TMP);
  xQueueOverwrite(downloadQueue,&result); vTaskDelete(nullptr);
}
static bool fetchBlocklist(String url) {
  if(updateOwner!=UpdateOwner::None || WiFi.status()!=WL_CONNECTED || !downloadQueue) return false;
  bool https=url.startsWith("https://");
  if(!https && !url.startsWith("http://")) { updateStatus="URL 无效"; return false; }
  String ca;
  if(https) {
    if(time(nullptr)<1700000000) { updateStatus="HTTPS 需要先完成 NTP 对时"; return false; }
    File f=LittleFS.open(CA_PATH,"r");
    if(!f || f.size()>8192) { if(f) f.close(); updateStatus="HTTPS 需要配置 PEM CA"; return false; }
    ca=f.readString(); f.close();
    if(!ca.length()) { updateStatus="HTTPS CA 为空"; return false; }
  }
  uint32_t freeSpace=LittleFS.totalBytes()-LittleFS.usedBytes();
  if(freeSpace<=8192) { updateStatus="临时文件空间不足"; return false; }
  DownloadJob* job=new(std::nothrow) DownloadJob{url,ca,freeSpace-8192};
  if(!job) { updateStatus="下载任务内存不足"; return false; }
  updateOwner=UpdateOwner::Remote; transferBytes=0; transferTotal=0; cancelDownload=false; transferStarted=millis();
  updateStatus="后台下载中";
  if(xTaskCreate(downloadTask,"blocklist",12288,job,1,nullptr)!=pdPASS) {
    delete job; updateOwner=UpdateOwner::None; updateStatus="无法创建下载任务"; return false;
  }
  return true;
}
static void serviceDownload() {
  if(updateOwner!=UpdateOwner::Remote) return;
  if(millis()-transferStarted>=120000 || WiFi.status()!=WL_CONNECTED) cancelDownload=true;
  DownloadResult result;
  if(xQueueReceive(downloadQueue,&result,0)==pdTRUE) {
    updateStatus=result.message;
    if(result.staged) updateStatus=installStagedBlocklist()?"更新成功":"黑名单校验或替换失败，保留旧表";
    updateOwner=UpdateOwner::None;
  }
}
static void handleFetchNow() {
  if(!checkAuth()) { requireAuth(); return; }
  if(!checkToken()) { web.send(403,"text/plain","bad token"); return; }
  bool started=fetchBlocklist(updateUrl);
  web.send(started?202:409,"text/plain; charset=utf-8",started?"已开始后台下载":updateStatus);
}
static void handleSetCA() {
  if(!checkAuth()) { requireAuth(); return; }
  if(!checkToken()) { web.send(403,"text/plain","bad token"); return; }
  if(updateOwner!=UpdateOwner::None) { web.send(409,"text/plain; charset=utf-8","更新中，请稍后"); return; }
  String pem=web.arg("pem");
  if(pem.length()<64 || pem.length()>8192) { web.send(400,"text/plain; charset=utf-8","CA 长度必须为 64..8192 字节"); return; }
  mbedtls_x509_crt chain; mbedtls_x509_crt_init(&chain);
  int parsed=mbedtls_x509_crt_parse(&chain,(const unsigned char*)pem.c_str(),pem.length()+1);
  mbedtls_x509_crt_free(&chain);
  if(parsed!=0) { web.send(400,"text/plain; charset=utf-8","无法解析 PEM CA"); return; }
  File f=LittleFS.open("/blocklist-ca.tmp","w");
  bool ok=f && f.print(pem)==pem.length(); if(f) f.close();
  ok=ok && LittleFS.rename("/blocklist-ca.tmp",CA_PATH);
  if(!ok) LittleFS.remove("/blocklist-ca.tmp");
  web.send(ok?200:500,"text/plain; charset=utf-8",ok?"CA 已保存":"CA 写入失败");
}
// ---- 配网门户（AP 模式）----
static void startConfigPortal() {
  portalMode = true;
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID_DEFAULT);
  pendingPortalMessage = true;
  LOG("[SETUP] AP %s open at http://%s/\n",
      AP_SSID_DEFAULT, WiFi.softAPIP().toString().c_str());
}

// ---- OTA 与规则上传：在 START 阶段先鉴权，失败不写、不激活、不重启 ----
static esp_ota_handle_t otaHandle=0;
static const esp_partition_t* otaPartition=nullptr;
static bool otaAccepted=false, otaOk=false, ruleAccepted=false, blocklistUploadOk=false;
static String otaSha;
static mbedtls_sha256_context otaHash;
static File blocklistUpload;
static uint32_t uploadStarted=0;
static void otaFail(const String& reason) {
  otaStatus=reason; otaAccepted=false; otaOk=false;
  if(otaHandle) { esp_ota_abort(otaHandle); otaHandle=0; }
  mbedtls_sha256_free(&otaHash);
  if(updateOwner==UpdateOwner::Firmware) updateOwner=UpdateOwner::None;
}
static void handleUpdate() {
  HTTPUpload& up=web.upload();
  if(up.status==UPLOAD_FILE_START) {
    otaOk=false;
    if(!checkAuth() || !checkToken()) { otaStatus="认证或 CSRF 校验失败"; return; }
    if(updateOwner!=UpdateOwner::None || restartAt) { otaStatus="另一项更新正在进行"; return; }
    long expected=web.arg("size").toInt();
    otaPartition=esp_ota_get_next_update_partition(nullptr);
    if(!otaPartition || expected<24 || uint32_t(expected)>otaPartition->size) { otaStatus="固件长度无效或超过 OTA 分区"; return; }
    otaSha=web.arg("sha256"); otaSha.toLowerCase();
    if(otaSha.length() && otaSha.length()!=64) { otaStatus="SHA-256 必须为 64 位十六进制"; return; }
    for(char ch:otaSha) if(!isxdigit(ch)) { otaStatus="SHA-256 格式无效"; return; }
    esp_err_t err=esp_ota_begin(otaPartition,expected,&otaHandle);
    if(err!=ESP_OK) { otaHandle=0; otaStatus="OTA 初始化失败："+String(esp_err_to_name(err)); return; }
    mbedtls_sha256_init(&otaHash); mbedtls_sha256_starts_ret(&otaHash,0);
    otaExpected=expected; otaBytes=0; otaAccepted=true; updateOwner=UpdateOwner::Firmware;
    otaStatus="接收固件中"; uploadStarted=millis();
  } else if(up.status==UPLOAD_FILE_WRITE && otaAccepted) {
    if(up.currentSize>otaExpected-otaBytes || millis()-uploadStarted>=120000) { otaFail("固件长度不符或上传超时"); return; }
    if(!otaBytes) {
      if(up.currentSize<sizeof(esp_image_header_t)) { otaFail("固件头部不完整"); return; }
      esp_image_header_t header; memcpy(&header,up.buf,sizeof(header));
      if(header.magic!=ESP_IMAGE_HEADER_MAGIC || header.chip_id!=ESP_CHIP_ID_ESP32S2) { otaFail("不是 ESP32-S2 应用固件；不能上传合成镜像"); return; }
    }
    esp_err_t err=esp_ota_write(otaHandle,up.buf,up.currentSize);
    if(err!=ESP_OK) { otaFail("OTA 写入失败："+String(esp_err_to_name(err))); return; }
    mbedtls_sha256_update_ret(&otaHash,up.buf,up.currentSize); otaBytes+=up.currentSize;
  } else if(up.status==UPLOAD_FILE_END && otaAccepted) {
    if(otaBytes!=otaExpected || up.totalSize!=otaExpected) { otaFail("固件上传不完整"); return; }
    uint8_t digest[32]; mbedtls_sha256_finish_ret(&otaHash,digest); mbedtls_sha256_free(&otaHash);
    char hex[65]; for(int i=0;i<32;++i) snprintf(hex+2*i,3,"%02x",digest[i]);
    if(otaSha.length() && otaSha!=hex) { otaFail("固件 SHA-256 不匹配"); return; }
    esp_err_t err=esp_ota_end(otaHandle); otaHandle=0;
    if(err==ESP_OK) err=esp_ota_set_boot_partition(otaPartition);
    if(err!=ESP_OK) { otaFail("镜像校验或激活失败："+String(esp_err_to_name(err))); return; }
    otaAccepted=false; otaOk=true; otaStatus="固件校验成功，等待重启";
    updateOwner=UpdateOwner::None;
  } else if(up.status==UPLOAD_FILE_ABORTED && otaAccepted) otaFail("固件上传已中断，未重启");
}
static void handleUpdateDone() {
  if(!checkAuth()) { requireAuth(); return; }
  if(!checkToken()) { web.send(403,"text/plain","bad token"); return; }
  web.send(otaOk?200:400,"text/plain; charset=utf-8",otaStatus);
  if(otaOk) { restartAt=millis()+1000; otaOk=false; }
}
static void failRules() {
  if(blocklistUpload) blocklistUpload.close();
  LittleFS.remove(BLOCKLIST_TMP); ruleAccepted=false; blocklistUploadOk=false;
  if(updateOwner==UpdateOwner::Rules) updateOwner=UpdateOwner::None;
}
static void handleBlocklistUpload() {
  HTTPUpload& up=web.upload();
  if(up.status==UPLOAD_FILE_START) {
    blocklistUploadOk=false;
    if(!checkAuth() || !checkToken()) { updateStatus="认证或 CSRF 校验失败"; return; }
    if(updateOwner!=UpdateOwner::None || restartAt) { updateStatus="另一项更新正在进行"; return; }
    uint32_t available=LittleFS.totalBytes()-LittleFS.usedBytes();
    long expected=web.arg("size").toInt();
    if(expected<BL_HEADER_SIZE || available<=8192 || uint32_t(expected)>available-8192) { updateStatus="规则长度无效或临时空间不足"; return; }
    transferTotal=expected; transferBytes=0; uploadStarted=millis();
    blocklistUpload=LittleFS.open(BLOCKLIST_TMP,"w");
    if(!blocklistUpload) { updateStatus="无法打开规则临时文件"; return; }
    ruleAccepted=true; updateOwner=UpdateOwner::Rules; updateStatus="接收规则中";
  } else if(up.status==UPLOAD_FILE_WRITE && ruleAccepted) {
    uint32_t done=transferBytes.load();
    if(up.currentSize>transferTotal.load()-done || millis()-uploadStarted>=120000 || blocklistUpload.write(up.buf,up.currentSize)!=up.currentSize) {
      updateStatus="规则长度不符、超时或写入失败"; failRules(); return;
    }
    transferBytes=done+up.currentSize;
  } else if(up.status==UPLOAD_FILE_END && ruleAccepted) {
    bool complete=up.totalSize==transferTotal.load() && transferBytes.load()==transferTotal.load();
    blocklistUpload.close();
    blocklistUploadOk=complete && installStagedBlocklist();
    if(!blocklistUploadOk) LittleFS.remove(BLOCKLIST_TMP);
    ruleAccepted=false; updateOwner=UpdateOwner::None;
    updateStatus=blocklistUploadOk?"黑名单已更新":"规则上传不完整或结构校验失败";
  } else if(up.status==UPLOAD_FILE_ABORTED && ruleAccepted) { updateStatus="规则上传已中断"; failRules(); }
}
static void handleBlocklistDone() {
  if(!checkAuth()) { requireAuth(); return; }
  if(!checkToken()) { web.send(403,"text/plain","bad token"); return; }
  web.send(blocklistUploadOk?200:400,"text/plain; charset=utf-8",updateStatus); blocklistUploadOk=false;
}
static void serviceUploads() {
  if(millis()-uploadStarted>=120000) {
    if(otaAccepted) otaFail("固件上传超时，未重启");
    if(ruleAccepted) { updateStatus="规则上传超时"; failRules(); }
  }
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
    ssid = f.readStringUntil('\n'); if(ssid.endsWith("\r")) ssid.remove(ssid.length()-1);
    pass = f.readStringUntil('\n'); if(pass.endsWith("\r")) pass.remove(pass.length()-1);
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
  (void)timeoutMs;
  digitalWrite(PIN_LED,LOW);
  return true; // 只表示连接已启动，成功由 serviceWifi() 的 WL_CONNECTED 边沿判定。
}
static bool wifiConnecting=false, networkStarted=false, wifiEverConnected=false;
static uint32_t wifiStarted=0, wifiNextTry=0;
static uint8_t wifiFailures=0;
static void stopDnsNetwork() {
  dnsServer.stop(); upstream.stop(); tcpUpstream.stop(); tcpDnsServer.end(); tcpDnsReset();
  for(auto& p:pending) p.active=false; pendingCount=0; dnsCache.clear(); MDNS.end();
  networkStarted=false;
}
static void beginWifiAttempt() {
  WiFi.disconnect(false,false);
  if(!connectWifi(WIFI_TIMEOUT)) { startConfigPortal(); return; }
  wifiConnecting=true; wifiStarted=millis();
}
static void serviceWifi() {
  uint32_t now=millis();
  if(portalMode) {
    wifiConnecting=false;
    if(networkStarted) stopDnsNetwork();
    return;
  }
  if(WiFi.status()==WL_CONNECTED) {
    wifiConnecting=false; wifiFailures=0; wifiEverConnected=true;
    if(!networkStarted) {
      if(dnsPrimary==WiFi.localIP()) dnsPrimary=IPAddress(9,9,9,9);
      if(dnsSecondary==WiFi.localIP()) dnsSecondary=IPAddress(1,1,1,1);
      dnsServer.begin(DNS_PORT); upstream.begin(0); tcpUpstream.begin(0);
      tcpDnsServer.begin(); tcpDnsServer.setNoDelay(true);
      configTime(NTP_UTC_OFFSET,0,"pool.ntp.org","time.nist.gov");
      MDNS.begin(DNS_HOSTNAME); MDNS.addService("http","tcp",80);
      networkStarted=true; lastDnsActivityMs=now; pendingRefresh=true;
      LOG("[WIFI] 已连接 %s，DNS 服务已重新绑定\n",WiFi.localIP().toString().c_str());
    }
    digitalWrite(PIN_LED,LOW); return;
  }
  if(networkStarted) { stopDnsNetwork(); wifiNextTry=now+LIVE_WIFI_RETRY_MS; }
  if(wifiConnecting) {
    digitalWrite(PIN_LED,(now/500)%2);
    if(now-wifiStarted>=WIFI_TIMEOUT) {
      wifiConnecting=false; WiFi.disconnect(false,false); ++wifiFailures;
      wifiNextTry=now+LIVE_WIFI_RETRY_MS;
      LOG("[WIFI] 连接超时，失败 %u/%u\n",unsigned(wifiFailures),unsigned(WIFI_RETRY_BEFORE_PORTAL));
      if(!wifiEverConnected || wifiFailures>=WIFI_RETRY_BEFORE_PORTAL) startConfigPortal();
    }
  } else if(int32_t(now-wifiNextTry)>=0) beginWifiAttempt();
}
static bool usableDnsIP(const IPAddress& ip) {
  uint8_t first=ip[0];
  return first!=0 && first!=127 && first<224 && ip!=WiFi.localIP() && ip!=WiFi.softAPIP();
}
static void loadDnsCfg() {
  File f=LittleFS.open("/dns.cfg","r"); if(!f) return;
  String a=f.readStringUntil('\n'),b=f.readStringUntil('\n'); f.close(); a.trim();b.trim();
  IPAddress primary,secondary;
  if(primary.fromString(a) && secondary.fromString(b) && usableDnsIP(primary) && usableDnsIP(secondary)) {
    dnsPrimary=primary;dnsSecondary=secondary;
  }
}
static void handleSetDns() {
  if(!checkAuth()) { requireAuth(); return; }
  if(!checkToken()) { web.send(403,"text/plain","bad token"); return; }
  IPAddress a,b;
  if(!a.fromString(web.arg("primary")) || !b.fromString(web.arg("secondary")) || !usableDnsIP(a) || !usableDnsIP(b)) {
    web.send(400,"text/plain; charset=utf-8","请输入非本机、非回环的有效 IPv4 上游"); return;
  }
  File f=LittleFS.open("/dns.tmp","w");
  bool ok=f && f.println(a.toString())==a.toString().length()+2 && f.println(b.toString())==b.toString().length()+2;
  if(f) f.close(); ok=ok && LittleFS.rename("/dns.tmp","/dns.cfg");
  if(!ok) { LittleFS.remove("/dns.tmp"); web.send(500,"text/plain; charset=utf-8","DNS 配置写入失败"); return; }
  dnsPrimary=a; dnsSecondary=b; dnsCache.clear();
  for(auto& p:pending) p.active=false; pendingCount=0; tcpDnsReset();
  web.send(200,"text/plain; charset=utf-8","上游已保存；旧待处理查询需客户端重试");
}
// ---- setup / loop ----
static uint32_t lastDashMs = 0;
static uint32_t lastPeriodicPullMs = 0;

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

  trackerInit();
  trackerLoad();
  loadCustom();
  loadBanned();
  loadAuth();
  loadUpdateCfg();
  downloadQueue=xQueueCreate(1,sizeof(DownloadResult));
  initDnsMemory();
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
  web.on("/fetch-now", HTTP_POST, handleFetchNow);
  web.on("/setca", HTTP_POST, handleSetCA);
  web.on("/setdns", HTTP_POST, handleSetDns);
  web.on("/setpass", HTTP_POST, handleSetPass);
  web.on("/setwifi", HTTP_POST, handleSetWifi);
  web.on("/update", HTTP_POST, handleUpdateDone, handleUpdate);
  web.on("/blocklist-upload", HTTP_POST, handleBlocklistDone, handleBlocklistUpload);
  web.collectHeaders(COLLECTED_HEADERS, sizeof(COLLECTED_HEADERS) / sizeof(COLLECTED_HEADERS[0]));
  web.begin();
  LOGN("[BOOT] routes registered, connecting to WiFi");

  loadDnsCfg();
  beginWifiAttempt();
  lastDashMs = millis();
  lastPeriodicPullMs = millis();
}

void loop() {
  uint32_t loopStarted=micros();
  if(restartAt && int32_t(millis()-restartAt)>=0) { cancelDownload=true; ESP.restart(); }
  // 收尾仍在进行中的面板刷新。一帧流式写入完成后，玻璃面板还需要
  // 约 15 s 才能稳定，这段时间就花在这里而不是绘图调用内部，
  // 因此 DNS 仍能持续得到应答。
  web.handleClient();
  serviceDownload();
  serviceUploads();

  if (WiFi.status() == WL_CONNECTED) {
    handleDns();
    pollUpstream(dnsBuf, sizeof(dnsBuf));
    handleTcpDns();
    expirePending();
  }

  epdServiceRefresh();
  uint32_t now = millis();
  static uint32_t cacheSweep=0;
  if(now-cacheSweep>=1000) { dnsCache.expire(now); cacheSweep=now; }

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

  // BOOT 键使用非阻塞去抖；按住或松开时仍服务 DNS/HTTP。
  static bool rawPressed = false;
  static bool pressed = false;
  static bool holdHandled = false;
  static uint32_t rawSince = 0;
  static uint32_t pressedSince = 0;
  bool refreshRequested = false;
  const bool sample = digitalRead(PIN_CFG_BTN) == LOW;
  if (sample != rawPressed) {
    rawPressed = sample;
    rawSince = now;
  }
  if (rawPressed != pressed && now - rawSince >= BTN_POLL_MS) {
    pressed = rawPressed;
    if (pressed) {
      holdHandled = false;
      pressedSince = rawSince;
    } else if (!holdHandled) {
      const uint32_t heldMs = rawSince - pressedSince;
      if (heldMs >= CFG_BTN_HOLD_MS) {
        holdHandled = true;
        startConfigPortal();
      } else {
        refreshRequested = heldMs >= SHORT_PRESS_MIN_MS;
      }
    }
  }
  if (pressed && rawPressed && !holdHandled && now - pressedSince >= CFG_BTN_HOLD_MS) {
    holdHandled = true;
    LOGN("[BTN] 检测到长按，打开配网门户");
    startConfigPortal();
  }
  pendingRefresh = pendingRefresh || refreshRequested;
  if (portalMode && pendingPortalMessage && !epdRefreshActive()) {
    pendingPortalMessage = false;
    dashboardMessage("配网模式", AP_SSID_DEFAULT, "浏览器打开 192.168.4.1");
  }
  if (!portalMode && !epdRefreshActive() &&
      (pendingRefresh || now - lastDashMs >= DASH_REFRESH_MS)) {
    pendingRefresh = false;
    dashboardDraw();
    lastDashMs = now;
  }

  // 定期拉取黑名单，只在 DNS 已经安静下来之后执行。
  if (!portalMode && WiFi.status() == WL_CONNECTED &&
      updateIntervalH > 0 && updateUrl.length() &&
      uint64_t(now - lastPeriodicPullMs) >= uint64_t(updateIntervalH) * 3600000ULL &&
      now - lastDnsActivityMs > REMOTE_FETCH_IDLE_MS) {
    fetchBlocklist(updateUrl);
    lastPeriodicPullMs = now;
  }

  serviceWifi();

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
  uint32_t elapsedUs=micros()-loopStarted; if(elapsedUs>maxLoopUs) maxLoopUs=elapsedUs;
}