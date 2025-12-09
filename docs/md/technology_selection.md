# TCP 抓包工具技术选型调研方案

## 1. 项目概述

本文档针对 TCP 层抓包工具的技术选型进行调研分析，主要涵盖抓包、解包、Wireshark 插件开发等关键技术方案。

## 2. 抓包技术选型

### 2.1 主流抓包库对比

| 库名称 | 语言支持 | 性能 | 跨平台 | 学习成本 | 推荐度 |
|--------|----------|------|--------|----------|--------|
| **libpcap** | C/C++, Python(scapy), Go(gopacket) | 高 | 优秀 | 中等 | 5 |
| **WinPcap/Npcap** | C/C++, Python | 高 | Windows专用 | 中等 | 4 |
| **AF_PACKET** | C/C++ | 极高 | Linux专用 | 高 | 3 |
| **Raw Socket** | 多语言 | 高 | 受限 | 高 | 2 |

### 2.2 推荐方案：libpcap

**选择理由：**
- **跨平台兼容性**：Linux、macOS、Windows 全支持
- **性能优秀**：底层 C 实现，零拷贝优化
- **生态丰富**：多语言绑定，文档完善
- **工业标准**：Wireshark、tcpdump 等知名工具均基于此

**使用示例：**

```cpp
/**
 * simple_sniffer.cpp - 单文件抓包示例 (C++11)
 * g++ -std=c++11 simple_sniffer.cpp -lpcap -o simple_sniffer
 * 运行示例:
 *   ./simple_sniffer           // 交互式选择网卡并抓包
 *   ./simple_sniffer eth0      // 直接抓取 eth0
 *   ./simple_sniffer --list    // 仅列出网卡
 */
 #include <pcap.h>
 #include <iostream>
 #include <iomanip>
 #include <cstring>
 #include <signal.h>
 #include <netinet/in.h>
 #include <netinet/if_ether.h>   // Ethernet header
 #include <netinet/ip.h>         // IP header
 #include <netinet/udp.h>        // UDP header
 #include <netinet/tcp.h>        // TCP header
 #include <arpa/inet.h>
#include <cctype>
#include <cstdio>
#include <memory>
#include <vector>
#include <regex>
#include <string>
#include <thread>
#include <chrono>

// 以十六进制 + ASCII 形式打印载荷数据
void print_hex(const u_char* data, size_t len)
{
    const size_t BYTES_PER_LINE = 16;
    for(size_t i = 0; i < len; ++i) {
        if(i % BYTES_PER_LINE == 0) std::printf("%04zx : ", i);
        std::printf("%02x ", data[i]);
        if(i % BYTES_PER_LINE == BYTES_PER_LINE - 1 || i == len - 1) {
            size_t pad = (BYTES_PER_LINE - 1) - (i % BYTES_PER_LINE);
            for(size_t j = 0; j < pad; ++j) std::printf("   ");
            std::printf(" | ");
            size_t line_start = i - (i % BYTES_PER_LINE);
            for(size_t j = line_start; j <= i; ++j)
                std::printf("%c", std::isprint(data[j]) ? data[j] : '.');
            std::printf("\n");
        }
    }
}

// ===========================
// 协议解析插件接口与示例实现
// ===========================
class ProtocolPlugin {
public:
    virtual ~ProtocolPlugin() = default;
    virtual const char* name() const = 0;
    virtual bool supports_port(uint16_t port) const = 0;
    virtual bool parse(const uint8_t* data, size_t len) = 0;
};

class HttpPlugin : public ProtocolPlugin {
public:
    const char* name() const override { return "HTTP"; }
    bool supports_port(uint16_t port) const override {
        return port == 80 || port == 8080 || port == 8000;
    }
    bool parse(const uint8_t* data, size_t len) override {
        std::string text(reinterpret_cast<const char*>(data), len);
        std::regex firstLineRegex("^(GET|POST|PUT|DELETE|HTTP/)", std::regex::icase);
        if (std::regex_search(text, firstLineRegex)) {
            auto endPos = text.find("\r\n");
            std::cout << "[HTTP] " << text.substr(0, endPos) << "\n";
            return true;
        }
        return false;
    }
};

static std::vector<std::unique_ptr<ProtocolPlugin>> g_plugins;

static volatile bool keep_running = true;
 void sigint_handler(int) { keep_running = false; std::cout << "\n停止抓包...\n"; }
 
 bool is_network_device(const pcap_if_t* d)
{
    // 排除 loopback
    // if (d->flags & PCAP_IF_LOOPBACK) return false;
    // 至少要有一个 IPv4 或 IPv6 地址
    for (pcap_addr_t* addr = d->addresses; addr; addr = addr->next)
        if (addr->addr && (addr->addr->sa_family == AF_INET ||
                           addr->addr->sa_family == AF_INET6))
            return true;
    return false;
}

void list_devices()
{
    // 调用 pcap_findalldevs 获取接口列表，结果存于 alldevs 链表
    pcap_if_t *alldevs=nullptr; char errbuf[PCAP_ERRBUF_SIZE]{};
    if (pcap_findalldevs(&alldevs, errbuf)==-1) {
        std::cerr << "获取网卡失败: " << errbuf << '\n'; return;
    }
    std::cout << "可用网卡列表\n--------------------------\n";
    int idx=0;
    for (pcap_if_t *d=alldevs; d; d=d->next)
    {
        if(!is_network_device(d)) continue;
        std::cout << std::setw(2) << idx++ << ": " << d->name;
        if (d->description) std::cout << " (" << d->description << ')';
        std::cout << '\n';
    }
    if(!idx) std::cout << "未检测到任何接口\n";
    pcap_freealldevs(alldevs);
}
 
 const char* choose_device_interactively()
{
    static char selected[128]{};
    // 调用 pcap_findalldevs 获取接口列表，结果存于 alldevs 链表
    pcap_if_t *alldevs=nullptr; char errbuf[PCAP_ERRBUF_SIZE]{};
    if (pcap_findalldevs(&alldevs, errbuf)==-1) { std::cerr<<errbuf<<'\n'; return nullptr; }

    list_devices();
    std::cout << "输入要抓取的网卡编号: ";
    int choice=-1; std::cin >> choice;

    int idx = 0; pcap_if_t* target = nullptr;
    for(pcap_if_t* d = alldevs; d; d = d->next) {
        if(!is_network_device(d)) continue;
        if(idx == choice) { target = d; break; }
        ++idx;
    }
    if(!target) { std::cerr<<"编号无效\n"; pcap_freealldevs(alldevs); return nullptr; }
    strncpy(selected, target->name, sizeof(selected)-1);
    pcap_freealldevs(alldevs);
    return selected;
}
 
 void packet_handler(u_char *user, const struct pcap_pkthdr *h, const u_char *bytes)
 {
     (void)user;
     static unsigned long pkt_no=0;
     ++pkt_no;
     std::cout << "\n=== Packet #" << pkt_no << " (" << h->len << " bytes) ===\n";
 
     if(h->caplen < sizeof(ether_header)) return;
     auto eth = reinterpret_cast<const ether_header*>(bytes);
     if(ntohs(eth->ether_type) != ETHERTYPE_IP) {
         std::cout << "非 IPv4 数据包\n"; return;
     }
 
     auto ip = reinterpret_cast<const iphdr*>(bytes + sizeof(ether_header));
     char src_ip[INET_ADDRSTRLEN]{}, dst_ip[INET_ADDRSTRLEN]{};
     inet_ntop(AF_INET, &ip->saddr, src_ip, INET_ADDRSTRLEN);
     inet_ntop(AF_INET, &ip->daddr, dst_ip, INET_ADDRSTRLEN);
 
     std::cout << "IP  " << src_ip << " -> " << dst_ip << "  ";
     switch(ip->protocol) {
                 case IPPROTO_TCP: {
            auto ip_header_len = ip->ihl * 4;
            auto tcp = reinterpret_cast<const tcphdr*>(
                bytes + sizeof(ether_header) + ip_header_len);
            std::cout << "TCP " << ntohs(tcp->source) << "->" << ntohs(tcp->dest);

            size_t tcp_header_len = tcp->doff * 4;
            size_t payload_offset = sizeof(ether_header) + ip_header_len + tcp_header_len;
            if(h->caplen > payload_offset) {
                size_t payload_len = h->caplen - payload_offset;
                std::cout << "  Payload: " << payload_len << " bytes\n";
                print_hex(bytes + payload_offset, payload_len);
            } else {
                std::cout << "  (无 Payload)\n";
            }
            break;
        }
                 case IPPROTO_UDP: {
            auto ip_header_len = ip->ihl * 4;
            auto udp = reinterpret_cast<const udphdr*>(
                bytes + sizeof(ether_header) + ip_header_len);
            std::cout << "UDP " << ntohs(udp->source) << "->" << ntohs(udp->dest);

            size_t payload_offset = sizeof(ether_header) + ip_header_len + sizeof(udphdr);
            if(h->caplen > payload_offset) {
                size_t payload_len = h->caplen - payload_offset;
                std::cout << "  Payload: " << payload_len << " bytes\n";
                print_hex(bytes + payload_offset, payload_len);
            } else {
                std::cout << "  (无 Payload)\n";
            }
            break;
        }
         default:
             std::cout << "协议号 " << int(ip->protocol) << '\n';
     }
 }
 
 int main(int argc, char* argv[])
 {
     signal(SIGINT, sigint_handler);  // Ctrl+C
    // 注册协议解析插件
    g_plugins.emplace_back(std::unique_ptr<ProtocolPlugin>(new HttpPlugin()));
     if(argc>=2 && (std::string(argv[1])=="-l"||std::string(argv[1])=="--list")) {
         list_devices(); return 0;
     }
 
     const char* dev = nullptr;
     if(argc>=2) {
         dev = argv[1];
     } else {
         dev = choose_device_interactively();
         if(!dev) return 1;
     }
     std::cout << "开始抓取接口: " << dev << " (Ctrl+C 终止)\n";
 
         char errbuf[PCAP_ERRBUF_SIZE]{};
    // 打开接口进行抓包：BUFSIZ=最大抓取字节数，1=混杂模式，1000ms=超时
    pcap_t* handle = pcap_open_live(dev, BUFSIZ, 1, 1000, errbuf);
     if(!handle) { std::cerr << "打开接口失败: " << errbuf << '\n'; return 1; }
 
     // 可选: 仅抓 IP 包
         struct bpf_program fp; // BPF 过滤器对象
    // 编译过滤表达式 "ip" (仅捕获 IP 数据包)，成功后再把过滤规则应用到会话
    if (pcap_compile(handle, &fp, "ip", 0, 0) == 0) {
        pcap_setfilter(handle, &fp);
    }
 
     // 抓包循环
         // pcap_dispatch 从缓冲区提取数据包并触发回调函数 packet_handler
    while (keep_running) {
        if (pcap_dispatch(handle, 0, packet_handler, nullptr) == -1) break;
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
 
         // 关闭 pcap 会话并释放资源
    pcap_close(handle);
    std::cout << "抓包结束\n";
     return 0;
 }
```

