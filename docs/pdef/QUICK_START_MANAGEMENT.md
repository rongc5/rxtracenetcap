# PDEF Management API 快速入门

## 快速开始

### 1. 启动服务器
```bash
cd /home/rong/gnrx/rxtracenetcap
./bin/rxtracenetcap &
```

### 2. 列出所有PDEF
```bash
curl http://localhost:8080/api/pdef/list
```

### 3. 获取PDEF内容
```bash
# 按名称
curl "http://localhost:8080/api/pdef/get?name=http.pdef"

# 按路径
curl "http://localhost:8080/api/pdef/get?path=config/protocols/dns.pdef"
```

## 常用操作

### 查看所有协议定义
```bash
curl -s http://localhost:8080/api/pdef/list | python -m json.tool
```

### 下载特定协议定义
```bash
curl -s "http://localhost:8080/api/pdef/get?name=http.pdef" | \
  python -c "import sys, json; print(json.load(sys.stdin)['content'])"
```

### 上传并验证PDEF
```bash
# 1. 上传
curl -X POST http://localhost:8080/api/pdef/upload \
  -H "Content-Type: text/plain" \
  --data-binary @my_protocol.pdef

# 2. 列出确认
curl http://localhost:8080/api/pdef/list | grep my_protocol

# 3. 读取验证
curl "http://localhost:8080/api/pdef/get?name=rxtracenetcap_pdef_*.pdef"
```

### 在捕获中使用PDEF
```bash
curl -X POST http://localhost:8080/api/capture/start \
  -H "Content-Type: application/json" \
  -d '{
    "mode": "interface",
    "iface": "eth0",
    "protocol": "http"
  }'
```

## 运行测试

### 完整测试
```bash
./tests/test_pdef_management_api.sh
```

### 使用示例
```bash
./tests/pdef_management_example.sh
```

## 可用的PDEF文件

系统预装的协议定义（位于 `config/protocols/`）：
- `http.pdef` - HTTP协议
- `dns.pdef` - DNS协议
- `mysql.pdef` - MySQL协议
- `redis.pdef` - Redis协议
- `mqtt.pdef` - MQTT协议
- `memcached.pdef` - Memcached协议
- `iec104.pdef` - IEC104工控协议

## API端点总结

| 端点 | 方法 | 参数 | 功能 |
|------|------|------|------|
| `/api/pdef/list` | GET | - | 列出所有PDEF文件 |
| `/api/pdef/get` | GET | `name` 或 `path` | 获取PDEF内容 |
| `/api/pdef/upload` | POST | body=PDEF内容 | 上传新PDEF |

## 故障排查

### 服务器未运行
```bash
# 检查进程
ps aux | grep rxtracenetcap

# 启动服务器
./bin/rxtracenetcap &
```

### 找不到PDEF文件
```bash
# 检查文件是否存在
ls -la config/protocols/
ls -la /tmp/rxtracenetcap_pdef/

# 验证文件权限
ls -l config/protocols/http.pdef
```

### 连接被拒绝
```bash
# 检查端口监听
netstat -tlnp | grep 8080

# 检查防火墙
sudo iptables -L | grep 8080
```

## 更多信息

- [完整API文档](./PDEF_MANAGEMENT_API.md)
- [实现细节](./PDEF_MANAGEMENT_IMPLEMENTATION.md)
- [PDEF上传API](./PDEF_UPLOAD_API.md)
- [PDEF使用指南](./PDEF_USAGE_GUIDE.md)

