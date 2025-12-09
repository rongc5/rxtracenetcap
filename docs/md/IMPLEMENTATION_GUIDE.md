# 抓包系统消息结构实施指南

## 📚 文档导航

本次设计产出了以下文件：

1. **`capture_messages_design.md`** - 完整的设计文档（推荐先看）
   - 设计原则和评审意见
   - 完整的消息枚举和数据结构
   - 消息处理时序图
   - 实现建议和注意事项

2. **`src/rxcapturemessages.h`** - 可直接使用的扩展消息头文件
   - 基于现有 `rxcapturemessages.h` 的扩展
   - 包含所有新增消息类型
   - 兼容 C++98

3. **`docs/safe_task_mgr_extensions.h`** - SafeTaskMgr 扩展接口示例
   - 便捷方法定义
   - 使用示例
   - Functor 实现（C++98 兼容）

4. **本文档** - 实施步骤和检查清单

---

## 🎯 评审总结

### 你的方案优点

 **单写线程模型** - 与 SafeTaskMgr 完美契合
 **消息统一承载** - 使用 normal_msg + _msg_op
 **配置快照机制** - 避免运行时配置不一致
 **幂等性考虑** - capture_id + key + op_version
 **组件分工清晰** - Manager/Worker/Clean/SAM 职责明确

### 需要补充的部分

📝 **消息类型不够完整**
- 缺少进度上报消息 `RX_MSG_CAPTURE_PROGRESS`
- 缺少文件就绪通知 `RX_MSG_CAPTURE_FILE_READY`
- 缺少 Clean 线程相关消息

📝 **配置快照缺失**
- 现有消息没有携带配置快照
- 需要 `CaptureConfigSnapshot` 结构

📝 **时间戳不统一**
- 建议所有消息使用 `int64_t ts_usec`（微秒）

📝 **错误处理不够细化**
- 需要错误码枚举 `ECaptureErrorCode`
- 区分启动失败、运行失败、清理失败

---

## 🚀 实施步骤

### 阶段 1：扩展消息枚举（1 小时）

**目标**：在 `rx_msg_types.h` 中添加新消息类型

**步骤**：

1. 打开 `src/rx_msg_types.h`
2. 在 `ERxCaptureMsg` 枚举中添加新消息（参考 `rxcapturemessages.h`）：

```cpp
enum ERxCaptureMsg {
    // ... 现有消息 ...
    RX_MSG_TASK_UPDATE = 2007,

    // ===== 新增：Worker -> Manager =====
    RX_MSG_CAPTURE_PROGRESS = 2111,      // 抓包进度
    RX_MSG_CAPTURE_FILE_READY = 2112,    // 文件就绪
    RX_MSG_CAPTURE_HEARTBEAT = 2115,     // 心跳

    // ===== 新增：Manager -> Clean =====
    RX_MSG_FILE_ENQUEUE = 2120,          // 文件入队
    RX_MSG_CLEAN_CFG_REFRESH = 2121,     // 清理配置刷新
    RX_MSG_CLEAN_SHUTDOWN = 2122,        // 清理线程关闭

    // ===== 新增：Clean -> Manager =====
    RX_MSG_CLEAN_STORED = 2130,          // 文件记录完成
    RX_MSG_CLEAN_COMPRESS_DONE = 2131,   // 压缩完成
    RX_MSG_CLEAN_COMPRESS_FAILED = 2132, // 压缩失败
    RX_MSG_CLEAN_HEARTBEAT = 2133        // 清理心跳
};
```

3. 更新 `rx_msg_type_to_string()` 函数

**验证**：
- [ ] 编译通过
- [ ] 枚举值无冲突

---

### 阶段 2：扩展消息结构（2 小时）

**目标**：在 `rxcapturemessages.h` 中添加新消息结构

**步骤**：

1. 打开 `src/rxcapturemessages.h`

2. 在文件开头添加新的数据结构（参考 `rxcapturemessages.h`）：
   - `ECaptureErrorCode` 枚举
   - `CaptureConfigSnapshot` 结构
   - `CaptureStats` 结构
   - `CaptureFileInfo` 结构

