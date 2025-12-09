# PDEF 内联协议过滤 HTTP API 使用指南

## 功能概述

现在支持通过 HTTP API 直接传递内联的 PDEF 协议定义，无需在服务器上预先部署 `.pdef` 文件！

## 三种使用方式

### 方式 1：使用本地文件路径（已支持）

```bash
curl -X POST http://localhost:8080/api/capture/start \
  -H "Content-Type: application/json" \
  -d '{
    "iface": "eth0",
    "filter": "tcp port 80",
    "protocol_filter": "config/protocols/http.pdef"
  }'
```

### 方式 2：使用内联 PDEF 定义（新功能！）✨

```bash
curl -X POST http://localhost:8080/api/capture/start \
  -H "Content-Type: application/json" \
  -d '{
    "iface": "eth0",
    "filter": "tcp port 80",
    "protocol_filter_inline": "@protocol { name = \"HTTP\"; ports = 80; endian = big; } @const { GET_ = 0x47455420; } HTTPRequest { uint32 method; } @filter GET_Requests { method = GET_; }"
  }'
```

### 方式 3：使用内联 PDEF（多行格式，更易读）

由于 JSON 中的换行需要转义，可以使用工具生成请求：

```bash
# 创建 PDEF 内容
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
    uint8   version;
    uint8   type;
}

@filter LoginPackets {
    magic = MAGIC;
    type = TYPE_LOGIN;
}
EOF

# 使用 jq 生成 JSON 请求
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

## 优先级规则

如果同时提供了 `protocol_filter` 和 `protocol_filter_inline`：
-  **优先使用** `protocol_filter_inline`（内联内容）
- ⏭️ 忽略 `protocol_filter`（文件路径）

这样设计是为了让动态生成的过滤规则优先级更高。

## 完整示例

### 示例 1：只抓取 HTTP GET 请求

```bash
curl -X POST http://localhost:8080/api/capture/start \
  -H "Content-Type: application/json" \
  -d '{
    "iface": "eth0",
    "filter": "tcp port 80",
    "duration": 60,
    "protocol_filter_inline": "@protocol { name = \"HTTP\"; ports = 80; endian = big; } @const { GET_ = 0x47455420; POST_ = 0x504F5354; } HTTPRequest { uint32 method; } @filter GET_Requests { method = GET_; }"
  }'
```

**响应**：
```json
{
  "success": true,
  "capture_id": 12345,
  "message": "Capture started"
}
```

**日志输出**：
```
[PDEF] Loaded inline protocol filter: HTTP (1 rules)
```

### 示例 2：只抓取 DNS 查询

```bash
curl -X POST http://localhost:8080/api/capture/start \
  -H "Content-Type: application/json" \
  -d '{
    "iface": "eth0",
    "filter": "udp port 53",
    "duration": 120,
    "protocol_filter_inline": "@protocol { name = \"DNS\"; ports = 53; endian = big; } DNSHeader { uint16 transaction_id; uint16 flags; uint16 questions; uint16 answer_rrs; uint16 authority_rrs; uint16 additional_rrs; } @filter DNS_Queries { flags & 0x8000 = 0x0000; }"
  }'
```

### 示例 3：动态生成过滤规则

**Python 示例**：

```python
import requests
import json

# 动态构建 PDEF 内容
pdef_content = """
@protocol {
    name = "MyApp";
    ports = 9000;
    endian = big;
}

@const {
    MAGIC = 0x12345678;
    CMD_LOGIN = 1;
    CMD_LOGOUT = 2;
}

MyPacket {
    uint32  magic;
    uint16  cmd;
    uint16  seq;
}

@filter LoginLogout {
    magic = MAGIC;
    cmd >= CMD_LOGIN;
    cmd <= CMD_LOGOUT;
}
"""

# 发送请求
response = requests.post(
    'http://localhost:8080/api/capture/start',
    json={
        'iface': 'eth0',
        'filter': 'tcp port 9000',
        'duration': 300,
        'protocol_filter_inline': pdef_content.strip()
    }
)

print(f"Status: {response.status_code}")
print(f"Response: {response.json()}")
```

**Node.js 示例**：

```javascript
const axios = require('axios');

const pdefContent = `
@protocol {
    name = "WebSocket";
    ports = 8080;
    endian = big;
}

@const {
    OP_TEXT = 0x01;
    OP_BINARY = 0x02;
    OP_CLOSE = 0x08;
    OP_PING = 0x09;
    OP_PONG = 0x0A;
}

WSFrame {
    uint8   fin_opcode;
    uint8   mask_len;
}

@filter TextFrames {
    fin_opcode & 0x0F = OP_TEXT;
}
`;

axios.post('http://localhost:8080/api/capture/start', {
  iface: 'eth0',
  filter: 'tcp port 8080',
  duration: 180,
  protocol_filter_inline: pdefContent.trim()
})
.then(response => {
  console.log('Success:', response.data);
})
.catch(error => {
  console.error('Error:', error.response.data);
});
```

## 使用场景

### 场景 1：临时调试

```bash
# 快速创建临时过滤规则，无需部署文件
curl -X POST http://localhost:8080/api/capture/start \
  -H "Content-Type: application/json" \
  -d '{
    "iface": "eth0",
    "protocol_filter_inline": "@protocol { name = \"Debug\"; ports = 12345; endian = big; } Packet { uint32 magic; } @filter All { magic = 0xABCDEF01; }"
  }'
