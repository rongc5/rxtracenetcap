# 快速开始 - 协议过滤

## 测试协议定义

### 1. 查看协议结构

```bash
# 查看HTTP协议定义
./bin/debug_parse config/protocols/http.pdef

# 查看DNS协议定义
./bin/debug_parse config/protocols/dns.pdef

# 查看MySQL协议定义
./bin/debug_parse config/protocols/mysql.pdef

# 查看Redis协议定义
./bin/debug_parse config/protocols/redis.pdef

# 查看Memcached协议定义
./bin/debug_parse config/protocols/memcached.pdef
```

### 2. 查看过滤器字节码

```bash
# 查看DNS过滤器的字节码
./bin/test_disasm config/protocols/dns.pdef

# 查看Memcached过滤器的字节码
./bin/test_disasm config/protocols/memcached.pdef
```

## 使用示例

### C++ 示例：捕获HTTP GET请求

```cpp
#include "pdef/pdef_wrapper.h"
#include <iostream>
#include <vector>

int main() {
    // 加载HTTP协议定义
    pdef::ProtocolFilter http_filter;
    if (!http_filter.load("config/protocols/http.pdef")) {
        std::cerr << "Failed to load HTTP protocol: "
                  << http_filter.getError() << std::endl;
        return 1;
    }

    // 模拟HTTP GET请求报文
    // "GET /index.html HTTP/1.1\r\n"
    std::vector<uint8_t> packet = {
        0x47, 0x45, 0x54, 0x20,  // "GET "
        0x2F, 0x69, 0x6E, 0x64,  // "/ind"
        0x65, 0x78, 0x2E, 0x68,  // "ex.h"
        0x74, 0x6D, 0x6C, 0x20,  // "tml "
        // ... 剩余HTTP头部 ...
    };

    // 测试过滤
    bool matched = http_filter.match(packet.data(), packet.size(), 80);

    if (matched) {
        std::cout << "✓ HTTP GET request detected!" << std::endl;
    } else {
        std::cout << "✗ Not an HTTP GET request" << std::endl;
    }

    return 0;
}
```

### C 示例：捕获DNS查询

```c
#include "pdef/parser.h"
#include "runtime/protocol.h"
#include <stdio.h>
#include <stdint.h>

int main() {
    char error[512];

    // 加载DNS协议定义
    ProtocolDef* proto = pdef_parse_file(
        "config/protocols/dns.pdef", error, sizeof(error)
    );

    if (!proto) {
        fprintf(stderr, "Parse error: %s\n", error);
        return 1;
    }

    // 模拟DNS查询报文（查询www.example.com的A记录）
    uint8_t packet[] = {
        // Transaction ID
        0x12, 0x34,
        // Flags (standard query)
        0x01, 0x00,
        // Questions: 1
        0x00, 0x01,
        // Answer RRs: 0
        0x00, 0x00,
        // Authority RRs: 0
        0x00, 0x00,
        // Additional RRs: 0
        0x00, 0x00,
        // ... 域名查询部分 ...
    };

    // 测试过滤（DNS使用端口53）
    bool matched = packet_filter_match(packet, sizeof(packet), 53, proto);

    if (matched) {
        printf("✓ DNS query packet detected!\n");
    } else {
        printf("✗ Not a DNS query\n");
    }

    // 打印协议信息
    protocol_print(proto);

    // 清理
    protocol_free(proto);
    return 0;
}
```

### 实际场景：集成到抓包线程