### 2.3 开发环境依赖

#### Ubuntu / Debian
```bash
sudo apt update
sudo apt install build-essential cmake libpcap-dev
```

#### CentOS / RHEL
```bash
sudo yum install gcc-c++ cmake libpcap-devel
# 或在较新系统中
sudo dnf install gcc-c++ cmake libpcap-devel
```

## 3. 解包技术选型

### 3.1 协议解析库对比

| 解析方案 | 优势 | 劣势 | 适用场景 |
|----------|------|------|----------|
| **手工解析** | 灵活、高效 | 开发量大、易出错 | 私有协议 |
| **Scapy** | 功能强大、易用 | 性能一般 | 原型开发、测试 |
| **dpkt** | 轻量、快速 | 功能有限 | 简单协议 |
| **构造体映射** | 高效、直观 | 字节序问题 | 固定格式协议 |

### 3.2 推荐方案：分层解析架构

**核心思路：**
1. **L2-L4 标准解析**：使用成熟库处理以太网/IP/TCP
2. **应用层插件化**：针对私有协议开发专用解析器
3. **TCP 流重组**：处理分片、乱序、重传


## 4. Wireshark 插件开发

### 4.1 插件开发方式对比

| 方式 | 语言 | 难度 | 功能 | 分发 |
|------|------|------|------|------|
| **Lua 脚本** | Lua | 低 | 基础解析 | 简单 |
| **C 插件** | C | 高 | 完整功能 | 复杂 |
| **Python 解析器** | Python | 中 | 中等功能 | 中等 |

