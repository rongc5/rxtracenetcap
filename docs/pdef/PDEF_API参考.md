# PDEF HTTP API 完整参考

本文档提供 PDEF 相关的所有 HTTP API 的完整参考。

**版本**: 2.0
**最后更新**: 2025-01-15
**编码格式**: UTF-8

---

## 目录

1. [抓包控制 API](#抓包控制-api)
2. [PDEF 管理 API](#pdef-管理-api)
3. [参数组合说明](#参数组合说明)
4. [完整示例](#完整示例)
5. [错误处理](#错误处理)
6. [最佳实践](#最佳实践)

---

## 抓包控制 API

### 启动抓包

```
POST /api/capture/start
Content-Type: application/json
```

**请求体参数**：

| 分类 | 参数名 | 类型 | 说明 | 示例 |
|------|--------|------|------|------|
| **抓包方式** | | | | |
| | `capture_mode` | string | 抓包模式 | `"interface"`, `"process"`, `"pid"`, `"container"` |
| | `iface` | string | 网卡接口名 | `"eth0"`, `"lo"`, `"any"` |
| | `proc_name` | string | 进程名称 | `"nginx"`, `"redis-server"` |
| | `pid` / `target_pid` | int | 进程 ID | `1234` |
| | `container_id` | string | 容器 ID | `"abc123..."` |
| **过滤条件** | | | | |
| | `filter` | string | BPF 过滤表达式 | `"tcp port 80"`, `"host 192.168.1.100"` |
| | `port` / `port_filter` | int | 端口号 | `80`, `9090` |
| | `ip` / `ip_filter` | string | IP 地址 | `"192.168.1.100"` |
| **协议过滤** | | | | |
| | `protocol` | string | 协议名（自动查找 .pdef） | `"http"`, `"dns"`, `"mysql"` |
| | `protocol_filter` | string | PDEF 文件路径 | `"config/protocols/http.pdef"` |
| | `protocol_filter_inline` | string | 内联 PDEF 内容 | `"@protocol { ... }"` |
| **限制条件** | | | | |
| | `duration` / `duration_sec` | int | 抓包时长（秒） | `60`, `300` |
| | `max_bytes` | int64 | 最大文件大小（字节） | `1073741824` (1GB) |
| | `max_packets` | int | 最大数据包数 | `10000` |
| **输出配置** | | | | |
| | `file` | string | 输出文件路径 | `"/tmp/capture.pcap"` |
| | `file_pattern` | string | 文件名模式 | `"{date}-{iface}-{proc}.pcap"` |
| | `category` | string | 分类标签 | `"production"`, `"debug"` |
| **其他** | | | | |
| | `priority` | string | 优先级 | `"high"`, `"normal"` |
| | `user` / `request_user` | string | 请求用户 | `"admin"` |
| | `client_ip` | string | 客户端 IP | `"10.0.0.1"` |

**响应**：

```json
{
  "success": true,
  "capture_id": 12345,
  "sid": "20250115_123456_eth0_nginx_80",
  "key": "nginx_80_20250115",
  "path": "/var/log/rxtrace/captures/diag/20250115/...",
  "message": "Capture started successfully"
}
```

**响应字段说明**：
- `capture_id`: 抓包任务 ID（用于后续查询/停止）
- `sid`: 会话 ID（唯一标识）
- `key`: 缓存键（用于去重）
- `path`: 输出文件路径
- `duplicate`: 是否为重复请求（如果存在）

---

### 停止抓包

```
POST /api/capture/stop
Content-Type: application/json
```

**请求体**：

```json
{
  "capture_id": 12345
}
```

**响应**：

```json
{
  "success": true,
  "message": "Capture stopped"
}
```

---

### 查询抓包状态

#### 方式 1：POST 请求

```
POST /api/capture/status
Content-Type: application/json
```

**请求体**：

```json
{
  "capture_id": 12345
}
```

#### 方式 2：GET 请求

```
GET /api/capture/status?capture_id=12345
```

**响应**：

```json
{
  "capture_id": 12345,
  "status": "running",
  "packets_captured": 5420,
  "bytes_captured": 3145728,
  "elapsed_sec": 25,
  "path": "/var/log/rxtrace/captures/diag/..."
}
```

**状态值**：
- `running`: 正在运行
- `completed`: 已完成
- `stopped`: 已停止
- `failed`: 失败

---

### 列出所有抓包任务

```
GET /api/capture/list
```

**响应**：

```json
{
  "captures": [
    {
      "capture_id": 12345,
      "status": "running",
      "iface": "eth0",
      "proc_name": "nginx",
      "duration": 60,
      "elapsed": 25,
      "packets": 5420
    },
    {
      "capture_id": 12346,
      "status": "completed",
      "iface": "lo",
      "protocol": "redis",
      "duration": 120,
      "elapsed": 120,
      "packets": 8932
    }
  ]
}
```

---

## PDEF 管理 API

### 列出所有 PDEF 文件

```
GET /api/pdef/list
```

**功能**：列出所有可用的 PDEF 文件，包括：
- `config/protocols/` 目录中的预置协议文件
- `/var/log/rxtrace/pdef_tmp/` 目录中用户上传的临时文件

**请求示例**：

```bash
curl http://localhost:8080/api/pdef/list
```

**响应示例**：

```json
{
  "success": true,
  "pdefs": [
    {
      "name": "http.pdef",
      "path": "config/protocols/http.pdef",
      "size": 1234,
      "mtime": "2025-01-15T10:30:00Z",
      "type": "builtin"
    },
    {
      "name": "rxtracenetcap_pdef_1705392123.pdef",
      "path": "/var/log/rxtrace/pdef_tmp/rxtracenetcap_pdef_1705392123.pdef",
      "size": 856,
      "mtime": "2025-01-15T14:20:00Z",
      "type": "uploaded"
    }
  ]
}
```

**响应字段**：
- `name`: 文件名
- `path`: 完整路径
- `size`: 文件大小（字节）
- `mtime`: 最后修改时间（ISO 8601 格式）
- `type`: 类型（`builtin` 或 `uploaded`）

---

### 获取 PDEF 文件内容

```
GET /api/pdef/get
```

**功能**：获取指定 PDEF 文件的文本内容。

**参数**：
- `name`（可选）: 文件名，如 `http.pdef`
- `path`（可选）: 完整路径

**注意**：必须提供 `name` 或 `path` 之一。

**请求示例 1** - 按名称获取：

```bash
curl "http://localhost:8080/api/pdef/get?name=http.pdef"
```

**请求示例 2** - 按路径获取：

```bash
curl "http://localhost:8080/api/pdef/get?path=config/protocols/dns.pdef"
```

**响应示例**：

```json
{
  "success": true,
  "filename": "http.pdef",
  "path": "config/protocols/http.pdef",
  "size": 1234,
  "mtime": "2025-01-15T10:30:00Z",
  "content": "@protocol {\n    name = \"HTTP\";\n    ports = 80, 443;\n    endian = big;\n}\n\n@const {\n    GET_ = 0x47455420;\n}\n\nHTTPRequest {\n    uint32 method;\n}\n\n@filter GET_Requests {\n    method = GET_;\n}\n"
}
```

**响应字段**：
- `success`: 是否成功
- `filename`: 文件名
- `path`: 完整路径
- `size`: 文件大小（字节）
- `mtime`: 最后修改时间（ISO 8601 格式）
- `content`: PDEF 文件的文本内容

---

### 上传 PDEF 文件

```
POST /api/pdef/upload
Content-Type: text/plain
```

**功能**：将 PDEF 文件内容上传到服务器，服务器会：
1. 解析并验证 PDEF 语法
2. 保存到临时目录（`/var/log/rxtrace/pdef_tmp/`）
3. 返回文件路径供后续使用

**请求体**：PDEF 文件的原始文本内容（不需要 JSON 包装）

**限制**：
- 最大文件大小：2MB
- 上传后立即进行语法/语义校验

**请求示例**：

```bash
curl -X POST http://localhost:8080/api/pdef/upload \
  -H "Content-Type: text/plain" \
  --data-binary @my_protocol.pdef
```

**或者直接传递内容**：

```bash
curl -X POST http://localhost:8080/api/pdef/upload \
  -H "Content-Type: text/plain" \
  --data-binary '@protocol {
    name = "Test";
    ports = 9090;
    endian = big;
}

@const {
    MAGIC = 0x12345678;
}

TestPacket {
    uint32 magic;
}

@filter Valid {
    magic = MAGIC;
}'
```

**响应示例**：

```json
{
  "success": true,
  "filename": "rxtracenetcap_pdef_1705392123.pdef",
  "path": "/var/log/rxtrace/pdef_tmp/rxtracenetcap_pdef_1705392123.pdef",
  "size": 256,
  "checksum": "3af01b2c1234abcd",
  "validated": true
}
```

**响应字段**：
- `success`: 是否成功
- `filename`: 生成的文件名（包含时间戳）
- `path`: 服务器上的完整路径（可用于 `protocol_filter`）
- `size`: 文件大小（字节）
- `checksum`: FNV1a-64 校验和（用于验证）
- `validated`: 是否通过语法/语义校验

**注意事项**：
- 上传的文件存储在临时目录，重启后可能丢失
- 文件名格式：`rxtracenetcap_pdef_<timestamp>_<pid>_<counter>.pdef`
- 临时文件默认保留 72 小时（可在配置中调整）

---

## 参数组合说明

### 协议过滤优先级

当同时提供多个协议过滤参数时的优先级顺序：

1. **`protocol_filter_inline`**（最高优先级）- 内联 PDEF 内容
2. **`protocol_filter`** - PDEF 文件路径
3. **`protocol`** - 协议名（自动查找 .pdef 文件）
4. **无协议过滤**（默认）

```json
// 示例：同时提供多个参数时
{
  "protocol": "http",                           // 优先级 3
  "protocol_filter": "config/protocols/http.pdef",  // 优先级 2
  "protocol_filter_inline": "@protocol { ... }"    // 优先级 1（实际使用这个）
}
```

### BPF 过滤器构建优先级

1. **手动指定 `filter`**（最高优先级）- 直接使用
2. **自动生成（进程模式）** - 根据进程监听端口自动生成
3. **`port_filter`** - 转换为 `tcp port X or udp port X`
4. **默认** - 无过滤（抓取所有流量）

### 支持的参数组合

#### 组合 1：网卡 + BPF + PDEF 文件

```json
{
  "iface": "eth0",
  "filter": "tcp port 80",
  "protocol_filter": "config/protocols/http.pdef",
  "duration": 60
}
```

**适用场景**：精确控制抓包范围和协议过滤

---

#### 组合 2：进程名 + PDEF 文件

```json
{
  "proc_name": "nginx",
  "protocol_filter": "config/protocols/http.pdef",
  "duration": 60
}
```

**工作流程**：
1. 自动查找 nginx 进程监听的端口
2. 自动构建 BPF 过滤器（如 `tcp port 80 or tcp port 443`）
3. 应用 PDEF 协议过滤

**适用场景**：进程抓包，自动化程度高

---

#### 组合 3：网卡 + 端口 + PDEF 文件

```json
{
  "iface": "eth0",
  "port_filter": 9090,
  "protocol_filter": "cli_samples/rxbinaryproto.pdef",
  "duration": 60
}
```

**适用场景**：简化配置，单端口抓包

---

#### 组合 4：网卡 + BPF + 内联 PDEF

```json
{
  "iface": "eth0",
  "filter": "tcp port 9090",
  "protocol_filter_inline": "@protocol { name = \"Test\"; ports = 9090; endian = big; } @const { MAGIC = 0x12345678; } Packet { uint32 magic; } @filter Valid { magic = MAGIC; }",
  "duration": 60
}
```

**适用场景**：
- 临时测试
- 动态生成 PDEF 规则
- 无需创建文件

---

#### 组合 5：进程名 + 无端口 PDEF

```json
{
  "proc_name": "game_server",
  "protocol_filter": "cli_samples/test_no_ports.pdef",
  "duration": 60
}
```

**PDEF 文件示例**（无 `ports` 字段）：

```pdef
@protocol {
    name = "GameProtocol";
    endian = big;
    // 注意：没有 ports 字段
}

@const {
    MAGIC = 0x12345678;
}

GamePacket {
    uint32 magic;
}

@filter ValidPackets {
    magic = MAGIC;
}
```

**适用场景**：动态端口应用，协议可能出现在任意端口

---

#### 组合 6：使用预置协议

```json
{
  "iface": "eth0",
  "protocol": "http",
  "duration": 60
}
```

**系统自动**：
- 查找 `config/strategy.json` 中的协议映射
- 加载对应的 PDEF 文件（如 `config/protocols/http.pdef`）
- 应用协议过滤规则

**预置协议列表**：
- `http` - HTTP 协议
- `dns` - DNS 协议
- `mysql` - MySQL 协议
- `redis` - Redis 协议
- `mqtt` - MQTT 协议
- `memcached` - Memcached 协议
- `iec104` - IEC104 工控协议

---

###  重要：只传 PDEF 的性能问题

**问题场景**：

```json
{
  "protocol_filter": "config/protocols/http.pdef"
}
```

**实际行为**：
- `iface`: 默认 `"any"`（所有网卡）
- `filter`: 空字符串（**没有 BPF 过滤**）
- 结果：BPF 层抓取所有流量，PDEF 层才过滤

**性能问题**：
- BPF 抓取大量无用数据（SSH、DNS、MySQL 等所有流量）
- PDEF 在应用层才过滤，浪费 CPU 和内存
- 磁盘 I/O 压力大

**推荐做法**：

```json
//  不推荐：只传 PDEF
{
  "protocol_filter": "config/protocols/http.pdef"
}

//  推荐：添加 BPF 过滤
{
  "filter": "tcp port 80 or tcp port 443",
  "protocol_filter": "config/protocols/http.pdef"
}

//  推荐：使用 port_filter
{
  "port_filter": 80,
  "protocol_filter": "config/protocols/http.pdef"
}

//  推荐：使用进程名（自动生成 BPF）
{
  "proc_name": "nginx",
  "protocol_filter": "config/protocols/http.pdef"
}
```

---

## 完整示例

### 示例 1：上传 PDEF 并启动抓包

```bash
#!/bin/bash

# 步骤 1：创建 PDEF 文件
cat > /tmp/my_protocol.pdef << 'EOF'
@protocol {
    name = "MyProtocol";
    ports = 9090;
    endian = big;
}

@const {
    MAGIC = 0xDEADBEEF;
    CMD_LOGIN = 1;
}

Packet {
    uint32  magic;
    uint16  cmd;
}

@filter LoginPackets {
    magic = MAGIC;
    cmd = CMD_LOGIN;
}
EOF

# 步骤 2：上传 PDEF
RESPONSE=$(curl -s -X POST http://localhost:8080/api/pdef/upload \
  -H "Content-Type: text/plain" \
  --data-binary @/tmp/my_protocol.pdef)

echo "Upload response: $RESPONSE"

# 提取路径
PDEF_PATH=$(echo "$RESPONSE" | jq -r '.path')
echo "Uploaded to: $PDEF_PATH"

# 步骤 3：启动抓包
CAPTURE_RESPONSE=$(curl -s -X POST http://localhost:8080/api/capture/start \
  -H "Content-Type: application/json" \
  -d "{
    \"iface\": \"eth0\",
    \"filter\": \"tcp port 9090\",
    \"protocol_filter\": \"$PDEF_PATH\",
    \"duration\": 60
  }")

echo "Capture response: $CAPTURE_RESPONSE"

# 提取 capture_id
CAPTURE_ID=$(echo "$CAPTURE_RESPONSE" | jq -r '.capture_id')
echo "Capture ID: $CAPTURE_ID"

# 步骤 4：查询状态
sleep 5
curl -s "http://localhost:8080/api/capture/status?capture_id=$CAPTURE_ID" | jq .
```

---

### 示例 2：使用内联 PDEF（Python）

```python
import requests
import json

# 动态生成 PDEF 内容
pdef_content = """
@protocol {
    name = "WebSocket";
    ports = 8080;
    endian = big;
}

@const {
    OP_TEXT = 0x01;
    OP_BINARY = 0x02;
}

WSFrame {
    uint8   fin_opcode;
    uint8   mask_len;
}

@filter TextFrames {
    fin_opcode & 0x0F = OP_TEXT;
}
"""

# 启动抓包
response = requests.post(
    'http://localhost:8080/api/capture/start',
    json={
        'iface': 'eth0',
        'filter': 'tcp port 8080',
        'duration': 120,
        'protocol_filter_inline': pdef_content.strip(),
        'category': 'websocket-debug'
    }
)

if response.status_code == 200:
    result = response.json()
    print(f"Capture started: ID={result['capture_id']}")
    print(f"Output path: {result['path']}")
else:
    print(f"Error: {response.status_code}")
    print(response.text)
```

---

### 示例 3：进程抓包（Bash）

```bash
#!/bin/bash

# 启动 nginx 进程的 HTTP 抓包
curl -X POST http://localhost:8080/api/capture/start \
  -H "Content-Type: application/json" \
  -d '{
    "proc_name": "nginx",
    "protocol": "http",
    "duration": 300,
    "category": "production"
  }' | jq .

# 系统自动：
# 1. 查找 nginx 进程监听的端口（如 80, 443）
# 2. 构建 BPF：tcp port 80 or tcp port 443
# 3. 加载 config/protocols/http.pdef
# 4. 开始抓包
```

---

### 示例 4：列出和查看 PDEF

```bash
#!/bin/bash

# 列出所有 PDEF
echo "=== All PDEF files ==="
curl -s http://localhost:8080/api/pdef/list | jq '.pdefs[] | {name, path, size}'

# 查看 HTTP 协议定义
echo -e "\n=== HTTP Protocol Definition ==="
curl -s "http://localhost:8080/api/pdef/get?name=http.pdef" | jq -r '.content'

# 查看上传的 PDEF
echo -e "\n=== Uploaded PDEF files ==="
curl -s http://localhost:8080/api/pdef/list | \
  jq -r '.pdefs[] | select(.type == "uploaded") | .path'
```

---

## 错误处理

### 通用错误响应格式

```json
{
  "success": false,
  "error": "错误描述",
  "code": "ERROR_CODE"
}
```

### 常见错误码

#### 抓包 API 错误

| HTTP 状态码 | 错误描述 | 原因 |
|------------|---------|------|
| 400 | `Invalid parameters` | 缺少必需参数或参数格式错误 |
| 400 | `Invalid iface` | 网卡不存在 |
| 400 | `Invalid PDEF syntax` | PDEF 语法错误 |
| 404 | `Process not found` | 指定的进程不存在 |
| 404 | `PDEF file not found` | PDEF 文件不存在 |
| 429 | `Too many captures` | 超过并发抓包限制 |
| 500 | `Failed to start capture` | 内部错误 |

#### PDEF 管理 API 错误

| HTTP 状态码 | 错误描述 | 原因 |
|------------|---------|------|
| 400 | `Missing 'name' or 'path' parameter` | 必需参数缺失 |
| 403 | `Invalid path` | 路径包含 `..` 或不在允许目录 |
| 404 | `PDEF not found` | 文件不存在 |
| 413 | `File too large` | 文件超过 2MB |
| 500 | `Failed to read file` | 读取文件失败 |

#### PDEF 上传 API 错误

| HTTP 状态码 | 错误描述 | 原因 |
|------------|---------|------|
| 400 | `Empty content` | 上传内容为空 |
| 400 | `Invalid PDEF syntax` | PDEF 语法错误 |
| 413 | `Payload too large` | 文件超过 2MB |
| 500 | `Failed to save file` | 保存文件失败 |

### 错误示例

#### 示例 1：PDEF 语法错误

**请求**：

```bash
curl -X POST http://localhost:8080/api/pdef/upload \
  --data-binary '@protocol { name = Test; }'  # 缺少引号
```

**响应**：

```json
{
  "success": false,
  "error": "Invalid PDEF syntax: expected string at line 1",
  "code": "PDEF_PARSE_ERROR"
}
```

#### 示例 2：进程不存在

**请求**：

```json
{
  "proc_name": "nonexistent_process",
  "duration": 60
}
```

**响应**：

```json
{
  "success": false,
  "error": "Process 'nonexistent_process' not found",
  "code": "PROCESS_NOT_FOUND"
}
```

#### 示例 3：超过并发限制

**请求**：启动第 9 个抓包任务（假设限制为 8）

**响应**：

```json
{
  "success": false,
  "error": "Maximum concurrent captures (8) reached",
  "code": "CAPTURE_LIMIT_REACHED"
}
```

---

## 最佳实践

### 1. 生产环境推荐配置

```json
{
  "proc_name": "nginx",              // 使用进程名，自动跟踪
  "protocol": "http",                // 使用预置协议
  "duration": 300,                   // 5 分钟
  "max_bytes": 1073741824,           // 最大 1GB
  "category": "production",          // 标记为生产环境
  "priority": "high"                 // 高优先级
}
```

**优点**：
- 简洁清晰
- 自动化程度高
- 使用稳定的预置协议

---

### 2. 调试/测试环境

```json
{
  "iface": "lo",
  "filter": "tcp port 9090",
  "protocol_filter_inline": "@protocol { ... }",
  "duration": 60,
  "category": "debug"
}
```

**优点**：
- 快速迭代
- 无需文件管理
- 灵活性高

---

### 3. 性能敏感场景

```json
{
  "iface": "eth0",
  "filter": "tcp port 80 and host 192.168.1.100",  // 精确 BPF
  "protocol": "http",
  "max_packets": 10000,                             // 限制包数
  "duration": 60
}
```

**优点**：
- BPF 预过滤减少负载
- 限制包数防止资源耗尽
- 精确控制抓包范围

---

### 4. 动态端口场景

```pdef
// game.pdef - 不指定 ports
@protocol {
    name = "GameProtocol";
    endian = big;
    // 不指定 ports，支持任意端口
}
```

```json
{
  "proc_name": "game_server",
  "protocol_filter": "game.pdef"
}
```

**优点**：
- 支持动态端口
- 不遗漏流量

---

### 5. 多租户场景

```python
def start_tenant_capture(tenant_id, protocol_def):
    """为不同租户启动定制化抓包"""
    response = requests.post(
        'http://capture-server/api/capture/start',
        json={
            'iface': f'tenant-{tenant_id}',
            'protocol_filter_inline': protocol_def,
            'category': f'tenant-{tenant_id}',
            'file_pattern': f'{{date}}-tenant-{tenant_id}-{{iface}}.pcap'
        }
    )
    return response.json()
```

---

### 6. 安全性建议

1. **路径安全**：
   - 只允许访问受控目录
   - 自动拒绝包含 `..` 的路径

2. **文件大小限制**：
   - PDEF 上传限制 2MB
   - 抓包文件设置 `max_bytes`

3. **并发控制**：
   - 配置 `max_concurrent_captures`
   - 使用优先级队列

4. **资源清理**：
   - 临时 PDEF 文件自动清理（TTL: 72 小时）
   - 抓包文件自动归档和清理

---

## 参考资料

- **PDEF 完整指南**: [PDEF_完整指南.md](PDEF_完整指南.md)
- **配置使用指南**: [../配置使用指南.md](../配置使用指南.md)
- **使用指南（中文）**: [../使用指南.md](../使用指南.md)

---

## 附录：完整参数表

### 抓包启动 API 完整参数

| 参数 | 类型 | 必需 | 默认值 | 说明 |
|------|------|------|--------|------|
| `capture_mode` | string | 否 | `"interface"` | 抓包模式 |
| `iface` | string | 条件 | `"any"` | 网卡接口 |
| `proc_name` | string | 条件 | - | 进程名 |
| `pid` / `target_pid` | int | 条件 | - | 进程 ID |
| `container_id` | string | 条件 | - | 容器 ID |
| `filter` | string | 否 | `""` | BPF 过滤器 |
| `port` / `port_filter` | int | 否 | `0` | 端口号 |
| `ip` / `ip_filter` | string | 否 | - | IP 地址 |
| `protocol` | string | 否 | - | 协议名 |
| `protocol_filter` | string | 否 | - | PDEF 文件路径 |
| `protocol_filter_inline` | string | 否 | - | 内联 PDEF 内容 |
| `duration` / `duration_sec` | int | 否 | `60` | 持续时间（秒） |
| `max_bytes` | int64 | 否 | `200*1024*1024` | 最大文件大小 |
| `max_packets` | int | 否 | `0` | 最大包数（0=无限制） |
| `file` | string | 否 | - | 输出文件路径 |
| `file_pattern` | string | 否 | 配置文件 | 文件名模式 |
| `category` | string | 否 | `"diag"` | 分类标签 |
| `priority` | string | 否 | `"normal"` | 优先级 |
| `user` / `request_user` | string | 否 | - | 请求用户 |
| `client_ip` | string | 否 | - | 客户端 IP |

**条件必需**：至少需要以下之一：
- `iface`（网卡模式）
- `proc_name`（进程名模式）
- `pid` / `target_pid`（进程 ID 模式）
- `container_id`（容器模式）

---

**文档版本**: 2.0
**最后更新**: 2025-01-15
**维护者**: rxtracenetcap 项目组
