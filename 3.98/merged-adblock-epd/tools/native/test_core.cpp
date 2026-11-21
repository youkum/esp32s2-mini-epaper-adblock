#include "bloom_hash.h"
#include "top_counter.h"
#include "dns_cache.h"
#include "http_reader.h"
#include "epd_driver.h"
#include "config.h"
#include <cassert>
#include <vector>
#include <string>
#include <cstdio>
#include <cstring>
#include <random>
static unsigned long fakeNow=0;
static int pins[50]={};
static int byteBits=0,byteValue=0;
struct Byte { uint8_t value; bool data; };
static std::vector<Byte> trace;
FakeSerial Serial;
unsigned long millis(){return fakeNow;}
void pinMode(int,int){}
void delay(unsigned long){assert(false && "状态机不得 delay");}
void digitalWrite(int pin,int value){
  pins[pin]=value;
  if(pin==PIN_EPD_SCK && value==HIGH && pins[PIN_EPD_CS]==LOW){
    byteValue=(byteValue<<1)|pins[PIN_EPD_MOSI];
    if(++byteBits==8){trace.push_back({uint8_t(byteValue),pins[PIN_EPD_DC]==HIGH});byteBits=byteValue=0;}
  }
}
int digitalRead(int pin){return pins[pin];}
static int bands=0;
static void paint(int base,int rows,uint8_t* buf){
  assert(base>=0 && rows>0 && rows<=64 && base+rows<=H);
  ++bands; memset(buf,0x1b,size_t(rows)*W/4);
}
static std::vector<uint8_t> query(const char* domain="example.com",uint16_t type=1){
  std::vector<uint8_t> p={0x12,0x34,1,0,0,1,0,0,0,0,0,0};
  const char* at=domain;
  while(*at){const char* end=strchr(at,'.');size_t n=end?size_t(end-at):strlen(at);p.push_back(n);p.insert(p.end(),at,at+n);if(!end)break;at=end+1;}
  p.insert(p.end(),{0,uint8_t(type>>8),uint8_t(type),0,1}); return p;
}
static std::vector<uint8_t> response(const std::vector<uint8_t>& q,uint32_t ttl=60){
  auto r=q;r[2]=0x81;r[3]=0x80;r[7]=1;
  uint8_t ans[]={0xc0,0x0c,0,1,0,1,0,0,0,0,0,4,1,2,3,4};dnsPut32(ans+6,ttl);
  r.insert(r.end(),ans,ans+16);return r;
}
struct MemoryIO {
  std::string input,output;size_t at=0,limit=10000;uint32_t expected=0;
  int read(uint8_t* p,size_t n){n=std::min(n,input.size()-at);if(!n)return 0; memcpy(p,input.data()+at,n);at+=n;return int(n);}
  bool write(const uint8_t* p,size_t n){if(output.size()+n>limit)return false;output.append((const char*)p,n);return true;}
  void total(uint32_t n){expected=n;}
};
static int checks=0;
static void test(bool v){assert(v);++checks;}
int main(){
  TopCounter<12> top;TopEntry out[12];
  top.record("a.example");top.record("b.example");top.record("b.example");top.get(out,12);
  test(std::string(out[0].domain)=="b.example" && out[0].count==2 && out[0].error==0);
  for(int i=0;i<30;++i){auto d=std::to_string(i)+".example";top.record(d.c_str());}
  for(int i=0;i<80;++i) { top.record("new-hot.example"); }
  top.get(out,12);
  test(std::string(out[0].domain)=="new-hot.example" && out[0].count-out[0].error==80);
  for(int i=1;i<12;++i)test(out[i-1].count>=out[i].count);
  std::string longName(62,'a');longName+="."+std::string(62,'b')+".example";top.record(longName.c_str());top.record(longName.c_str());
  top.get(out,12);bool found=false;for(auto& t:out)if(t.domain==longName){found=true;test(t.count-t.error==2);}test(found);
  top.clear();top.get(out,12);test(!out[0].domain[0]);
  std::mt19937_64 rng(42);
  for(int i=0;i<1000;++i){uint64_t h=rng();uint32_t bits=uint32_t(rng()%16777216+1);uint32_t step=uint32_t(h>>32);if(!step)step=UINT32_MAX;for(uint8_t k=0;k<32;++k)test(bloomPosition(h,bits,k)==(uint64_t(uint32_t(h))+uint64_t(k)*step)%bits);}
  test(bloomPosition(0,1000000,31)==(uint64_t(31)*UINT32_MAX)%1000000);
  auto q=query(),r=response(q);uint8_t buf[512];size_t len=0;DnsCache cache;
  test(cache.put(q.data(),q.size(),r.data(),r.size(),100));
  q[0]=0xab;q[1]=0xcd;
  test(cache.get(q.data(),q.size(),5100,buf,len) && buf[0]==0xab && buf[1]==0xcd && dns32(buf+len-10)==55);
  test(!cache.get(q.data(),q.size(),60100,buf,len));
  q=query();r=response(q,0);test(!cache.put(q.data(),q.size(),r.data(),r.size(),0));
  r=response(q);r[3]=0x83;test(!cache.put(q.data(),q.size(),r.data(),r.size(),0));
  r=response(q);r[2]|=2;test(!cache.put(q.data(),q.size(),r.data(),r.size(),0));
  r=response(q);r.pop_back();test(!cache.put(q.data(),q.size(),r.data(),r.size(),0));
  r=response(q);r[q.size()+1]=0xff;test(!cache.put(q.data(),q.size(),r.data(),r.size(),0));
  r=response(q);test(cache.put(q.data(),q.size(),r.data(),r.size(),0xfffff000));test(cache.get(q.data(),q.size(),0x1000,buf,len));
  q[3]=0x10;test(!cache.get(q.data(),q.size(),0x1000,buf,len));
  q=query();q[11]=1;q.insert(q.end(),{0,0,41,0,0,0,0,0,0,0,0});test(!cache.put(q.data(),q.size(),r.data(),r.size(),0));
  q=query();q[4]=1;test(!dnsQuestionEnd(q.data(),q.size()));
  q=query();q[12]=64;test(!dnsQuestionEnd(q.data(),q.size()));
  q=query();q[13]=0;test(!dnsQuestionEnd(q.data(),q.size()));
  q=query();r=response(q);r[0]++;test(!dnsReplyMatches(q.data(),q.size(),r.data(),r.size()));
  q=query();r=response(q);r[13]='x';test(!dnsReplyMatches(q.data(),q.size(),r.data(),r.size()));
  cache.clear();q=query();test(!cache.get(q.data(),q.size(),0x1000,buf,len));
  for(int i=0;i<17;++i){auto name=std::to_string(i)+".example";auto a=query(name.c_str()),b=response(a);test(cache.put(a.data(),a.size(),b.data(),b.size(),0));}
  q=query("0.example");test(!cache.get(q.data(),q.size(),1,buf,len));
  std::vector<std::pair<std::string,bool>> http={
    {"HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc",true},
    {"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\na\r\n2;x=y\r\nbc\r\n0\r\n\r\n",true},
    {"HTTP/1.0 200 OK\r\n\r\nabc",true},
    {"HTTP/1.1 302 Found\r\n\r\nabc",false},
    {"HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nabc",false},
    {"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc",false},
    {"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nContent-Length: 3\r\n\r\nabc",false},
    {"HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\n\r\nabc",false},
    {"HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\nabc",false},
    {"HTTP/1.1 200 OK\r\nContent-Length: 4294967296\r\n\r\nabc",false},
    {"HTTP/1.1 200 OK\r\nContent-Length: 3\r\nContent-Length: 3\r\n\r\nabc",false},
    {"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nXYZ\r\nabc",false},
    {"HTTP/1.1 200 OK\r\n"+std::string(1100,'a')+"\r\n\r\nabc",false}
  };
  for(auto& c:http){MemoryIO io;io.input=c.first;HttpReader<MemoryIO> reader(io);test(reader.run()==c.second);if(c.second)test(io.output=="abc");}
  MemoryIO limited;limited.input=http[0].first;limited.limit=2;HttpReader<MemoryIO> limitReader(limited);test(!limitReader.run());
  // 使用真正的 epd_driver.cpp，解码 GPIO 字节轨迹检查每行窗口与像素顺序。
  gpioInit();pins[PIN_EPD_BUSY]=HIGH;test(epdSendFrame(paint));test(!epdSendFrame(paint));
  bool refreshing=false;unsigned long refreshStart=0;
  while(epdRefreshActive() && fakeNow<100000){
    if(std::string(epdStatus())=="面板刷新"){
      if(!refreshing){refreshing=true;refreshStart=fakeNow;}
      pins[PIN_EPD_BUSY]=fakeNow-refreshStart<15000?LOW:HIGH;
    }else pins[PIN_EPD_BUSY]=HIGH;
    epdServiceRefresh();fakeNow+=10;
  }
  test(std::string(epdStatus())=="空闲" && epdFaults()==0 && bands==9);
  int row=0;
  for(size_t i=0;i<trace.size();++i){
    if(!trace[i].data && trace[i].value==0x83 && i+10<trace.size() && !trace[i+10].data && trace[i+10].value==0x10){
      int actual=trace[i+5].value*256+trace[i+6].value;
      test(actual==(row<H/2?2*row+1:2*(H-1-row)));
      test(trace[i+11].value==0xe4);++row;
    }
  }
  test(row==H && byteBits==0);
  test(epdSendFrame(paint));pins[PIN_EPD_BUSY]=LOW;unsigned long start=fakeNow;
  while(epdRefreshActive() && fakeNow-start<200000){epdServiceRefresh();fakeNow+=100;}
  test(!epdRefreshActive() && epdFaults()==3 && std::string(epdStatus())=="故障，等待下次请求");
  test(epdSendFrame(paint));pins[PIN_EPD_BUSY]=HIGH;start=fakeNow;
  while(epdRefreshActive() && fakeNow-start<100000){epdServiceRefresh();fakeNow+=10;}
  test(!epdRefreshActive() && epdFaults()==6);
  // 刷新 BUSY 常低、断电 BUSY 常低、慢服务触发整帧总超时。
  for(int mode=0;mode<3;++mode) {
    uint32_t previous=epdFaults();test(epdSendFrame(paint));start=fakeNow;
    bool seenRefresh=false;unsigned long began=0;
    while(epdRefreshActive() && fakeNow-start<500000) {
      std::string phase=epdStatus();
      if(phase=="面板刷新") {
        if(!seenRefresh){seenRefresh=true;began=fakeNow;}
        pins[PIN_EPD_BUSY]=(mode==0 || fakeNow-began<100)?LOW:HIGH;
      } else {seenRefresh=false;pins[PIN_EPD_BUSY]=(mode==1 && phase=="等待断电")?LOW:HIGH;}
      epdServiceRefresh();fakeNow+=mode==2?500:100;
    }
    test(!epdRefreshActive() && epdFaults()==previous+3);
  }
  q=query();r=response(q,600);test(cache.put(q.data(),q.size(),r.data(),r.size(),0));
  test(cache.get(q.data(),q.size(),1000,buf,len) && dns32(buf+len-10)==599);
  cache.expire(300000);test(!cache.get(q.data(),q.size(),300000,buf,len));
  // CNAME 链各资源记录独立递减，以最短 TTL 限制驻留。
  r=q;r[2]=0x81;r[3]=0x80;r[7]=2;
  uint8_t cn[]={0xc0,0x0c,0,5,0,1,0,0,0,20,0,2,0xc0,0x0c};
  r.insert(r.end(),cn,cn+14);auto a=response(q,60);r.insert(r.end(),a.end()-16,a.end());
  test(cache.put(q.data(),q.size(),r.data(),r.size(),0));
  test(cache.get(q.data(),q.size(),5000,buf,len) && dns32(buf+q.size()+6)==15 && dns32(buf+len-10)==55);
  test(!cache.get(q.data(),q.size(),20000,buf,len));
  r=response(q);r[q.size()+12]=1;r[q.size()+13]=2;r[q.size()+14]=3;r[q.size()+15]=4;
  r[q.size()+3]=5;r[q.size()+11]=4;r[q.size()+12]=0xc0;r[q.size()+13]=0;
  test(!cache.put(q.data(),q.size(),r.data(),r.size(),0));
  printf("C++ 核心断言通过：%d 项\n",checks);
}
