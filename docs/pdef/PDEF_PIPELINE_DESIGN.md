# PDEF 滑动窗口过滤 - 流水线架构设计

## 架构概览

```
┌─────────────┐      ┌──────────────┐      ┌─────────────┐      ┌─────────────┐
│  抓包线程    │ -->  │  包队列       │ -->  │ 过滤线程池   │ -->  │  写入队列    │ --> [磁盘]
│ (Capture)   │      │ (Lock-Free)  │      │ (N threads) │      │ (Lock-Free) │
└─────────────┘      └──────────────┘      └─────────────┘      └─────────────┘
  快速收包              环形缓冲区          PDEF滑动窗口           批量写入
  不阻塞                                  CPU密集操作
```

## 核心组件

### 1. 数据结构

```cpp
// 包队列节点
struct PacketNode {
    struct pcap_pkthdr header;  // pcap 头
    uint8_t data[65535];         // 包数据
    uint16_t src_port;           // 预解析的端口
    uint16_t dst_port;
    uint8_t* app_data;           // 应用层数据指针
    uint32_t app_len;            // 应用层长度
    bool valid;                  // 是否有效
};

// 无锁环形队列
template<typename T, size_t Size>
class LockFreeQueue {
private:
    std::array<T, Size> buffer_;
    std::atomic<size_t> write_pos_{0};
    std::atomic<size_t> read_pos_{0};

public:
    bool push(const T& item);
    bool pop(T& item);
    size_t size() const;
};

// 过滤上下文
struct FilterContext {
    ProtocolDef* protocol_def;
    bool sliding_window;         // 是否启用滑动窗口
    uint32_t sliding_max_offset; // 最大搜索偏移（限制性能）
};
```

### 2. 线程职责

#### 抓包线程 (CRxCaptureJob)
```cpp
void capture_thread() {
    while (!stopping_) {
        pcap_dispatch(pcap, 100, capture_callback, user_data);
    }
}

void capture_callback(u_char* user, const pcap_pkthdr* h, const u_char* bytes) {
    PacketNode node;
    // 1. 拷贝包数据
    memcpy(&node.header, h, sizeof(*h));
    memcpy(node.data, bytes, h->caplen);

    // 2. 预解析（快速操作）
    ParsedPacket parsed = parse_packet(bytes, h->caplen);
    node.src_port = parsed.src_port;
    node.dst_port = parsed.dst_port;
    node.app_data = node.data + (parsed.app_data - bytes);
    node.app_len = parsed.app_len;
    node.valid = parsed.valid;

    // 3. 放入队列（非阻塞）
    if (!packet_queue_.push(node)) {
        // 队列满，统计丢包
        stats_.queue_drops++;
    }
}
```

#### 过滤线程池 (新增)
```cpp
void filter_thread() {
    PacketNode node;
    while (!stopping_) {
        // 1. 从队列取包
        if (!packet_queue_.pop(node)) {
            usleep(100);  // 队列空，短暂休眠
            continue;
        }

        // 2. 执行 PDEF 过滤（支持滑动窗口）
        bool matched = false;
        if (node.valid && node.app_len > 0) {
            if (filter_ctx_.sliding_window) {
                // 滑动窗口模式
                matched = sliding_window_match(
                    node.app_data,
                    node.app_len,
                    node.dst_port,
                    node.src_port,
                    filter_ctx_.protocol_def,
                    filter_ctx_.sliding_max_offset
                );
            } else {
                // 传统模式（从头匹配）
                matched = packet_filter_match(
                    node.app_data,
                    node.app_len,
                    node.dst_port,
                    filter_ctx_.protocol_def
                );
            }
        }

        // 3. 匹配的包放入写入队列
        if (matched) {
            if (!write_queue_.push(node)) {
                stats_.write_queue_drops++;
            }
        } else {
            stats_.packets_filtered++;
        }
    }
}

// 滑动窗口匹配实现
bool sliding_window_match(const uint8_t* data, uint32_t len,
                         uint16_t dst_port, uint16_t src_port,
                         const ProtocolDef* proto, uint32_t max_offset) {
    if (!proto || !data || len == 0) {
        return false;
    }

    // 限制搜索范围
    uint32_t search_limit = (max_offset > 0 && max_offset < len)
                            ? max_offset
                            : len;

    // 尝试匹配目标端口
    for (uint32_t offset = 0; offset < search_limit; offset++) {
        uint32_t remaining = len - offset;

        // 检查最小包大小
        bool min_size_ok = false;
        for (uint32_t i = 0; i < proto->filter_count; i++) {
            if (remaining >= proto->filters[i].min_packet_size) {
                min_size_ok = true;
                break;
            }
        }
        if (!min_size_ok) {
            break;  // 剩余长度不够，停止搜索
        }

        // 尝试匹配
        if (packet_filter_match(data + offset, remaining, dst_port, proto)) {
            return true;
        }
    }

    // 尝试匹配源端口
    for (uint32_t offset = 0; offset < search_limit; offset++) {
        uint32_t remaining = len - offset;
        if (packet_filter_match(data + offset, remaining, src_port, proto)) {
            return true;
        }
    }

    return false;
}
```

