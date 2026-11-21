#!/usr/bin/env python3
"""简易实机 DNS 探针；只有用户显式指定设备地址才发送查询。"""
import argparse, random, socket, statistics, struct, time

def make_query(domain, qtype):
    packet=struct.pack('!HHHHHH',random.randrange(65536),0x100,1,0,0,0)
    labels=domain.rstrip('.').split('.')
    for label in labels:
        data=label.encode('idna')
        if not 1<=len(data)<=63:raise ValueError('DNS 标签长度无效')
        packet+=bytes([len(data)])+data
    if len(packet)-12>253:raise ValueError('域名过长')
    return packet+b'\0'+struct.pack('!HH',qtype,1)

def receive_exact(sock,n):
    out=b''
    while len(out)<n:
        data=sock.recv(n-len(out))
        if not data:raise OSError('连接提前关闭')
        out+=data
    return out

def main():
    ap=argparse.ArgumentParser(description='测量设备 DNS 响应，不修改设备配置')
    ap.add_argument('--server',required=True)
    ap.add_argument('--domain',default='example.com')
    ap.add_argument('--count',type=int,default=20)
    ap.add_argument('--interval',type=float,default=0.05)
    ap.add_argument('--timeout',type=float,default=5.5)
    ap.add_argument('--tcp',action='store_true')
    ap.add_argument('--aaaa',action='store_true')
    ap.add_argument('--unique',action='store_true',help='每次加不同前缀，避免命中正响应缓存')
    a=ap.parse_args()
    if a.count<1 or a.interval<0:ap.error('次数必须为正，间隔不得为负')
    results=[];failures=0;rcodes={}
    for i in range(a.count):
        domain=(f'probe-{time.time_ns()}.' if a.unique else '')+a.domain
        query=make_query(domain,28 if a.aaaa else 1)
        started=time.perf_counter()
        try:
            kind=socket.SOCK_STREAM if a.tcp else socket.SOCK_DGRAM
            with socket.socket(socket.AF_INET,kind) as sock:
                sock.settimeout(a.timeout);sock.connect((a.server,53))
                if a.tcp:
                    sock.sendall(struct.pack('!H',len(query))+query)
                    length=struct.unpack('!H',receive_exact(sock,2))[0]
                    reply=receive_exact(sock,length)
                else:
                    sock.send(query);reply=sock.recv(4096)
                if len(reply)<12 or reply[:2]!=query[:2] or not reply[2]&0x80:raise OSError('应答头或 ID 无效')
                rcode=reply[3]&15;rcodes[rcode]=rcodes.get(rcode,0)+1
                results.append((time.perf_counter()-started)*1000)
        except (OSError,ValueError) as e:
            failures+=1;print(f'查询 {i+1} 失败：{e}')
        time.sleep(a.interval)
    print(f'收到 {len(results)}/{a.count}，失败 {failures}，RCODE 分布 {rcodes}')
    if results:
        ordered=sorted(results);p95=ordered[min(len(ordered)-1,int(len(ordered)*0.95))]
        print(f'p50={statistics.median(results):.2f} ms，p95={p95:.2f} ms，最大={max(results):.2f} ms')
    print('这是串行探针，间隔是每次应答后的休眠，不等于固定 QPS 压力或同时突发测试。')
if __name__=='__main__':main()
