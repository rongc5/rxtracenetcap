# PDEF Management API

## 概述

PDEF Management API 提供查询和获取协议定义文件（PDEF）的功能，使用户能够：
- 列出所有可用的PDEF文件
- 获取特定PDEF文件的内容

## API端点

### 1. 列出所有PDEF文件

**端点**: `GET /api/pdef/list`

**描述**: 返回所有可用的PDEF文件列表，包括：
- `config/protocols/` 目录中的预定义协议文件
- `/tmp/rxtracenetcap_pdef/` 目录中用户上传的临时文件

**请求示例**:
```bash
curl -X GET http://localhost:8080/api/pdef/list
```

**响应示例**:
```json
{
  "status": "ok",
  "pdefs": [
    {
      "name": "http.pdef",
      "path": "config/protocols/http.pdef",
      "size": 1234,
      "mtime": "2025-12-08T10:30:00Z"
    },
    {
      "name": "dns.pdef",
      "path": "config/protocols/dns.pdef",
      "size": 856,
      "mtime": "2025-12-08T10:30:00Z"
    }
  ]
}
```

**响应字段**:
- `status`: 操作状态（"ok"）
- `pdefs`: PDEF文件数组
  - `name`: 文件名
  - `path`: 完整路径
  - `size`: 文件大小（字节）
  - `mtime`: 最后修改时间（ISO 8601 格式）

---

### 2. 获取PDEF文件内容

**端点**: `GET /api/pdef/get`

**描述**: 获取指定PDEF文件的完整内容

**参数**:
- `name` (可选): PDEF文件名（例如：`http.pdef`）
- `path` (可选): PDEF文件的完整路径

**注意**: 必须提供 `name` 或 `path` 参数之一

**请求示例1** - 按名称获取:
```bash
curl -X GET "http://localhost:8080/api/pdef/get?name=http.pdef"
```

**请求示例2** - 按路径获取:
```bash
curl -X GET "http://localhost:8080/api/pdef/get?path=config/protocols/dns.pdef"
```

**响应示例**:
```json
{
  "status": "ok",
  "path": "config/protocols/http.pdef",
  "size": 1234,
  "mtime": "2025-12-08T10:30:00Z",
  "content": "protocol http {\n  port 80, 443;\n  ...\n}"
}
```

**响应字段**:
- `status`: 操作状态（"ok"）
- `path`: 文件完整路径
- `size`: 文件大小（字节）
- `mtime`: 最后修改时间（ISO 8601 格式）
- `content`: PDEF文件的完整文本内容

---

## 错误响应

所有错误都会返回适当的HTTP状态码和JSON错误消息：

**400 Bad Request** - 缺少参数:
```json
{
  "error": "Missing 'name' or 'path' parameter"
}
```

**403 Forbidden** - 无效路径（包含路径遍历）:
```json
{
  "error": "Invalid path"
}
```

**404 Not Found** - 文件不存在:
```json
{
  "error": "PDEF not found"
}
```

**413 Payload Too Large** - 文件过大:
```json
{
  "error": "File too large"
}
```

**500 Internal Server Error** - 服务器内部错误:
```json
{
  "error": "Failed to read file"
}
```

---

## 安全特性

### 路径安全
- 只允许访问以下目录中的文件：
  - `config/protocols/`
  - `/tmp/rxtracenetcap_pdef/`
- 自动拒绝包含 `..` 的路径遍历尝试
- 验证文件必须是常规文件

### 文件大小限制
- 最大文件大小：2MB
- 超过限制的文件将被拒绝

### 按名称查找
使用 `name` 参数时，系统会按以下顺序搜索：
1. `config/protocols/<name>`
2. `/tmp/rxtracenetcap_pdef/<name>`

---

## 使用场景

### 场景1: 浏览可用协议
1. 调用 `/api/pdef/list` 获取所有可用的PDEF文件
2. 选择需要的协议
3. 使用 `/api/pdef/get?name=<filename>` 获取内容

### 场景2: 验证上传的PDEF
1. 使用 `/api/pdef/upload` 上传PDEF文件
2. 使用 `/api/pdef/list` 确认文件已保存
3. 使用 `/api/pdef/get` 读取并验证内容

### 场景3: 导出配置
1. 使用 `/api/pdef/list` 列出所有协议定义
2. 批量使用 `/api/pdef/get` 下载所有PDEF文件
3. 备份或迁移到其他系统

---

## 测试

运行测试脚本验证功能：
```bash
./tests/test_pdef_management_api.sh
```

确保服务器正在运行：
```bash
./bin/rxtracenetcap &
```

---

## 与其他API的集成

### 与Capture API集成
在启动捕获时，可以指定protocol或protocol_filter：
```bash
curl -X POST http://localhost:8080/api/capture/start \
  -H "Content-Type: application/json" \
  -d '{
    "mode": "interface",
    "iface": "eth0",
    "protocol": "http"
  }'
```

系统会：
1. 查找对应的PDEF文件（如 `config/protocols/http.pdef`）
2. 使用该PDEF进行协议过滤

### 与Upload API集成
上传的PDEF文件会保存到 `/tmp/rxtracenetcap_pdef/`，可以：
1. 通过 `/api/pdef/list` 查看
2. 通过 `/api/pdef/get` 获取内容
3. 在capture中通过路径引用使用

---

## 实现细节

### 文件路径解析
- 相对路径自动解析为绝对路径
- 支持URL参数中的路径编码

### 时间格式
所有时间戳使用UTC时间，格式为ISO 8601：
```
YYYY-MM-DDTHH:MM:SSZ
```

### JSON转义
文件内容在返回时会进行JSON转义：
- 特殊字符（`\`, `"`, 换行等）自动转义
- 确保JSON格式正确

---

## 限制与注意事项

1. **文件大小**: 单个PDEF文件最大2MB
2. **并发访问**: 支持多个客户端同时查询
3. **临时文件**: `/tmp/rxtracenetcap_pdef/` 中的文件在系统重启后会丢失
4. **权限**: 需要读取配置目录的权限
5. **编码**: PDEF文件应使用UTF-8编码

---

## 未来增强

可能的功能扩展：
- [ ] PDEF文件验证和语法检查
- [ ] PDEF文件版本管理
- [ ] 支持搜索和过滤功能
- [ ] 支持删除临时PDEF文件
- [ ] 支持更新已存在的PDEF文件
- [ ] 添加分页支持（当文件数量很大时）

---

## 相关文档

- [PDEF Upload API](./PDEF_UPLOAD_API.md)
- [PDEF Usage Guide](./PDEF_USAGE_GUIDE.md)
- [Capture API Design](../md/capture_messages_design.md)