```cpp
// 在rxcapturethread.cpp中使用

class CaptureThread {
private:
    pdef::ProtocolFilter http_filter_;
    pdef::ProtocolFilter dns_filter_;
    pdef::ProtocolFilter mysql_filter_;

public:
    bool Initialize() {
        // 加载所有需要的协议
        if (!http_filter_.load("config/protocols/http.pdef")) {
            return false;
        }
        if (!dns_filter_.load("config/protocols/dns.pdef")) {
            return false;
        }
        if (!mysql_filter_.load("config/protocols/mysql.pdef")) {
            return false;
        }
        return true;
    }

    void ProcessPacket(const uint8_t* packet, uint32_t len,
                      uint16_t src_port, uint16_t dst_port) {
        // 提取应用层数据
        const uint8_t* app_data = ExtractApplicationData(packet, len);
        uint32_t app_len = GetApplicationDataLength(packet, len);

        // 检查是否匹配HTTP
        if (http_filter_.match(app_data, app_len, dst_port)) {
            SavePacket("http", packet, len);
            return;
        }

        // 检查是否匹配DNS
        if (dns_filter_.match(app_data, app_len, dst_port)) {
            SavePacket("dns", packet, len);
            return;
        }

        // 检查是否匹配MySQL
        if (mysql_filter_.match(app_data, app_len, dst_port)) {
            SavePacket("mysql", packet, len);
            return;
        }
    }
};
```

## 编译和运行

### 编译示例程序

如果要编译上述C++示例：

```bash
g++ -O2 -Wall -Wextra -std=gnu++98 -Isrc -Icore \
    -o my_http_filter my_http_filter.cpp \
    src/pdef/pdef_wrapper.o -Lbin -lpdef
```

如果要编译C示例：

```bash
gcc -O2 -Wall -Wextra -std=c99 -Isrc -Icore \
    -o my_dns_filter my_dns_filter.c \
    -Lbin -lpdef
```

### 运行

```bash
# 确保libpdef.a已经编译
make pdef

# 运行你的程序
./my_http_filter
./my_dns_filter
```

## 调试技巧

### 1. 查看详细的协议信息

```bash
./bin/debug_parse config/protocols/http.pdef
```

输出示例：
```
Protocol: HTTP
Default endian: big
Ports: 80, 8080, 8000, 8888

Constants (9):
  GET_ = 0x47455420 (1195725856)
  POST_ = 0x504f5354 (1347375956)
  ...

Structures (2):
  HTTPRequest (min_size=64, has_variable=0)
    [   0] method               uint32 (size=4, endian=big)
    [   4] headers              bytes (size=60, endian=big)
  ...

Filter Rules (5):
  GET_Requests (struct=HTTPRequest, min_size=64, bytecode_len=5)
  ...
```

### 2. 反汇编过滤器字节码

```bash
./bin/test_disasm config/protocols/dns.pdef
```

输出示例：
```
Filter: DNS_Queries
Structure: DNSHeader
Min packet size: 12
Bytecode (5 instructions):
     0: LOAD_U16_BE     offset=2
     1: CMP_EQ          value=0x0 (0)
     2: JUMP_IF_FALSE   target=4
     3: RETURN_TRUE
     4: RETURN_FALSE
```

### 3. 性能测试

使用现有的integration_example来测试性能：

```bash
./bin/integration_example
```

## 常见问题

### Q: 如何添加自定义协议？

A: 参考现有的协议定义文件，创建新的.pdef文件即可。基本格式：

```pdef
@protocol {
    name = "MyProtocol";
    ports = 12345;
    endian = big;
}

MyPacket {
    uint32  magic;
    uint16  type;
}

@filter MyFilter {
    magic = 0xDEADBEEF;
}
```

### Q: 协议端口号可以修改吗？

A: 可以。PDEF文件中的端口只是建议值，实际使用时可以传入任意端口号到match()函数。

### Q: 支持哪些数据类型？

A:
- 整数: uint8, uint16, uint32, uint64, int8, int16, int32, int64
- 字节数组: bytes[N]
- 字符串: string[N]

### Q: 性能如何？

A: 单报文过滤时间 < 100ns，可以处理 10Mpps+ 流量。

### Q: 如何处理变长字段？

A: 目前使用固定大小的bytes数组。真正的变长字段支持需要扩展解析器。

## 下一步

- 阅读 [完整设计文档](../../docs/pdef/PROTOCOL_FILTER_DESIGN.md)
- 查看 [使用指南](../../docs/pdef/PDEF_USAGE_GUIDE.md)
- 参考 [集成指南](../../docs/pdef/INTEGRATION_GUIDE.md)