```

### 场景 2：多租户环境

每个租户可以定义自己的协议过滤规则，无需修改服务器配置：

```python
def start_capture_for_tenant(tenant_id, custom_protocol_def):
    """为不同租户启动定制化抓包"""
    response = requests.post(
        'http://capture-server/api/capture/start',
        json={
            'iface': f'tenant-{tenant_id}',
            'protocol_filter_inline': custom_protocol_def,
            'category': f'tenant-{tenant_id}'
        }
    )
    return response.json()
```

### 场景 3：A/B 测试不同过滤规则

```bash
# 规则 A：只抓取登录包
RULE_A='@protocol { name = "Test"; ports = 8000; endian = big; } Packet { uint32 type; } @filter Login { type = 1; }'

# 规则 B：抓取登录和注册包
RULE_B='@protocol { name = "Test"; ports = 8000; endian = big; } Packet { uint32 type; } @filter LoginReg { type >= 1; type <= 2; }'

# 启动两个抓包任务进行对比
curl -X POST http://localhost:8080/api/capture/start -H "Content-Type: application/json" \
  -d "{\"iface\":\"eth0\",\"protocol_filter_inline\":\"$RULE_A\",\"file_pattern\":\"test-a.pcap\"}"

curl -X POST http://localhost:8080/api/capture/start -H "Content-Type: application/json" \
  -d "{\"iface\":\"eth0\",\"protocol_filter_inline\":\"$RULE_B\",\"file_pattern\":\"test-b.pcap\"}"
```

### 场景 4：前端可视化协议编辑器

```javascript
// 前端可视化编辑器生成 PDEF
function generatePDEF(config) {
  return `
@protocol {
    name = "${config.name}";
    ports = ${config.ports.join(', ')};
    endian = ${config.endian};
}

@const {
    ${config.constants.map(c => `${c.name} = ${c.value};`).join('\n    ')}
}

${config.structName} {
    ${config.fields.map(f => `${f.type} ${f.name};`).join('\n    ')}
}

@filter ${config.filterName} {
    ${config.conditions.map(c => `${c.field} ${c.op} ${c.value};`).join('\n    ')}
}
`;
}

// 用户在界面上配置
const userConfig = {
  name: "MyProtocol",
  ports: [9000, 9001],
  endian: "big",
  constants: [
    { name: "MAGIC", value: "0x12345678" }
  ],
  structName: "MyPacket",
  fields: [
    { type: "uint32", name: "magic" },
    { type: "uint16", name: "cmd" }
  ],
  filterName: "ValidPackets",
  conditions: [
    { field: "magic", op: "=", value: "MAGIC" }
  ]
};

// 生成并发送
const pdef = generatePDEF(userConfig);
fetch('/api/capture/start', {
  method: 'POST',
  headers: { 'Content-Type': 'application/json' },
  body: JSON.stringify({
    iface: 'eth0',
    protocol_filter_inline: pdef
  })
});
```

## 错误处理

### 语法错误

**请求**：
```json
{
  "protocol_filter_inline": "@protocol { name = HTTP; ports = 80; }"
}
```

**日志输出**：
```
[PDEF] Failed to parse inline protocol filter: Parse error at line 1: expected string
```

**响应**：
抓包继续进行，但不应用协议过滤（保存所有数据包）。

### 验证内联 PDEF

在发送请求前，可以使用本地工具验证：

```bash
# 将内联内容保存到临时文件
echo '@protocol { name = "Test"; ports = 80; endian = big; }' > /tmp/test.pdef

# 验证语法
./bin/debug_parse /tmp/test.pdef

# 如果成功，则可以放心使用
```

## API 完整参数列表

```json
{
  "iface": "eth0",                    // 网卡名称
  "filter": "tcp port 80",            // BPF 过滤器
  "protocol_filter": "file.pdef",     // PDEF 文件路径（可选）
  "protocol_filter_inline": "...",    // 内联 PDEF 内容（可选，优先级高）
  "duration": 60,                     // 抓包时长（秒）
  "max_bytes": 1073741824,            // 最大文件大小（字节）
  "category": "debug",                // 分类标签
  "file_pattern": "{date}-custom.pcap" // 文件名模式
}
```

## 性能考虑

1. **解析开销**：
   - 文件方式：需要读取文件（约 1-2ms）
   - 内联方式：直接解析字符串（约 1-2ms）
   - **性能相当**，无明显差异

2. **内存占用**：
   - 内联 PDEF 存储在内存中
   - 典型的 PDEF 定义约 500-2000 字节
   - 对于大规模部署（1000+ 并发抓包任务），推荐使用文件方式

3. **缓存策略**：
   - 文件方式可以被操作系统文件缓存优化
   - 内联方式每次都需要重新解析
   - 如果多次使用相同规则，文件方式更优

## 最佳实践

1. **开发/测试环境**：使用内联方式，快速迭代
2. **生产环境**：将稳定的规则保存为文件，使用文件方式
3. **动态场景**：使用内联方式，根据运行时参数生成规则
4. **安全考虑**：验证用户提交的 PDEF 内容，防止恶意输入

## 总结

**内联 PDEF 的优势**：
-  无需文件管理
-  支持动态生成
-  便于集成到自动化工具
-  适合多租户场景

**文件方式的优势**：
-  可重用
-  版本控制友好
-  可被缓存优化
-  便于团队协作

**选择建议**：
- 固定规则 → 文件方式
- 动态规则 → 内联方式
- 临时测试 → 内联方式
- 生产部署 → 文件方式

现在你可以通过 HTTP API 灵活地传递协议过滤规则，无论是预定义文件还是动态生成的内容！🎉
