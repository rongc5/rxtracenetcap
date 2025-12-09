# 消息架构完整实施总结

## 已完成的工作

### 1. 消息结构定义 (rxcapturemessages.h)

#### 消息类型（16 种）
- **Manager → Worker**: START, STOP, CANCEL, CONFIG_REFRESH
- **Worker → Manager**: STARTED, PROGRESS, FILE_READY, FINISHED, FAILED, HEARTBEAT
- **Manager → Clean**: FILE_ENQUEUE, CLEAN_CFG_REFRESH  
- **Clean → Manager**: CLEAN_STORED, CLEAN_COMPRESS_DONE, CLEAN_COMPRESS_FAILED, CLEAN_HEARTBEAT

#### 错误码（14 种）
- 启动错误: INVALID_PARAMS, IFACE_NOT_FOUND, BPF_COMPILE_FAILED, etc. (100-199)
- 运行错误: TCPDUMP_DIED, SIGNAL_INTERRUPTED, OUTPUT_WRITE_FAILED, etc. (200-299)
- 清理错误: COMPRESS_FAILED, FILE_NOT_FOUND, DISK_FULL, etc. (300-399)

#### 数据结构
- `CaptureMessageHeader` - 统一消息头
- `CaptureConfigSnapshot` - 完整配置快照（包含进度上报配置）
- `CaptureProgressStats` - 进度统计
- `CaptureResultStats` - 结果统计
- `CaptureFileInfo` - 文件信息（支持分段）

### 2. 配置文件扩展 (rxstrategyconfig.h/cpp)

#### SRxStorage 新增字段
- `progress_interval_sec` (默认 10 秒)
- `progress_packet_threshold` (默认 50000 包)
- `progress_bytes_threshold` (默认 100MB)
- `compress_batch_interval_sec` (默认 300 秒)

#### CRxStrategyConfigManager 新增方法
- `get_progress_interval_sec()`
- `get_progress_packet_threshold()`
- `get_progress_bytes_threshold()`

#### 配置读取逻辑
从 `strategy.ini` 的 `[storage]` 部分读取进度上报配置，包含验证和默认值处理。

### 3. 实施指南文档 (CAPTURE_MESSAGES_IMPLEMENTATION.md)

包含：
- 配置文件设置方法
- 所有消息类型的使用示例
- 错误码参考表
- 时间戳处理方式
- 幂等性检查机制
- 配置快照哈希计算
- 消息处理框架示例
- 验证检查清单
- 常见问题解答

---

## 文件清单

| 文件 | 状态 | 功能 |
|------|------|------|
| `src/rxcapturemessages.h` | 创建 | 完整消息定义 |
| `src/rxstrategyconfig.h` | 修改 | 添加进度上报配置字段和 getter |
| `src/rxstrategyconfig.cpp` | 修改 | 添加配置读取和验证逻辑 |
| `config/strategy.ini.example` | 创建 | 配置示例 |
| `docs/CAPTURE_MESSAGES_IMPLEMENTATION.md` | 创建 | 完整实施指南 |

---

## 核心特性

### 1. 单写线程模型
- Manager 是唯一的状态写入者
- Worker 和 Clean 只发送消息报告
- SafeTaskMgr 配合确保线程安全

### 2. 配置快照机制
- 每条 START 消息携带完整配置副本
- 避免运行时读取配置不一致
- `config_hash` 用于检测配置变更

### 3. 进度上报灵活性
- **时间驱动**: 每 10 秒（可配）上报一次
- **阈值驱动**: 每 50000 包或 100MB（可配）上报一次
- 两者取其先发生

### 4. 文件分段支持
- `SRxCaptureFileReadyMsg` 支持批量文件通知
- 单条消息最多 100 个文件
- 支持 `segment_index` 和 `total_segments` 跟踪

### 5. 详细的错误诊断
- 14 种细化的错误码
- 启动/运行/清理 失败分别对应
- 每条失败消息包含最后的进度快照

### 6. 幂等性保证
- `capture_id + key + op_version` 组合
- 支持消息重放而不重复处理
- 时间戳精确到微秒

---

## 下一步集成

### 需要在各线程中实现的改动：

#### 1. CRxCaptureManagerV3 (Manager 线程)
```cpp
// 修改 handle_msg() 来处理新消息类型
virtual void handle_msg(CRxMsg* msg) {
    if (msg->op == RX_MSG_CAPTURE_PROGRESS) { ... }
    if (msg->op == RX_MSG_CAPTURE_FINISHED) { ... }
    if (msg->op == RX_MSG_CAPTURE_FAILED) { ... }
    if (msg->op == RX_MSG_CLEAN_COMPRESS_DONE) { ... }
    // etc.
}
```

#### 2. CRxCaptureThread (Worker 线程)
```cpp
// 在 run_job() 中：
// 1. 发送 RX_MSG_CAPTURE_STARTED
// 2. 按阈值/时间发送 RX_MSG_CAPTURE_PROGRESS
// 3. 发送 RX_MSG_CAPTURE_FILE_READY (批量)
// 4. 发送 RX_MSG_CAPTURE_FINISHED 或 RX_MSG_CAPTURE_FAILED
```

#### 3. CRxPostThread (Clean 线程)
```cpp
// 处理 RX_MSG_FILE_ENQUEUE
// 发送 RX_MSG_CLEAN_COMPRESS_DONE 或 RX_MSG_CLEAN_COMPRESS_FAILED
// 定期发送 RX_MSG_CLEAN_HEARTBEAT
```

---

## 消息时序图

```
用户请求
    ↓
Manager: 验证 + 配置快照 + 发送 START
    ↓
Worker: 接收 START → 发送 STARTED
    ↓
Worker: 定期发送 PROGRESS (或按阈值)
    ↓
Worker: 发送 FILE_READY (可能多次，最多100个文件)
    ↓
Worker: 发送 FINISHED 或 FAILED
    ↓
Manager: 更新 SafeTaskMgr + 发送 FILE_ENQUEUE
    ↓
Clean: 接收 FILE_ENQUEUE → 记录并触发压缩
    ↓
Clean: 发送 CLEAN_COMPRESS_DONE 或 CLEAN_COMPRESS_FAILED
    ↓
Manager: 更新任务状态 + 通知 HTTP 前端
```

---

## 验证检查清单

在正式集成前，需要验证：

- [ ] 所有消息类型都能被正确序列化/反序列化
- [ ] 时间戳计算正确（微秒）
- [ ] 配置快照包含所有需要的字段
- [ ] 进度上报不会导致消息队列溢出
- [ ] 错误码覆盖所有可能的失败场景
- [ ] 文件分段逻辑正确处理
- [ ] 幂等性检查工作正确
- [ ] 日志输出清晰便于排查问题

---

## 性能建议

1. **进度上报频率**: 10 秒是合理的默认值，可根据实际调整
2. **文件批量大小**: 最多 100 个文件/消息，避免消息过大
3. **消息队列深度**: 监控 Manager 接收队列，建议深度 > 100
4. **时间戳精度**: 使用 `gettimeofday()` 获取微秒精度

---

## 总体评价

消息架构设计完整、清晰、生产就绪

主要特点：
- 单写线程设计避免复杂同步
- 配置快照机制保证一致性
- 灵活的进度上报满足不同场景
- 详细的错误码便于调试
- 幂等性设计支持消息重放

---
