#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
// 有界 HTTP/1.x 响应读取器。读、写和取消由适配器提供；不依赖 Arduino。
// 支持 Content-Length、chunked 或连接关闭封帧；拒绝 gzip、跳转与矛盾头部。
template<class IO> class HttpReader {
  IO& io;
  bool line(char* out,size_t cap) {
    size_t used=0;
    while(used+1<cap) {
      uint8_t c;
      if(io.read(&c,1)!=1) return false;
      if(c=='\n') {
        if(!used || out[used-1]!='\r') return false;
        out[used-1]=0; return true;
      }
      if(c==0 || (c<32 && c!='\r' && c!='\t')) return false;
      out[used++]=char(c);
    }
    return false;
  }
  bool number(const char* s,uint32_t& n,int base) {
    n=0; if(!*s) return false;
    for(;*s;++s) {
      char c=*s; unsigned v=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:255;
      if(v>=unsigned(base) || n>(UINT32_MAX-v)/base) return false;
      n=n*base+v;
    } return true;
  }
  bool copy(uint32_t bytes) {
    uint8_t buf[1024];
    while(bytes) {
      size_t want=bytes<sizeof(buf)?bytes:sizeof(buf);
      int n=io.read(buf,want);
      if(n<=0 || !io.write(buf,size_t(n))) return false;
      bytes-=n;
    } return true;
  }
public:
  explicit HttpReader(IO& value):io(value) {}
  bool run() {
    char text[1024];
    if(!line(text,sizeof(text)) || strlen(text)<12 ||
       (strncmp(text,"HTTP/1.1 ",9) && strncmp(text,"HTTP/1.0 ",9)) || strncmp(text+9,"200",3) || (text[12] && text[12]!=' ')) return false;
    bool lengthSeen=false,encodingSeen=false,chunked=false;
    uint32_t length=0; size_t headerBytes=0;
    for(size_t count=0;;++count) {
      if(count>=64 || !line(text,sizeof(text)) || (headerBytes+=strlen(text)+2)>16384) return false;
      if(!*text) break;
      char* colon=strchr(text,':'); if(!colon) return false;
      *colon=0;
      for(char* p=text;*p;++p) if(*p>='A'&&*p<='Z') *p+=32;
      char* value=colon+1; while(*value==' '||*value=='\t') ++value;
      size_t n=strlen(value); while(n && (value[n-1]==' '||value[n-1]=='\t')) value[--n]=0;
      if(!strcmp(text,"content-length")) {
        if(lengthSeen || !number(value,length,10)) return false;
        lengthSeen=true;
      } else if(!strcmp(text,"transfer-encoding")) {
        if(encodingSeen) return false;
        encodingSeen=true;
        for(char* p=value;*p;++p) if(*p>='A'&&*p<='Z') *p+=32;
        if(strcmp(value,"chunked")) return false;
        chunked=true;
      } else if(!strcmp(text,"content-encoding") && strcmp(value,"identity")) return false;
    }
    if(chunked && lengthSeen) return false;
    if(lengthSeen) { io.total(length); return copy(length); }
    io.total(0);
    if(chunked) {
      for(size_t chunks=0;chunks<65536;++chunks) {
        if(!line(text,sizeof(text))) return false;
        char* extension=strchr(text,';'); if(extension) *extension=0;
        uint32_t n; if(!number(text,n,16)) return false;
        if(!n) {
          for(int trailers=0;trailers<32;++trailers) {
            if(!line(text,sizeof(text))) return false;
            if(!*text) return true;
          } return false;
        }
        if(!copy(n)) return false;
        uint8_t end[2];
        if(io.read(end,1)!=1 || io.read(end+1,1)!=1 || end[0]!='\r' || end[1]!='\n') return false;
      } return false;
    }
    uint8_t buf[1024];
    for(;;) { int n=io.read(buf,sizeof(buf)); if(n<0) return false; if(!n) return true; if(!io.write(buf,size_t(n))) return false; }
  }
};