3. 添加新消息结构：
   - `SRxCaptureProgressMsg`
   - `SRxCaptureFileReadyMsg`
   - `SRxFileEnqueueMsg`
   - `SRxCleanStoredMsg`
   - `SRxCleanCompressDoneMsg`
   - `SRxCleanCompressFailedMsg`
   - 等等...

4. 添加辅助函数：
   - `get_current_usec()`
   - `error_code_to_string()`

**验证**：
- [ ] 编译通过
- [ ] 结构体大小合理（避免过大）
- [ ] 构造函数初始化所有字段

---

### 阶段 3：扩展 SafeTaskMgr（1 小时）

**目标**：添加便捷方法

**步骤**：

1. 打开 `src/safe_task_mgr.h`

2. 在 `SafeTaskMgr` 类中添加 public 方法（参考 `safe_task_mgr_extensions.h`）：

```cpp
class SafeTaskMgr {
public:
    // ... 现有接口 ...

    // 新增：便捷方法
    bool update_progress(int capture_id, unsigned long packets, unsigned long bytes);
    bool set_capture_started(int capture_id, int64_t start_ts, pid_t capture_pid, const std::string& output_file);
    bool set_capture_finished(int capture_id, int64_t finish_ts, unsigned long packets, unsigned long bytes);
    bool set_capture_failed(int capture_id, const std::string& error_msg);
};
```

3. 在 `src/safe_task_mgr.cpp` 中实现这些方法（如果有 .cpp 文件）

**提示**：可以直接使用现有的 `update_task()` 模板方法实现

**验证**：
- [ ] 编译通过
- [ ] 接口清晰易用

---

### 阶段 4：扩展 SRxCaptureTask（可选）

**目标**：为任务结构添加新字段

**步骤**：

1. 打开 `src/capture_task_types.h`

2. 在 `SRxCaptureTask` 中添加字段：

```cpp
struct SRxCaptureTask {
    // ... 现有字段 ...

    // 新增：错误码
    int error_code;                      // ECaptureErrorCode

    // 新增：产出文件列表
    std::vector<std::string> result_files;

    // 新增：最后更新时间
    int64_t last_update_ts;

    // 构造函数中初始化新字段
    SRxCaptureTask() : ..., error_code(0), last_update_ts(0) {}
};
```

**验证**：
- [ ] 编译通过
- [ ] 构造函数初始化新字段

---

### 阶段 5：实现 Manager 消息处理（4 小时）

**目标**：在 `CRxCaptureManagerThread` 中处理新消息

**步骤**：

1. 打开 `src/rxcapturemanagerthread.cpp`

2. 在 `handle_msg()` 中添加新消息分发：

```cpp
void CRxCaptureManagerThread::handle_msg(std::tr1::shared_ptr<normal_msg>& msg)
{
    switch (msg->_msg_op) {
        // ... 现有消息 ...

        // 新增：Worker -> Manager
        case RX_MSG_CAPTURE_PROGRESS:
            handle_capture_progress(msg);
            break;
        case RX_MSG_CAPTURE_FILE_READY:
            handle_capture_file_ready(msg);
            break;

        // 新增：Clean -> Manager
        case RX_MSG_CLEAN_STORED:
            handle_clean_stored(msg);
            break;
        case RX_MSG_CLEAN_COMPRESS_DONE:
            handle_clean_compress_done(msg);
            break;
        case RX_MSG_CLEAN_COMPRESS_FAILED:
            handle_clean_compress_failed(msg);
            break;

        default:
            LOG_WARN("Unknown message type: %d", msg->_msg_op);
            break;
    }
}
```

3. 实现各个 `handle_*()` 方法（参考 `capture_messages_design.md` 中的示例）

4. 在 `run_process()` 末尾调用 `cleanup_pending_deletes()`：

```cpp
void CRxCaptureManagerThread::run_process()
{
    while (!_stop) {
        // 处理消息
        handle_messages();

        // 处理定时器
        handle_timers();

        //  安全点：清理待释放对象
        proc_data::instance()->capture_task_mgr().cleanup_pending_deletes();

        // 睡眠
        usleep(10000);  // 10ms
    }
}
```

**验证**：
- [ ] 编译通过
- [ ] 消息处理逻辑正确
- [ ] 日志输出清晰

