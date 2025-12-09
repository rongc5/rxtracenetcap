# PDEF 滑动窗口快速入门

## 快速开始（3分钟）

### 1. 修改你的 PDEF 文件

```bash
vim config/protocols/iec104.pdef
```

添加滑动窗口过滤器：

```c
@filter GI_Anywhere {
    sliding = true;          // 启用滑动窗口
    sliding_max = 512;       // 搜索前512字节（可选）
    apci.start = 0x68;
    asdu.type_id = 100;
}
```

### 2. 编译项目

```bash
make clean && make
```

### 3. 使用（两种方式）

#### 方式A：直接指定本地PDEF文件

```bash
curl -X POST http://localhost:8080/api/capture/start \
  -H "Content-Type: application/json" \
  -d '{
    "iface": "eth0",
    "filter": "tcp port 2404",
    "protocol_filter": "/path/to/iec104.pdef"
  }'
```

#### 方式B：先上传，再使用

```bash
# 1. 上传PDEF
RESPONSE=$(curl -s -X POST http://localhost:8080/api/pdef/upload \
  --data-binary @config/protocols/iec104.pdef)

PDEF_PATH=$(echo "$RESPONSE" | jq -r '.path')
echo "Uploaded to: $PDEF_PATH"

# 2. 启动抓包
curl -X POST http://localhost:8080/api/capture/start \
  -H "Content-Type: application/json" \
  -d "{
    \"iface\": \"eth0\",
    \"filter\": \"tcp port 2404\",
    \"protocol_filter\": \"$PDEF_PATH\"
  }"
```

## 语法参考

### 滑动窗口选项

```c
@filter MyFilter {
    // === 配置选项 ===
    sliding = true;          // 是否启用滑动窗口（默认false）
    sliding_max = 512;       // 最大搜索偏移（0=无限制，默认0）

    // === 匹配条件 ===
    field1.value = 0x68;
    field2.value = 100;
}
```

### 完整示例

```c
@protocol {
    name = "MyProtocol";
    ports = 1234, 5678;
    endian = little;
}

@const {
    MAGIC = 0xABCD;
}

Header {
    uint16  magic;
    uint16  length;
    uint32  seq;
}

// 传统模式：必须从包头开始匹配
@filter StrictMatch {
    header.magic = MAGIC;
    header.length > 10;
}

// 滑动窗口：在包中任意位置查找
@filter FlexibleMatch {
    sliding = true;
    sliding_max = 1024;      // 只搜索前1KB
    header.magic = MAGIC;
}
```

## 性能建议

| 流量大小 | 推荐配置 | 原因 |
|---------|---------|------|
| < 100 Mbps | `sliding_max = 0` (无限制) | 流量小，CPU够用 |
| 100-500 Mbps | `sliding_max = 512` 或 `1024` | 平衡性能和匹配范围 |
| > 500 Mbps | `sliding_max = 256` | 减少CPU负载 |

## 验证功能

```bash
# 1. 检查PDEF编译
make pdef

# 2. 运行测试
./tests/test_sliding_window.sh

# 3. 查看IEC104示例
cat config/protocols/iec104.pdef | grep -A 5 "sliding"
```

## 常见问题

### Q: 滑动窗口会降低性能吗？

A: 是的，但可以通过 `sliding_max` 控制：
- `sliding_max = 0`：全包搜索，性能最差
- `sliding_max = 512`：只搜索前512字节，性能适中
- 传统模式（`sliding = false`）：性能最好

### Q: 如何知道是否匹配成功？

A: 查看抓包统计：

```bash
curl http://localhost:8080/api/capture/status | jq '.packets_filtered'
```

如果 `packets_filtered` 很高，说明大部分包被过滤掉了（没匹配）。

### Q: 可以在同一个PDEF中混用吗？

A: 可以！

```c
@filter StrictFilter {
    // 不启用sliding，从头匹配
    header.type = 1;
}

@filter FlexibleFilter {
    sliding = true;   // 启用sliding，任意位置匹配
    header.type = 2;
}
```

只要任何一个filter匹配，包就会被保留。

### Q: 如何调试匹配不到包的问题？

1. **简化过滤条件**：
   ```c
   @filter Debug {
       sliding = true;
       header.magic = 0x68;  // 只匹配一个字段
   }
   ```

2. **取消PDEF过滤**：
   ```bash
   # 不指定 protocol_filter，看是否能抓到包
   curl -X POST ... -d '{"iface": "eth0", "filter": "tcp port 2404"}'
   ```

3. **检查端口**：
   ```c
   @protocol {
       ports = 2404;  // 确保端口正确
   }
   ```

## 技术细节

### 实现位置

- **语法解析**：`src/pdef/parser.c:440-485`
- **运行时匹配**：`src/runtime/protocol.c:31-50`
- **类型定义**：`src/pdef/pdef_types.h:108-109`

### 算法

```c
for (offset = 0; offset < min(packet_len, sliding_max); offset++) {
    if (remaining_len < min_packet_size) break;  // 提前终止
    if (execute_filter(packet + offset, remaining_len, rule)) {
        return true;  // 匹配成功
    }
}
```

## 更多资源

- 完整设计：`PDEF_PIPELINE_DESIGN.md`
- 实现总结：`SLIDING_WINDOW_IMPLEMENTATION_SUMMARY.md`
- API文档：`PDEF_UPLOAD_API.md`
- 集成指南：`PDEF_INTEGRATION_USAGE.md`