### 4.2 推荐方案：Lua 脚本插件

**选择理由：**
- **开发简单**：语法简洁，调试方便
- **集成度高**：Wireshark 原生支持
- **部署容易**：单文件分发

**Wireshark Lua 插件示例：**
```lua
-- 私有协议解析器
local myproto = Proto("MyProtocol", "My Private Protocol")

-- 定义字段
local f_magic = ProtoField.uint32("myproto.magic", "Magic Number", base.HEX)
local f_type = ProtoField.uint8("myproto.type", "Message Type")
local f_length = ProtoField.uint16("myproto.length", "Data Length")
local f_data = ProtoField.bytes("myproto.data", "Data")

myproto.fields = {f_magic, f_type, f_length, f_data}

-- 解析函数
function myproto.dissector(buffer, pinfo, tree)
    local length = buffer:len()
    if length == 0 then return end
    
    pinfo.cols.protocol = myproto.name
    local subtree = tree:add(myproto, buffer(), "My Protocol Data")
    
    -- 解析各字段
    subtree:add_le(f_magic, buffer(0,4))
    subtree:add(f_type, buffer(4,1))
    subtree:add_le(f_length, buffer(5,2))
    
    local data_len = buffer(5,2):le_uint()
    if length >= 7 + data_len then
        subtree:add(f_data, buffer(7, data_len))
    end
end

-- 注册端口
local tcp_port = DissectorTable.get("tcp.port")
tcp_port:add(8080, myproto)
```