---

### 阶段 6：实现 Worker 消息发送（2 小时）

**目标**：在 `CRxCaptureThread` 中发送新消息

**步骤**：

1. 打开 Worker 线程实现文件

2. 在合适的时机发送消息：

```cpp
// 示例：发送进度消息
void CRxCaptureThread::report_progress()
{
    std::tr1::shared_ptr<SRxCaptureProgressMsg> msg(new SRxCaptureProgressMsg());
    msg->capture_id = _current_capture_id;
    msg->key = _current_key;
    msg->stats.packets = _current_packets;
    msg->stats.bytes = _current_bytes;
    msg->ts_usec = get_current_usec();

    // 发送给 Manager 线程
    _manager_thread->send_msg(msg);
}

// 示例：发送文件就绪消息
void CRxCaptureThread::notify_file_ready(const std::string& file_path, size_t file_size)
{
    std::tr1::shared_ptr<SRxCaptureFileReadyMsg> msg(new SRxCaptureFileReadyMsg());
    msg->capture_id = _current_capture_id;
    msg->key = _current_key;

    CaptureFileInfo file_info;
    file_info.file_path = file_path;
    file_info.file_size = file_size;
    file_info.segment_index = _current_segment;
    file_info.created_ts = get_current_usec();

    msg->files.push_back(file_info);
    msg->ts_usec = get_current_usec();

    _manager_thread->send_msg(msg);
}
```

3. 控制进度上报频率：

```cpp
// 每 2 秒或每 10000 包上报一次
void CRxCaptureThread::check_and_report_progress()
{
    int64_t now = get_current_usec();
    if (now - _last_progress_ts >= 2000000 ||  // 2 秒
        _current_packets - _last_progress_packets >= 10000) {  // 10000 包
        report_progress();
        _last_progress_ts = now;
        _last_progress_packets = _current_packets;
    }
}
```

**验证**：
- [ ] 编译通过
- [ ] 消息发送正确
- [ ] 进度上报频率合理

---

### 阶段 7：实现 Clean 线程（3 小时）

**目标**：实现文件清理和压缩逻辑

**步骤**：

1. 创建 `CRxCleanupThread` 类（如果还没有）

2. 实现消息处理：
   - 接收 `RX_MSG_FILE_ENQUEUE` 消息
   - 记录文件到队列
   - 按策略触发压缩
   - 发送结果给 Manager

3. 实现压缩逻辑：

```cpp
void CRxCleanupThread::compress_files()
{
    // 检查是否达到压缩阈值
    if (_pending_files.size() < _config.compress_threshold_mb) {
        return;
    }

    // 执行压缩
    std::string archive_path = create_archive(_pending_files);

    // 发送成功消息
    std::tr1::shared_ptr<SRxCleanCompressDoneMsg> msg(new SRxCleanCompressDoneMsg());
    msg->archive_path = archive_path;
    msg->compressed_files = _pending_files;
    msg->compressed_bytes = get_file_size(archive_path);
    msg->duration_ms = _compress_duration_ms;
    msg->ts_usec = get_current_usec();

    _manager_thread->send_msg(msg);
}
```

**验证**：
- [ ] 编译通过
- [ ] 文件正确入队
- [ ] 压缩逻辑正确
- [ ] 消息发送正确

---

### 阶段 8：配置管理（1 小时）

**目标**：实现配置快照生成

**步骤**：

1. 在 `proc_data.h` 中添加方法：

```cpp
class proc_data {
public:
    // ... 现有方法 ...

    // 新增：获取配置快照
    CaptureConfigSnapshot get_capture_config_snapshot();
};
```

2. 在 `proc_data.cpp` 中实现：

```cpp
CaptureConfigSnapshot proc_data::get_capture_config_snapshot()
{
    CaptureConfigSnapshot config;

    // 从配置文件读取
    CRxStrategyConfigManager* strategy = _strategy_dict->cur();
    if (strategy) {
        config.output_dir = strategy->get_output_dir();
        config.filename_template = strategy->get_filename_template();
        // ... 填充其他字段 ...
    }

    // 计算哈希
    config.config_hash = calc_config_hash(config);
    config.config_timestamp = get_current_usec();

    return config;
}
```

