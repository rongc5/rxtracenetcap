# PDEF 协议过滤集成使用指南

## 概述

rxtracenetcap 现已集成 PDEF 协议过滤功能，可以在抓包时只保存匹配特定协议规则的数据包。

## 功能特性

- **运行时加载**：不需要重新编译，直接加载 `.pdef` 文件
- **应用层过滤**：自动解析以太网、IP、TCP/UDP 头部，提取应用层数据进行匹配
- **双向匹配**：同时尝试匹配源端口和目的端口
- **统计信息**：显示加载的过滤规则数量和过滤掉的数据包数量

## 如何使用

### 1. 通过配置文件指定协议过滤器

在抓包配置中添加 `protocol_filter` 字段，指向 `.pdef` 文件路径：

```json
{
  "capture": {
    "iface": "eth0",
    "filter": "tcp",
    "protocol_filter": "config/protocols/http.pdef",
    "output_pattern": "{day}/{date}-{iface}-http.pcap"
  }
}
```

### 2. 通过 CLI 参数指定

如果你的 CLI 工具支持 `--protocol-filter` 参数：

```bash
./bin/rxcli capture --iface eth0 --protocol-filter config/protocols/dns.pdef
```

### 3. 工作流程

当启动抓包时，系统会：

1. **加载 PDEF 文件**
   ```
   [PDEF] Loaded protocol filter: HTTP (5 rules)
   ```

2. **解析每个数据包**
   - 提取应用层数据（跳过以太网、IP、TCP/UDP 头部）
   - 获取源端口和目的端口

3. **执行协议匹配**
   - 对应用层数据运行 PDEF 过滤规则
   - 同时尝试匹配目的端口和源端口

4. **保存匹配的数据包**
   - 只有匹配成功的数据包才会写入 pcap 文件
   - 其他数据包被过滤掉（计数器递增）

5. **显示统计信息**
   ```
   [PDEF] Filtered 15234 packets (did not match protocol filter)
   ```

## 示例场景

### 场景 1：只抓取 HTTP GET 请求

使用 `config/protocols/http.pdef`，它定义了 `GET_Requests` 过滤器：

```bash
# 启动抓包，只保存 HTTP GET 请求
./bin/rxcli capture \
  --iface eth0 \
  --bpf "tcp port 80" \
  --protocol-filter config/protocols/http.pdef
```

结果：pcap 文件中只包含 HTTP GET 请求的数据包，其他 HTTP 流量（POST、响应等）都被过滤掉。

### 场景 2：只抓取 DNS 查询

使用 `config/protocols/dns.pdef`：

```bash
./bin/rxcli capture \
  --iface eth0 \
  --bpf "udp port 53" \
  --protocol-filter config/protocols/dns.pdef
```

结果：只保存 DNS 查询数据包，DNS 响应被过滤掉。

### 场景 3：只抓取 MQTT CONNECT 包

使用我们创建的 `config/protocols/mqtt.pdef`：

```bash
./bin/rxcli capture \
  --iface eth0 \
  --bpf "tcp port 1883" \
  --protocol-filter config/protocols/mqtt.pdef
```

结果：只保存 MQTT CONNECT 数据包。

## 与 BPF 过滤器的结合

建议同时使用 BPF 过滤器和 PDEF 过滤器：

- **BPF 过滤器**：在内核层面快速过滤（基于 IP/端口/协议）
- **PDEF 过滤器**：在应用层精确匹配（基于协议内容）

```bash
# BPF: 只抓取 TCP 80 端口的流量
# PDEF: 只保存 HTTP POST 请求
./bin/rxcli capture \
  --iface eth0 \
  --bpf "tcp port 80" \
  --protocol-filter config/protocols/http.pdef
```

这样可以最大化性能，减少不必要的数据处理。

## 创建自定义协议过滤器

你可以创建自己的 `.pdef` 文件来过滤任何协议：