#### 写入线程 (新增)
```cpp
void write_thread() {
    PacketNode node;
    pcap_dumper_t* dumper = NULL;
    std::vector<PacketNode> batch;
    batch.reserve(100);

    while (!stopping_ || !write_queue_.empty()) {
        // 1. 批量读取
        while (batch.size() < 100 && write_queue_.pop(node)) {
            batch.push_back(node);
        }

        if (batch.empty()) {
            usleep(1000);
            continue;
        }

        // 2. 批量写入（减少系统调用）
        for (const auto& pkt : batch) {
            // 检查文件轮转
            if (need_rotate()) {
                rotate_file(&dumper);
            }

            // 写入 pcap
            pcap_dump((u_char*)dumper, &pkt.header, pkt.data);
        }

        // 3. 定期 flush
        static int batch_count = 0;
        if (++batch_count >= 10) {
            pcap_dump_flush(dumper);
            batch_count = 0;
        }

        batch.clear();
    }
}
```

## 配置参数

```json
{
    "iface": "eth0",
    "filter": "tcp port 2404",
    "protocol_filter": "/path/to/iec104.pdef",
    "pdef_filter_mode": {
        "sliding_window": true,
        "max_search_offset": 512,
        "filter_threads": 4
    }
}
```

## 性能分析

### 队列大小设计

```cpp
// 抓包队列：需要容纳短时突发流量
const size_t CAPTURE_QUEUE_SIZE = 10000;  // 约 100MB 内存

// 写入队列：过滤后数据量小
const size_t WRITE_QUEUE_SIZE = 5000;
```

### 预期性能

| 流量 | 抓包线程 CPU | 过滤线程 CPU (4核) | 写入线程 CPU | 总 CPU |
|------|-------------|-------------------|-------------|--------|
| 100 Mbps | 10% | 40% | 5% | 55% |
| 1 Gbps | 50% | 200% (4核) | 20% | 270% |

### 与当前实现对比

| 指标 | 当前（同步） | 流水线 | 提升 |
|------|-------------|--------|------|
| 抓包线程 CPU | 80% | 15% | **5.3x** |
| 丢包率 (1Gbps) | 15% | <1% | **15x** |
| 实时性 | 好 | 好 | 相同 |
| 磁盘 I/O | 1x | 1x | 相同 |

## 实施步骤

### 阶段 1：基础框架（2-3小时）
- [ ] 实现 LockFreeQueue 无锁队列
- [ ] 创建 PacketNode 数据结构
- [ ] 修改 CRxCaptureJob 使用队列

### 阶段 2：过滤线程池（2小时）
- [ ] 实现 CRxFilterThread 类
- [ ] 实现 sliding_window_match 函数
- [ ] 线程池管理（启动/停止）

### 阶段 3：写入线程（1小时）
- [ ] 实现 CRxWriterThread 类
- [ ] 批量写入优化
- [ ] 文件轮转支持

### 阶段 4：测试和优化（2小时）
- [ ] 压力测试（iperf3 + tcpreplay）
- [ ] 性能分析（perf）
- [ ] 参数调优

## 向后兼容

如果不启用 sliding_window，退化为当前实现：
- 过滤线程数 = 1
- 直接调用 packet_filter_match（无滑动窗口）

## 监控指标

```json
{
    "capture_stats": {
        "packets_received": 1000000,
        "queue_drops": 0,
        "queue_size": 234
    },
    "filter_stats": {
        "packets_processed": 1000000,
        "packets_matched": 50000,
        "packets_filtered": 950000,
        "avg_filter_time_us": 15
    },
    "write_stats": {
        "packets_written": 50000,
        "bytes_written": 75000000,
        "write_queue_drops": 0
    }
}
```