3. 在 `handle_start_capture()` 中使用配置快照：

```cpp
void CRxCaptureManagerThread::handle_start_capture(std::tr1::shared_ptr<normal_msg>& msg)
{
    // 获取配置快照
    CaptureConfigSnapshot config = proc_data::instance()->get_capture_config_snapshot();

    // 填充到 START 消息
    std::tr1::shared_ptr<SRxStartCaptureMsg> start_msg = ...;
    start_msg->config = config;

    // 发送给 Worker
    send_to_worker(start_msg);
}
```

**验证**：
- [ ] 编译通过
- [ ] 配置快照正确生成
- [ ] 哈希计算正确

---

##  验证检查清单

### 编译检查

- [ ] 所有文件编译通过
- [ ] 无警告（或只有合理的警告）
- [ ] 链接成功

### 功能测试

- [ ] 启动抓包任务
- [ ] 收到 STARTED 消息，状态更新为 RUNNING
- [ ] 定期收到 PROGRESS 消息
- [ ] 收到 FILE_READY 消息
- [ ] 收到 FINISHED 消息，状态更新为 COMPLETED
- [ ] 文件被正确发送给 Clean 线程
- [ ] 文件压缩成功

### 错误处理测试

- [ ] 启动失败场景（网卡不存在、权限不足等）
- [ ] 运行失败场景（tcpdump 崩溃、磁盘满等）
- [ ] 正确收到 FAILED 消息
- [ ] 错误码和错误消息正确

### 性能测试

- [ ] 进度上报频率合理（不会过于频繁）
- [ ] cleanup_pending_deletes() 执行时间短（<1ms）
- [ ] 内存无泄漏
- [ ] CPU 占用合理

### 并发测试

- [ ] 多个抓包任务同时运行
- [ ] SafeTaskMgr 查询正确
- [ ] 无数据竞争（使用 valgrind --tool=helgrind 检测）

---

## 📊 消息时序验证

使用以下命令抓取日志，验证消息时序：

```bash
# 启动抓包任务，观察日志
tail -f /var/log/rxtracenetcap/app.log | grep -E "MSG_(START|STARTED|PROGRESS|FILE_READY|FINISHED)"
```

**预期输出**：

```
[INFO] Received RX_MSG_START_CAPTURE, capture_id=1001
[INFO] Sent RX_MSG_CAPTURE_START to worker 0
[INFO] Received RX_MSG_CAPTURE_STARTED, capture_id=1001, pid=12345
[DEBUG] Received RX_MSG_CAPTURE_PROGRESS, capture_id=1001, packets=1000
[DEBUG] Received RX_MSG_CAPTURE_PROGRESS, capture_id=1001, packets=5000
[DEBUG] Received RX_MSG_CAPTURE_FILE_READY, capture_id=1001, file=/data/capture_1001_0.pcap
[INFO] Received RX_MSG_CAPTURE_FINISHED, capture_id=1001, packets=10000
[INFO] Sent RX_MSG_FILE_ENQUEUE to clean thread
```

---

## 🐛 常见问题

### Q1: 编译错误：`error: 'CaptureStats' does not name a type`

**原因**：头文件包含顺序错误

**解决**：确保 `rxcapturemessages.h` 在 `safe_task_mgr.h` 之前包含

---

### Q2: 运行时崩溃：`Segmentation fault in update_task()`

**原因**：多线程同时调用 update_task()

**解决**：确保只有 Manager 线程调用写接口

---

### Q3: 内存泄漏：pending_deletes 越来越大

**原因**：忘记调用 `cleanup_pending_deletes()`

**解决**：在 `run_process()` 末尾调用清理方法

---

### Q4: 进度消息过于频繁，CPU 占用高

**原因**：进度上报间隔过短

**解决**：调整上报策略，改为 2 秒或 10000 包

---

## 📞 后续支持

如有问题，请查看：

1. **设计文档**：`docs/capture_messages_design.md`
2. **扩展头文件**：`src/rxcapturemessages.h`
3. **示例代码**：`docs/safe_task_mgr_extensions.h`

祝实施顺利！🎉
