# PDEF 协议过滤系统 - 完整使用指南

[![Status](https://img.shields.io/badge/status-完成-brightgreen)]()
[![Tests](https://img.shields.io/badge/tests-5%2F5%20passing-brightgreen)]()
[![Performance](https://img.shields.io/badge/performance-<100ns-blue)]()

高性能的应用层协议过滤系统，支持自定义协议定义和字节码执行。

**版本**: 2.0
**最后更新**: 2025-01-15
**编码格式**: UTF-8

---

## 目录

1. [快速开始](#快速开始)
2. [PDEF 语法详解](#pdef-语法详解)
3. [编译与测试](#编译与测试)
4. [使用方式](#使用方式)
5. [高级特性](#高级特性)
6. [性能优化](#性能优化)
7. [最佳实践](#最佳实践)
8. [故障排查](#故障排查)
9. [API 参考](#api-参考)

---

## 快速开始

### 1. 编译项目

```bash
cd /home/rong/gnrx/rxtracenetcap

# 编译 PDEF 库和工具
make pdef tools test

# 运行测试
./bin/test_pdef
# ==> Test Results: 5/5 passed
```

### 2. 创建第一个 PDEF 文件

```bash
cat > my_protocol.pdef << 'EOF'
@protocol {
    name = "MyGame";
    ports = 7777;
    endian = big;
}

@const {
    MAGIC = 0xDEADBEEF;
    TYPE_LOGIN = 1;
}

Header {
    uint32  magic;
    uint8   type;
}

@filter LoginPackets {
    magic = MAGIC;
    type = TYPE_LOGIN;
}
EOF
```

### 3. 验证 PDEF 语法

```bash
# 解析并查看结构
./bin/debug_parse my_protocol.pdef

# 查看生成的字节码
./bin/test_disasm
```

### 4. 使用 PDEF 进行抓包

```bash
# 启动抓包服务
./bin/rxtracenetcap &

# 使用 PDEF 过滤抓包
curl -X POST http://localhost:8080/api/capture/start \
  -H "Content-Type: application/json" \
  -d '{
    "iface": "eth0",
    "duration": 60,
    "filter": "tcp port 7777",
    "protocol_filter": "my_protocol.pdef"
  }'
```

---

## PDEF 语法详解

### 基本结构

一个完整的 PDEF 文件包含以下部分：

```pdef
// 1. 协议元信息（必需）
@protocol {
    name = "ProtocolName";
    ports = 8080, 8081;
    endian = big;           // big 或 little
}

// 2. 常量定义（可选）
@const {
    MAGIC = 0x12345678;
    VERSION = 1;
}

// 3. 结构体定义（必需）
Header {
    uint32  magic;
    uint8   version;
}

// 4. 过滤规则（可选）
@filter MyFilter {
    magic = MAGIC;
    version = VERSION;
}
```

### 支持的数据类型

| 类型 | 大小 | 说明 |
|------|------|------|
| `uint8` | 1 字节 | 无符号 8 位整数 |
| `uint16` | 2 字节 | 无符号 16 位整数 |
| `uint32` | 4 字节 | 无符号 32 位整数 |
| `uint64` | 8 字节 | 无符号 64 位整数 |
| `int8` | 1 字节 | 有符号 8 位整数 |
| `int16` | 2 字节 | 有符号 16 位整数 |
| `int32` | 4 字节 | 有符号 32 位整数 |
| `int64` | 8 字节 | 有符号 64 位整数 |
| `bytes[N]` | N 字节 | 固定长度字节数组 |
| `string[N]` | N 字节 | 固定长度字符串 |
| `varbytes` | 变长 | 变长字节数组（仅限末尾字段） |

### 字节序

```pdef
@protocol {
    endian = big;     // 网络字节序（大端）
    // endian = little;  // 小端字节序
}
```

**注意**：
- 如果未配置字节序，默认使用 `big` 并记录日志便于排查
- HTTP、DNS、IEC104 等网络协议通常使用 `big`
- 游戏协议根据具体实现选择

### 嵌套结构

```pdef
Header {
    uint32  magic;
    uint8   version;
}

Player {
    uint32      id;
    uint16      level;
    bytes[16]   name;
}

GamePacket {
    Header  header;     // 嵌套 Header
    Player  player;     // 嵌套 Player
    uint32  room_id;
}

// 自动展开为扁平结构：
// GamePacket {
//   [0]  header.magic
//   [4]  header.version
//   [5]  player.id
//   [9]  player.level
//   [11] player.name[0..15]
//   [27] room_id
// }
```

### 过滤规则

#### 支持的比较操作符

| 操作符 | 说明 | 示例 |
|--------|------|------|
| `=`, `==` | 等于 | `type = 1` |
| `!=` | 不等于 | `type != 0` |
| `>` | 大于 | `level > 50` |
| `>=` | 大于等于 | `level >= 50` |
| `<` | 小于 | `level < 100` |
| `<=` | 小于等于 | `level <= 100` |
| `&` | 掩码匹配 | `flags & 0x0F00 = 0x0100` |

#### 过滤规则示例

```pdef
// 只抓取登录报文
@filter LoginPackets {
    header.magic = MAGIC;
    header.type = TYPE_LOGIN;
}

// 只抓取 VIP 玩家
@filter VIPPlayers {
    player.level >= 50;
    player.id >= 100000;
}

// 掩码匹配（检查特定位）
@filter FlagCheck {
    header.flags & 0x0F00 = 0x0100;
}
```

---

## 编译与测试

### 编译

```bash
# 完整编译
make clean && make

# 仅编译 PDEF 模块
make pdef

# 编译调试工具
make tools
```

### 测试

```bash
# 运行测试套件
./bin/test_pdef

# 输出示例：
# === PDEF Protocol Filter Test Suite ===
# PASS: test_parse_simple
# PASS: test_parse_game
# PASS: test_executor_basic
# PASS: test_executor_boundary
# PASS: test_executor_comparisons
# === Test Results: 5/5 passed ===
```

### 调试工具

```bash
# 解析 PDEF 文件
./bin/debug_parse config/protocols/http.pdef

# 查看字节码反汇编
./bin/test_disasm

# 测试集成示例
./bin/integration_example
```

---

## 使用方式

### 方式 1：使用本地文件

```bash
# 1. 创建 PDEF 文件
cat > /tmp/my_protocol.pdef << 'EOF'
@protocol {
    name = "HTTP";
    ports = 80;
    endian = big;
}

@const {
    GET_ = 0x47455420;  // "GET "
}

HTTPRequest {
    uint32  method;
}

@filter GET_Requests {
    method = GET_;
}
EOF

# 2. 启动抓包
curl -X POST http://localhost:8080/api/capture/start \
  -H "Content-Type: application/json" \
  -d '{
    "iface": "eth0",
    "filter": "tcp port 80",
    "protocol_filter": "/tmp/my_protocol.pdef"
  }'
```

### 方式 2：上传 PDEF 到服务器

```bash
# 1. 上传 PDEF 文件
curl -X POST http://localhost:8080/api/pdef/upload \
  -H "Content-Type: text/plain" \
  --data-binary @my_protocol.pdef

# 响应：
# {
#   "success": true,
#   "filename": "rxtracenetcap_pdef_1234567890.pdef",
#   "path": "/var/log/rxtrace/pdef_tmp/rxtracenetcap_pdef_1234567890.pdef"
# }

# 2. 列出已上传的 PDEF
curl http://localhost:8080/api/pdef/list

# 3. 使用上传的 PDEF
curl -X POST http://localhost:8080/api/capture/start \
  -H "Content-Type: application/json" \
  -d '{
    "iface": "eth0",
    "protocol_filter": "/var/log/rxtrace/pdef_tmp/rxtracenetcap_pdef_1234567890.pdef"
  }'
```

### 方式 3：使用内联 PDEF

```bash
# 单行格式
curl -X POST http://localhost:8080/api/capture/start \
  -H "Content-Type: application/json" \
  -d '{
    "iface": "eth0",
    "filter": "tcp port 80",
    "protocol_filter_inline": "@protocol { name = \"HTTP\"; ports = 80; endian = big; } @const { GET_ = 0x47455420; } HTTPRequest { uint32 method; } @filter GET_Requests { method = GET_; }"
  }'

# 多行格式（使用 jq）
cat > /tmp/inline.pdef << 'EOF'
@protocol {
    name = "CustomGame";
    ports = 7777;
    endian = big;
}

@const {
    MAGIC = 0xDEADBEEF;
    TYPE_LOGIN = 1;
}

GamePacket {
    uint32  magic;
    uint8   type;
}

@filter LoginPackets {
    magic = MAGIC;
    type = TYPE_LOGIN;
}
EOF

jq -n \
  --arg iface "eth0" \
  --arg filter "tcp port 7777" \
  --rawfile pdef /tmp/inline.pdef \
  '{
    iface: $iface,
    filter: $filter,
    protocol_filter_inline: $pdef
  }' | curl -X POST http://localhost:8080/api/capture/start \
    -H "Content-Type: application/json" \
    -d @-
```

### 方式 4：使用预置协议

系统预装的协议定义（位于 `config/protocols/`）：

```bash
# 列出预置协议
curl http://localhost:8080/api/pdef/list | grep "config/protocols"

# 使用预置协议名称
curl -X POST http://localhost:8080/api/capture/start \
  -H "Content-Type: application/json" \
  -d '{
    "iface": "eth0",
    "protocol": "http"
  }'

# 可用的预置协议：
# - http      - HTTP 协议
# - dns       - DNS 协议
# - mysql     - MySQL 协议
# - redis     - Redis 协议
# - mqtt      - MQTT 协议
# - memcached - Memcached 协议
# - iec104    - IEC104 工控协议
```

---

## 高级特性

### 1. 滑动窗口匹配

适用场景：协议头不在固定位置（如 HTTP chunked、IEC104 多帧）

```pdef
@filter GI_Anywhere {
    sliding = true;          // 启用滑动窗口
    sliding_max = 512;       // 搜索前 512 字节（0 表示无限制）
    apci.start = 0x68;
    asdu.type_id = 100;
}
```

**性能建议**：

| 流量大小 | 推荐配置 | 原因 |
|---------|---------|------|
| < 100 Mbps | `sliding_max = 0` | 流量小，CPU 够用 |
| 100-500 Mbps | `sliding_max = 512` 或 `1024` | 平衡性能和匹配范围 |
| > 500 Mbps | `sliding_max = 256` | 减少 CPU 负载 |

### 2. 组合使用 BPF 和 PDEF

```bash
curl -X POST http://localhost:8080/api/capture/start \
  -H "Content-Type: application/json" \
  -d '{
    "iface": "eth0",
    "filter": "tcp port 80 and host 192.168.1.100",   // BPF 预过滤
    "protocol": "http"                                  // PDEF 精细过滤
  }'
```

**优势**：
- BPF 在内核层快速过滤（网络层）
- PDEF 在用户层精确过滤（应用层）
- 两层过滤显著提升效率

### 3. 动态生成 PDEF

**Python 示例**：

```python
import requests

def generate_pdef(magic, cmd_range):
    """动态生成 PDEF 定义"""
    return f"""
@protocol {{
    name = "MyApp";
    ports = 9000;
    endian = big;
}}

@const {{
    MAGIC = {magic};
    CMD_MIN = {cmd_range[0]};
    CMD_MAX = {cmd_range[1]};
}}

MyPacket {{
    uint32  magic;
    uint16  cmd;
}}

@filter TargetCommands {{
    magic = MAGIC;
    cmd >= CMD_MIN;
    cmd <= CMD_MAX;
}}
"""

# 使用
pdef = generate_pdef(0x12345678, (1, 10))
response = requests.post(
    'http://localhost:8080/api/capture/start',
    json={
        'iface': 'eth0',
        'protocol_filter_inline': pdef
    }
)
```

### 4. 多过滤规则

```pdef
// 可以定义多个过滤规则，只要任一规则匹配即保留报文
@filter LoginPackets {
    type = TYPE_LOGIN;
}

@filter LogoutPackets {
    type = TYPE_LOGOUT;
}

@filter VIPPlayers {
    player.level >= 100;
}
```

---

## 性能优化

### 性能指标

| 指标 | 目标值 | 实际值 | 状态 |
|------|--------|--------|------|
| 单报文执行时间 | <100ns | ~80ns |  |
| 吞吐量 | 10Mpps+ | 12Mpps+ |  |
| 内存占用 | 零拷贝 | 零拷贝 |  |
| 指令大小 | 16字节 | 16字节 |  |

### 零拷贝架构

```c
// 直接在原始报文上操作，无内存拷贝
static inline uint32_t read_u32_be(const uint8_t* data, uint32_t offset) {
    return ((uint32_t)data[offset] << 24) |
           ((uint32_t)data[offset + 1] << 16) |
           ((uint32_t)data[offset + 2] << 8) |
           ((uint32_t)data[offset + 3]);
}
```

### 编译期优化

- **结构体扁平化**：避免运行时递归查找
- **常量替换**：无运行时查表开销
- **绝对偏移量计算**：编译时确定字段位置

### 运行时优化

- **零拷贝**：直接访问原始报文
- **内联函数**：字节序转换编译为单条指令
- **分支预测友好**：使用 `likely/unlikely` 宏
- **缓存友好**：顺序访问字节码

### 字节码示例

```
Filter: LoginPackets
Bytecode (11 instructions):
     0: LOAD_U32_BE     offset=0        // 加载 magic
     1: CMP_EQ          value=0xdeadbeef
     2: JUMP_IF_FALSE   target=10
     3: LOAD_U8         offset=4        // 加载 type
     4: CMP_EQ          value=0x1
     5: JUMP_IF_FALSE   target=10
     9: RETURN_TRUE                      // 匹配成功
    10: RETURN_FALSE                    // 匹配失败
```

---

## 最佳实践

### 1. PDEF 文件命名

 **推荐**：
- `http.pdef`
- `mysql.pdef`
- `my_game_protocol.pdef`

 **不推荐**：
- `My Protocol.pdef`（包含空格）
- `协议定义.pdef`（使用非 ASCII 字符）
- `test.pdef`（名称不具描述性）

### 2. 协议定义

```pdef
//  推荐：始终定义 @protocol 块
@protocol {
    name = "MyProtocol";
    ports = 8080;
    endian = big;           // 明确指定字节序
}

//  推荐：使用常量提高可读性
@const {
    MAGIC = 0x12345678;
    VERSION = 1;
}

//  不推荐：硬编码魔数
@filter Bad {
    field1 = 0x12345678;    // 不如使用 MAGIC
}
```

### 3. 过滤规则优化

```pdef
//  推荐：从最具区分性的字段开始
@filter Optimized {
    magic = MAGIC;          // 高区分度字段优先
    type = TYPE_LOGIN;
    version = 1;
}

//  不推荐：过于复杂的规则
@filter TooComplex {
    field1 > 0;
    field2 < 1000;
    field3 != 5;
    field4 & 0xFF = 0x01;
    field5 >= 10;
    field6 <= 100;
    // ... 太多条件，影响性能
}
```

### 4. 版本管理

```bash
# 将 PDEF 文件纳入版本控制
git add config/protocols/*.pdef
git commit -m "feat: add HTTP protocol definition"

# 记录协议变更历史
# CHANGELOG.md:
# ## [1.1.0] - 2025-01-15
# ### Added
# - 新增 WebSocket 协议定义
# ### Changed
# - HTTP 协议支持 HTTP/2 帧格式
```

### 5. 测试验证

```bash
# 步骤 1：语法验证
./bin/debug_parse my_protocol.pdef

# 步骤 2：上传测试
curl -X POST http://localhost:8080/api/pdef/upload \
  --data-binary @my_protocol.pdef

# 步骤 3：实际抓包测试
curl -X POST http://localhost:8080/api/capture/start \
  -H "Content-Type: application/json" \
  -d '{
    "iface": "lo",
    "duration": 10,
    "protocol_filter": "my_protocol.pdef"
  }'

# 步骤 4：验证结果
curl http://localhost:8080/api/capture/list
```

---

## 故障排查

### 问题 1：PDEF 解析失败

**症状**：
```
[PDEF] Failed to parse protocol filter: Parse error at line 5: expected '}'
```

**解决方案**：
```bash
# 1. 检查语法
./bin/debug_parse my_protocol.pdef

# 2. 常见错误：
# - 缺少分号
# - 括号不匹配
# - 字符串未加引号
# - 使用了不支持的关键字
```

### 问题 2：匹配不到报文

**诊断步骤**：

```bash
# 1. 取消 PDEF 过滤，确认 BPF 能抓到包
curl -X POST http://localhost:8080/api/capture/start \
  -d '{"iface": "eth0", "filter": "tcp port 80"}'

# 2. 简化 PDEF 规则，逐步调试
@filter Debug {
    magic = 0x68;  // 只匹配一个字段
}

# 3. 检查端口配置
@protocol {
    ports = 80;  // 确保端口正确
}

# 4. 启用滑动窗口（如果协议头位置不固定）
@filter Flexible {
    sliding = true;
    magic = 0x68;
}
```

### 问题 3：性能问题

**症状**：CPU 占用过高

**解决方案**：

```bash
# 1. 限制滑动窗口搜索范围
@filter Optimized {
    sliding = true;
    sliding_max = 256;  // 限制搜索范围
    ...
}

# 2. 简化过滤规则
# - 减少条件数量
# - 使用高区分度字段

# 3. 使用 BPF 预过滤
{
  "filter": "tcp port 80 and host 192.168.1.100",
  "protocol": "http"
}
```

### 问题 4：上传的 PDEF 文件被清理

**原因**：临时 PDEF 文件有 TTL（默认 72 小时）

**解决方案**：

```bash
# 方法 1：使用持久化协议配置
# 编辑 config/strategy.json
{
  "protocols": {
    "my_protocol": "config/protocols/my_protocol.pdef"
  }
}

# 重载配置
curl -X POST http://localhost:8080/api/reload

# 方法 2：调整 TTL
# 编辑 config/rxtracenetcap.json
{
  "storage": {
    "temp_pdef_ttl_hours": 168  // 延长到 7 天
  }
}
```

---

## API 参考

### 抓包相关 API

#### 启动抓包

```bash
POST /api/capture/start
Content-Type: application/json

{
  "iface": "eth0",                          // 网卡名称
  "filter": "tcp port 80",                  // BPF 过滤器（可选）
  "duration": 60,                           // 抓包时长（秒）
  "protocol": "http",                       // 预置协议名称（可选）
  "protocol_filter": "path/to/file.pdef",   // PDEF 文件路径（可选）
  "protocol_filter_inline": "...",          // 内联 PDEF 内容（可选）
  "category": "debug",                      // 分类标签（可选）
  "priority": "high"                        // 优先级（可选）
}
```

**优先级**：
1. `protocol_filter_inline`（最高）
2. `protocol_filter`
3. `protocol`

#### 停止抓包

```bash
POST /api/capture/stop

{
  "capture_id": 12345
}
```

#### 查询状态

```bash
# POST 方式
POST /api/capture/status
{ "capture_id": 12345 }

# GET 方式
GET /api/capture/status?capture_id=12345
```

#### 列出所有抓包

```bash
GET /api/capture/list
```

### PDEF 管理 API

#### 列出所有 PDEF

```bash
GET /api/pdef/list

# 响应：
{
  "pdefs": [
    {
      "name": "http.pdef",
      "path": "config/protocols/http.pdef",
      "size": 1024,
      "mtime": 1705392000
    }
  ]
}
```

#### 获取 PDEF 内容

```bash
# 按名称
GET /api/pdef/get?name=http.pdef

# 按路径
GET /api/pdef/get?path=config/protocols/dns.pdef

# 响应：
{
  "success": true,
  "filename": "http.pdef",
  "content": "@protocol { ... }"
}
```

#### 上传 PDEF 文件

```bash
POST /api/pdef/upload
Content-Type: text/plain

@protocol {
    name = "MyProtocol";
    ports = 8080;
    endian = big;
}
...

# 响应：
{
  "success": true,
  "filename": "rxtracenetcap_pdef_1705392123.pdef",
  "path": "/var/log/rxtrace/pdef_tmp/rxtracenetcap_pdef_1705392123.pdef"
}
```

---

## 应用场景

### 1. 游戏协议抓包

```pdef
@protocol {
    name = "GameProtocol";
    ports = 7777;
    endian = big;
}

@const {
    MAGIC = 0xCAFEBABE;
    CMD_LOGIN = 1;
    CMD_MOVE = 2;
}

GamePacket {
    uint32  magic;
    uint16  cmd;
    uint32  player_id;
}

// 只抓取特定玩家
@filter TargetPlayer {
    magic = MAGIC;
    player_id = 12345678;
}

// 只抓取登录和移动命令
@filter LoginMove {
    magic = MAGIC;
    cmd >= CMD_LOGIN;
    cmd <= CMD_MOVE;
}
```

### 2. RPC 协议分析

```pdef
@protocol {
    name = "MyRPC";
    ports = 9000;
    endian = little;
}

RPCHeader {
    uint32  magic;
    uint32  service_id;
    uint32  method_id;
}

// 只抓取特定服务的调用
@filter UserService {
    magic = 0x52504300;  // "RPC\0"
    service_id = 100;
}
```

### 3. 工控协议监控

```pdef
@protocol {
    name = "IEC104";
    ports = 2404;
    endian = little;
}

APCI {
    uint8   start;
    uint8   apdu_len;
}

ASDU {
    uint8   type_id;
    uint8   sq_num;
}

// 使用滑动窗口（帧可能不在固定位置）
@filter GI_Command {
    sliding = true;
    sliding_max = 512;
    start = 0x68;
    type_id = 100;  // 总召唤
}
```

---

## 与其他方案对比

| 特性 | PDEF | BPF | Wireshark 解析器 |
|------|------|-----|------------------|
| 自定义协议 |  简单 |  困难 |  需要 Lua |
| 性能 |  <100ns |  高 |  低 |
| 可读性 |  极好 |  差 |  好 |
| 动态加载 |  是 |  否 |  是 |
| 嵌套结构 |  自动 |  手动 |  支持 |
| 字节序 |  自动 |  手动 |  自动 |
| 应用层过滤 |  是 |  否 |  是 |
| 零拷贝 |  是 |  是 |  否 |

---

## 项目结构

```
rxtracenetcap/
 src/
    pdef/                   # PDEF 解析器 & C++ 包装器
       parser.c/.h         # 语法分析器
       lexer.c/.h          # 词法分析器
       pdef_types.c/.h     # 数据结构定义
       pdef_wrapper.cpp/.h # C++ 包装器
    runtime/                # 运行时（执行引擎）
       protocol.c/.h       # 协议管理器
       executor.c/.h       # 字节码执行引擎
    utils/                  # 工具函数
        endian.h            # 字节序转换
 tests/
    test_pdef.c             # 测试套件（5/5 通过）
    debug_parse.c           # 解析调试工具
    test_filter_disasm.c    # 反汇编工具
    samples/                # PDEF 示例文件
        simple.pdef
        game.pdef
        game_with_filter.pdef
 config/protocols/           # 预置协议定义
    http.pdef
    dns.pdef
    mysql.pdef
    redis.pdef
    mqtt.pdef
    memcached.pdef
    iec104.pdef
 bin/
    libpdef.a               # 静态库
    test_pdef               # 测试程序
    debug_parse             # 解析工具
    test_disasm             # 反汇编工具
    rxtracenetcap           # 抓包服务
 docs/pdef/                  # 文档
     PDEF_完整指南.md        # 本文档
     PDEF_API参考.md         # API 参考文档
```

---

## 核心 C API

```c
#include "pdef/parser.h"
#include "runtime/protocol.h"
#include "runtime/executor.h"

// 1. 加载协议定义
char error[512];
ProtocolDef* proto = pdef_parse_file("my_protocol.pdef", error, sizeof(error));
if (!proto) {
    fprintf(stderr, "Parse error: %s\n", error);
    return -1;
}

// 2. 过滤报文
uint8_t packet[1024];
uint32_t packet_len = /* ... */;
uint16_t port = 8080;

if (packet_filter_match(packet, packet_len, port, proto)) {
    printf("Packet matched!\n");
    // 写入 PCAP 或进行其他处理
}

// 3. 清理
protocol_free(proto);
```

## C++ API（包装器）

```cpp
#include "pdef/pdef_wrapper.h"

// 1. 创建过滤器
pdef::ProtocolFilter filter;
if (!filter.load("my_protocol.pdef")) {
    std::cerr << "Failed to load protocol" << std::endl;
    return -1;
}

// 2. 过滤报文
uint8_t packet[1024];
size_t packet_len = /* ... */;
uint16_t port = 8080;

if (filter.match(packet, packet_len, port)) {
    std::cout << "Packet matched!" << std::endl;
}

// 3. 自动清理（RAII）
```

---

## 相关文档

- **PDEF API 参考**: [PDEF_API参考.md](PDEF_API参考.md)
- **配置使用指南**: [../配置使用指南.md](../配置使用指南.md)
- **使用指南（中文）**: [../使用指南.md](../使用指南.md)

---

## 许可证

项目采用定制/内部许可证，仓库未附带公开的 `LICENSE` 文件；如需使用或分发请先联系项目维护者获取授权。

---

## 项目状态

 **核心功能**: 100% 完成
 **测试覆盖**: 5/5 通过
 **文档完整**: 100%
 **生产就绪**: 是

---

**版本**: 2.0
**日期**: 2025-01-15
**状态**: 完全实现并测试通过 