### 7.2.1 C++ 插件示例
```cpp
// HTTPPlugin.h - 最小 C++ 插件示例
class ProtocolPlugin {
public:
    virtual ~ProtocolPlugin() = default;
    virtual const char* name() const = 0;
    virtual std::vector<uint16_t> ports() const = 0;
    virtual bool parse(const uint8_t* data, size_t len) = 0;
};

class HTTPPlugin : public ProtocolPlugin {
public:
    const char* name() const override { return "HTTP"; }
    std::vector<uint16_t> ports() const override { return {80, 8080, 8000}; }
    bool parse(const uint8_t* data, size_t len) override {
        if(len < 4) return false;
        std::string_view sv(reinterpret_cast<const char*>(data), len);
        return sv.starts_with("GET") || sv.starts_with("POST") || sv.starts_with("HTTP/");
    }
};
```

### 7.2.2 Python 插件示例 (精简版)
```python
class HTTPPlugin(ProtocolPlugin):
    def name(self):
        return "HTTP"

    def supported_ports(self):
        return [80, 8080, 8000]

    def parse(self, data: bytes):
        text = data[:200].decode(errors='ignore')
        if text.startswith(('GET', 'POST', 'HTTP/')):
            return {"first_line": text.split('\r\n', 1)[0]}
        return None
```



## 6. 语言选择对比分析

### 6.1 综合对比表

| 语言 | 抓包性能 | 开发效率 | 插件生态 | 跨平台 | 推荐度 |
|------|----------|----------|----------|---------|--------|
| **Python** | 中 | 高 | 优秀 | 优秀 | 4 |
| **C++** | 极高 | 低 | 中等 | 良好 | 3 |
| **Go** | 高 | 高 | 良好 | 优秀 | 4 |
| **Rust** | 极高 | 中 | 发展中 | 优秀 | 3 |

### 6.2 推荐方案：Python 主导 + C++ 优化

**架构设计：**
1. **Python 主程序**：逻辑控制、插件管理
2. **C++ 核心模块**：高性能抓包、TCP 重组
3. **进程间通信**：Unix Socket 或共享内存

**优势：**
- 开发效率高，适合快速迭代
- 性能关键部分用 C++ 优化
- 插件开发简单，Python 生态丰富




## 9. 总结与建议

### 9.1 推荐技术栈

**核心组合：**
- **抓包**：libpcap + Python scapy
- **解包**：插件化架构 + TCP 流重组
- **Wireshark 插件**：Lua 脚本
- **界面**：PySide2 + pyqtgraph
- **语言**：Python 主导，C++ 性能优化

