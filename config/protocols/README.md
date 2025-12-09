# 协议定义文件 (PDEF Files)

本目录包含常用网络协议的PDEF定义文件，可用于rxtracenetcap进行应用层协议过滤。

## 可用协议

### 1. HTTP (`http.pdef`)
- **端口**: 80, 8080, 8000, 8888
- **用途**: 过滤HTTP请求和响应
- **过滤器**:
  - `GET_Requests` - 捕获GET请求
  - `POST_Requests` - 捕获POST请求
  - `HTTP_Responses` - 捕获HTTP响应
  - `NotFound_Errors` - 捕获404错误
  - `Server_Errors` - 捕获5xx服务器错误

**示例**:
```bash
./bin/debug_parse config/protocols/http.pdef
```

### 2. MySQL (`mysql.pdef`)
- **端口**: 3306
- **用途**: 过滤MySQL客户端/服务器通信
- **过滤器**:
  - `Query_Packets` - 捕获SQL查询
  - `Ping_Packets` - 捕获Ping命令
  - `Quit_Packets` - 捕获连接断开
  - `InitDB_Packets` - 捕获数据库切换
  - `Server_Greeting` - 捕获服务器握手

**示例**:
```bash
./bin/debug_parse config/protocols/mysql.pdef
```

### 3. Redis (`redis.pdef`)
- **端口**: 6379
- **用途**: 过滤Redis RESP协议
- **过滤器**:
  - `Array_Commands` - 捕获数组格式的命令
  - `String_Responses` - 捕获简单字符串响应
  - `Error_Responses` - 捕获错误响应
  - `Integer_Responses` - 捕获整数响应
  - `Bulk_Strings` - 捕获批量字符串

**示例**:
```bash
./bin/debug_parse config/protocols/redis.pdef
```

### 4. DNS (`dns.pdef`)
- **端口**: 53
- **用途**: 过滤DNS查询和响应
- **过滤器**:
  - `DNS_Queries` - 捕获DNS查询
  - `DNS_Responses` - 捕获DNS响应
  - `Single_Question` - 捕获单问题查询
  - `Responses_With_Answers` - 捕获有答案的响应

**示例**:
```bash
./bin/debug_parse config/protocols/dns.pdef
```

### 5. IEC 104 General Interrogation (`iec104.pdef`)
- **端口**: 2404
- **用途**: 过滤 IEC 60870-5-104 总召（General Interrogation）报文
- **过滤器**:
  - `GI_Activation` - 总召激活（COT=6, QOI=20）
  - `GI_ActivationConfirm` - 激活确认（COT=7）
  - `GI_Termination` - 终止（COT=10）
  - `GI_Any` - 任意总召（Type=100, QOI=20）

**示例**:
```bash
./bin/debug_parse config/protocols/iec104.pdef
```

### 6. Memcached (`memcached.pdef`)
- **端口**: 11211
- **用途**: 过滤Memcached二进制协议
- **过滤器**:
  - `All_Requests` - 捕获所有请求
  - `All_Responses` - 捕获所有响应
  - `GET_Commands` - 捕获GET命令
  - `SET_Commands` - 捕获SET命令
  - `DELETE_Commands` - 捕获DELETE命令
  - `Success_Responses` - 捕获成功响应
  - `NotFound_Responses` - 捕获未找到响应

**示例**:
```bash
./bin/debug_parse config/protocols/memcached.pdef
```

## 使用方法

### 1. 验证协议定义

使用debug_parse工具检查协议定义是否正确：

```bash
./bin/debug_parse config/protocols/<protocol>.pdef
```

### 2. 查看字节码

使用test_disasm工具查看过滤器生成的字节码：

```bash
./bin/test_disasm config/protocols/<protocol>.pdef
```

### 3. 在代码中使用

**C API**:
```c
#include "pdef/parser.h"
#include "runtime/protocol.h"

// 加载协议定义
char error[512];
ProtocolDef* proto = pdef_parse_file("config/protocols/http.pdef", error, sizeof(error));

// 过滤报文
bool matched = packet_filter_match(packet_data, packet_len, port, proto);

// 清理
protocol_free(proto);
```

**C++ API**:
```cpp
#include "pdef/pdef_wrapper.h"

// 加载协议定义
pdef::ProtocolFilter filter;
if (!filter.load("config/protocols/http.pdef")) {
    std::cerr << "Error: " << filter.getError() << std::endl;
    return 1;
}

// 过滤报文
bool matched = filter.match(packet_data, packet_len, port);
```

### 4. 集成到抓包系统

在rxcapturethread.cpp中使用：

```cpp
// 初始化时加载协议定义
pdef::ProtocolFilter http_filter;
http_filter.load("config/protocols/http.pdef");

// 在抓包回调中过滤
void packet_handler(u_char* user, const struct pcap_pkthdr* header, const u_char* packet) {
    // ... 提取应用层数据 ...

    if (http_filter.match(app_data, app_len, dst_port)) {
        // 匹配HTTP协议，保存报文
        save_packet(...);
    }
}
```

## 自定义协议

你可以参考现有的协议定义文件，创建自己的PDEF文件：

```pdef
@protocol {
    name = "MyProtocol";
    ports = 12345;
    endian = big;
}

@const {
    MAGIC = 0xDEADBEEF;
}

MyPacket {
    uint32  magic;
    uint16  type;
    uint32  length;
}

@filter Important_Packets {
    magic = MAGIC;
    type >= 100;
}
```

## 注意事项

1. **文本协议限制**: HTTP等文本协议的定义相对简化，主要用于识别协议类型，不支持完整的文本解析

2. **变长字段**: 目前PDEF不支持真正的变长字段，使用固定大小的bytes数组作为占位符

3. **位字段**: 某些协议（如MySQL）使用了位字段语法（如`uint32 field : 24`），这需要解析器支持

4. **端口匹配**: 协议定义中的端口用于快速过滤，实际使用时可以覆盖

5. **性能**: 所有过滤器都编译为高效的字节码，单报文执行时间 < 100ns

## 测试

运行PDEF测试套件：

```bash
make pdef test tools
./bin/test_pdef
```

## 扩展阅读

- [PDEF完整设计文档](../../docs/pdef/PROTOCOL_FILTER_DESIGN.md)
- [PDEF使用指南](../../docs/pdef/PDEF_USAGE_GUIDE.md)
- [集成指南](../../docs/pdef/INTEGRATION_GUIDE.md)