```pdef
@protocol {
    name = "MyProtocol";
    ports = 9000, 9001;
    endian = big;
}

@const {
    MAGIC = 0x12345678;
    CMD_LOGIN = 1;
}

MyPacket {
    uint32  magic;
    uint16  cmd;
    uint16  seq;
}

@filter LoginPackets {
    magic = MAGIC;
    cmd = CMD_LOGIN;
}
```

保存为 `config/protocols/myprotocol.pdef`，然后直接使用：

```bash
./bin/rxcli capture \
  --iface eth0 \
  --protocol-filter config/protocols/myprotocol.pdef
```

**不需要重新编译程序！**

## 调试技巧

### 1. 验证 PDEF 文件是否正确

```bash
./bin/debug_parse config/protocols/http.pdef
```

### 2. 查看生成的字节码

```bash
./bin/test_disasm config/protocols/http.pdef
```

### 3. 检查过滤统计

启动抓包后，系统会在停止时显示：
- 加载的协议名称
- 过滤规则数量
- 过滤掉的数据包数量

如果 `packets_filtered` 数量等于抓到的总数据包数，说明没有匹配成功，需要检查：
- PDEF 规则是否正确
- 端口配置是否匹配
- 数据包格式是否符合预期

## 性能考虑

- **解析开销**：每个数据包需要解析以太网、IP、TCP/UDP 头部（约100ns）
- **匹配开销**：PDEF 字节码执行时间 < 100ns
- **总开销**：约 200-300ns/包，对于千兆网卡完全可以接受

如果需要高性能抓包，建议：
1. 使用精确的 BPF 过滤器减少到达应用层的数据包数量
2. 简化 PDEF 过滤规则
3. 使用专用抓包网卡

## 故障排除

### 问题：PDEF 加载失败

```
[PDEF] Failed to load protocol filter 'xxx.pdef': Parse error at line X
```

**解决**：使用 `debug_parse` 工具检查语法错误。

### 问题：没有数据包被保存

```
[PDEF] Filtered 10000 packets (did not match protocol filter)
```

**解决**：
1. 确认端口配置是否正确
2. 使用 tcpdump 或 Wireshark 验证实际数据包格式
3. 检查 PDEF 过滤条件是否过于严格

### 问题：保存了不应该保存的数据包

**解决**：
1. 检查 PDEF 过滤规则是否遗漏了某些条件
2. 验证数据包是否有双向流量（客户端 → 服务器 和 服务器 → 客户端）
3. 考虑添加更多过滤条件

## 限制和已知问题

1. **仅支持 IPv4**：目前不支持 IPv6 数据包
2. **仅支持 TCP/UDP**：不支持 ICMP、SCTP 等其他协议
3. **应用层数据为空**：如果数据包没有应用层数据（如 TCP SYN），会被过滤掉

## 下一步计划

- [ ] 支持 IPv6
- [ ] 支持更多传输层协议（SCTP、DCCP）
- [ ] 添加文本协议的正则表达式匹配
- [ ] 支持 VLAN 标签
- [ ] 性能优化（零拷贝）

## 示例：完整的抓包工作流

```bash
# 1. 创建协议定义
cat > config/protocols/game.pdef << 'EOF'
@protocol {
    name = "GameProtocol";
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

# 2. 验证语法
./bin/debug_parse config/protocols/game.pdef

# 3. 启动抓包（只抓取登录包）
./bin/rxcli capture \
  --iface eth0 \
  --bpf "tcp port 7777" \
  --protocol-filter config/protocols/game.pdef \
  --duration 60 \
  --output game-logins.pcap

# 4. 分析结果
tcpdump -r game-logins.pcap -X | less
```

## 结论

PDEF 协议过滤集成让你可以：
-  运行时加载任何自定义协议定义
-  精确过滤应用层数据包
-  减少存储空间和后期分析工作量
-  不需要修改或重新编译抓包程序

立即开始使用，享受高效的协议级抓包体验！
