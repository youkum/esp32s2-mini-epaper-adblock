#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
inline uint16_t dns16(const uint8_t* p) { return uint16_t(p[0])<<8 | p[1]; }
inline uint32_t dns32(const uint8_t* p) { return uint32_t(dns16(p))<<16 | dns16(p+2); }
inline void dnsPut32(uint8_t* p, uint32_t n) { p[0]=n>>24; p[1]=n>>16; p[2]=n>>8; p[3]=n; }
// 仅接受一个未压缩问题；拒绝越界、嵌入 NUL 与畸形标签。
inline int dnsQuestionEnd(const uint8_t* p, size_t len) {
  if (len<17 || dns16(p+4)!=1 || (p[2]&0x78)) return 0;
  size_t at=12, wire=0;
  while (at<len) {
    uint8_t n=p[at++]; ++wire;
    if (!n) return wire<=255 && at+4<=len ? int(at+4) : 0;
    if (n>63 || at+n>=len || (wire+=n)>255) return 0;
    for (size_t j=0;j<n;++j) if (p[at+j]<33 || p[at+j]>126 || p[at+j]=='.') return 0;
    at+=n;
  }
  return 0;
}
inline bool dnsReplyMatches(const uint8_t* q, size_t qlen, const uint8_t* r, size_t rlen) {
  int qe=dnsQuestionEnd(q,qlen), re=dnsQuestionEnd(r,rlen);
  return qe && qe==re && (r[2]&0x80) && !(q[2]&0x80) &&
         q[0]==r[0] && q[1]==r[1] && !memcmp(q+12,r+12,size_t(qe-12));
}
inline bool dnsSkipName(const uint8_t* p, size_t len, size_t& at) {
  size_t wire=0;
  while (at<len && wire<=255) {
    size_t start=at;
    uint8_t n=p[at++]; ++wire;
    if (!n) return true;
    if ((n&0xc0)==0xc0) {
      if (at>=len) return false;
      size_t target=((n&63)<<8)|p[at++];
      // 指针必须向后且不得指向头部；另遍历目标链防止循环/非法编码。
      if (target<12 || target>=start) return false;
      size_t scan=target, steps=0;
      while (scan<len && ++steps<=255) {
        size_t origin=scan; uint8_t b=p[scan++];
        if (!b) return true;
        if ((b&0xc0)==0xc0) {
          if (scan>=len) return false;
          size_t next=((b&63)<<8)|p[scan];
          if (next<12 || next>=origin) return false;
          scan=next;
        } else { if (b>63 || scan+b>=len) return false; scan+=b; }
      }
      return false;
    }
    if (n>63 || at+n>=len) return false;
    at+=n; wire+=n;
  }
  return false;
}
// 保守缓存：单问题、无 EDNS、仅 A/AAAA 查询、正响应、无权威/附加节。
// 原样匹配问题字节（包括大小写）、QTYPE/QCLASS 和标志，不混用 DNSSEC 语义。
class DnsCache {
  static constexpr size_t SLOTS=16, MAX_PACKET=512, MAX_TTLS=16;
  struct Entry {
    uint8_t packet[MAX_PACKET]; uint16_t len=0, qend=0, flags=0;
    uint16_t ttlAt[MAX_TTLS]; uint8_t ttls=0;
    uint32_t stored=0, lifetime=0, used=0;
  } entries[SLOTS] = {};
  uint32_t clock=0;
  bool eligible(const uint8_t* q,size_t len) const {
    int end=dnsQuestionEnd(q,len);
    return end && size_t(end)==len && !(q[2]&0xfe) && q[3]==0 &&
           !dns16(q+6) && !dns16(q+8) && !dns16(q+10) &&
           (dns16(q+end-4)==1 || dns16(q+end-4)==28) && dns16(q+end-2)==1;
  }
public:
  uint32_t hits=0, misses=0, inserts=0;
  void clear() { for(auto& e:entries) e.len=0; }
  void expire(uint32_t now) { for(auto& e:entries) if(e.len && (now-e.stored)/1000>=e.lifetime) e.len=0; }
  bool get(const uint8_t* q,size_t len,uint32_t now,uint8_t* out,size_t& outLen) {
    if (!eligible(q,len)) { ++misses; return false; }
    for(auto& e:entries) {
      uint32_t age=(now-e.stored)/1000;
      if (!e.len) continue;
      if (age>=e.lifetime) { e.len=0; continue; }
      if (len!=e.qend || dns16(q+2)!=e.flags || memcmp(q+12,e.packet+12,len-12)) continue;
      memcpy(out,e.packet,e.len); out[0]=q[0]; out[1]=q[1]; outLen=e.len;
      for(size_t i=0;i<e.ttls;++i) {
        uint16_t at=e.ttlAt[i]; uint32_t original=dns32(out+at);
        dnsPut32(out+at,original>age ? original-age : 0);
      }
      e.used=++clock; ++hits; return true;
    }
    ++misses; return false;
  }
  bool put(const uint8_t* q,size_t qlen,const uint8_t* r,size_t rlen,uint32_t now) {
    if (!eligible(q,qlen) || rlen>MAX_PACKET || !dnsReplyMatches(q,qlen,r,rlen) ||
        (r[2]&0x02) || (r[3]&0x0f) || dns16(r+8) || dns16(r+10)) return false;
    uint16_t count=dns16(r+6), offsets[MAX_TTLS];
    if (!count || count>MAX_TTLS) return false;
    size_t at=qlen; uint32_t life=300;
    for(uint16_t i=0;i<count;++i) {
      if (!dnsSkipName(r,rlen,at) || at+10>rlen) return false;
      uint16_t type=dns16(r+at), cls=dns16(r+at+2), n=dns16(r+at+8);
      uint32_t ttl=dns32(r+at+4); offsets[i]=uint16_t(at+4);
      if (!ttl || ttl>0x7fffffff || cls!=1 || (type!=1 && type!=28 && type!=5)) return false;
      if (ttl<life) life=ttl;
      at+=10;
      if (at+n>rlen || (type==1 && n!=4) || (type==28 && n!=16)) return false;
      if (type==5) { size_t end=at; if(!dnsSkipName(r,rlen,end) || end!=at+n) return false; }
      at+=n;
    }
    if (at!=rlen) return false;
    Entry* slot=&entries[0];
    for(auto& e:entries) { if(!e.len) {slot=&e;break;} if(e.used<slot->used) slot=&e; }
    memcpy(slot->packet,r,rlen); memcpy(slot->ttlAt,offsets,count*sizeof(uint16_t));
    slot->len=rlen; slot->qend=qlen; slot->flags=dns16(q+2); slot->ttls=count;
    slot->stored=now; slot->lifetime=life; slot->used=++clock; ++inserts; return true;
  }
};
