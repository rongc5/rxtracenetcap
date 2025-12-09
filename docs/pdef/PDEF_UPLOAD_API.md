# PDEF 协议定义上传 API 使用指南

为了解决在 JSON 中直接传递 PDEF 内容导致的可读性差和转义复杂的问题，引入了分步上传机制。

## 核心设计

将协议定义（PDEF）的传递与抓包任务的启动解耦：
1. **上传 PDEF**：使用专门的 API 上传原始 PDEF 文件，获取服务器上的临时路径。
2. **启动任务**：在启动抓包的 JSON 配置中，直接引用上一步获取的路径。

## API 详情

### 1. 上传 PDEF 文件

将 PDEF 文件原始内容直接作为 HTTP Body 发送（无需 JSON 包装）。

- URL: `POST /api/pdef/upload`
- Content-Type: `text/plain` (或任意)
- Body: PDEF 文件内容
- 限制: 最大 2MB；上传后会立即尝试解析校验

请求示例:
```bash
curl -X POST http://localhost:8080/api/pdef/upload \
  --data-binary @config/protocols/game.pdef
```

响应示例:
```json
{
  "status": "ok",
  "path": "/tmp/rxtracenetcap_pdef/rxtracenetcap_pdef_1701234567_000123_42.pdef",
  "size": 1024,
  "checksum": "3af01b2c1234abcd",
  "validated": true
}
```

字段说明：
- `path`: 后续任务可引用的临时文件路径（受控目录 `/tmp/rxtracenetcap_pdef` 下）
- `size`: 字节数
- `checksum`: 上传体的 FNV1a-64 摘要，便于端到端校验
- `validated`: 服务器已成功解析并通过语法/语义校验

### 2. 使用上传的 PDEF 启动抓包

获取到 `path` 后，将其填入 `protocol_filter` 字段。

请求示例:
```bash
curl -X POST http://localhost:8080/api/capture/start \
  -H "Content-Type: application/json" \
  -d '{
    "iface": "eth0",
    "filter": "tcp port 7777",
    "protocol_filter": "/tmp/rxtracenetcap_pdef/rxtracenetcap_pdef_1701234567_000123_42.pdef"
  }'
```

## 注意事项

- 上传的文件存储在受控临时目录 `/tmp/rxtracenetcap_pdef`，权限 0600，重启后可能会丢失。
- 每次上传都会生成新文件，建议在客户端管理好上传频率，并按需清理旧的路径。
- 上传会立即校验 PDEF 语法/语义；若返回错误，请修正后再上传。






