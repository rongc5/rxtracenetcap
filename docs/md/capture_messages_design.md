# 抓包系统消息设计文档

## 1. 设计原则

- **单写线程模型**：所有状态更新由 Manager 线程执行
- **消息统一承载**：继承 `normal_msg`，使用 `_msg_op` 区分类型
- **配置快照机制**：消息携带配置副本，避免运行时读取配置
- **时间戳统一**：使用 `int64_t ts_usec`（微秒）
- **路径绝对化**：所有文件路径使用绝对路径
- **幂等性保证**：capture_id + key + op_version 组合保证幂等

## 2. 消息枚举扩展

基于现有的 `ERxCaptureMsg`，建议添加以下消息类型：

```cpp
// 在 rx_msg_types.h 中扩展
enum ERxCaptureMsg_Extended {
    // ===== Manager -> Worker =====
    RX_MSG_CAPTURE_START = 2100,         // 启动抓包（现有：2001）
    RX_MSG_CAPTURE_STOP = 2101,          // 停止抓包（现有：2002）
    RX_MSG_CAPTURE_CANCEL = 2102,        // 取消抓包（新增）
    RX_MSG_CAPTURE_CONFIG_REFRESH = 2103,// 配置刷新（新增）

    // ===== Worker -> Manager =====
    RX_MSG_CAPTURE_STARTED = 2110,       // 抓包已启动（新增）
    RX_MSG_CAPTURE_PROGRESS = 2111,      // 抓包进度（新增）
    RX_MSG_CAPTURE_FILE_READY = 2112,    // 文件就绪（新增）
    RX_MSG_CAPTURE_FINISHED = 2113,      // 抓包完成（现有：10101）
    RX_MSG_CAPTURE_FAILED = 2114,        // 抓包失败（新增）
    RX_MSG_CAPTURE_HEARTBEAT = 2115,     // 心跳（可选）

    // ===== Manager -> Clean =====
    RX_MSG_FILE_ENQUEUE = 2120,          // 文件入队（新增）
    RX_MSG_CLEAN_CFG_REFRESH = 2121,     // 清理配置刷新（新增）
    RX_MSG_CLEAN_SHUTDOWN = 2122,        // 清理线程关闭（新增）

    // ===== Clean -> Manager =====
    RX_MSG_CLEAN_STORED = 2130,          // 文件记录完成（新增）
    RX_MSG_CLEAN_COMPRESS_DONE = 2131,   // 压缩完成（新增）
    RX_MSG_CLEAN_COMPRESS_FAILED = 2132, // 压缩失败（新增）
    RX_MSG_CLEAN_HEARTBEAT = 2133,       // 清理心跳（新增）

    // ===== Manager -> SAM（可选）=====
    RX_MSG_SAME_EVENT = 2140             // SAM 事件通知（新增）
};
```

## 3. 核心数据结构

### 3.1 错误码定义

```cpp
enum ECaptureErrorCode {
    ERR_NONE = 0,
    ERR_UNKNOWN = 1,

    // 启动失败 (100-199)
    ERR_START_INVALID_PARAMS = 100,      // 无效参数
    ERR_START_NO_PERMISSION = 101,       // 权限不足
    ERR_START_INTERFACE_NOT_FOUND = 102, // 网卡不存在
    ERR_START_PROCESS_NOT_FOUND = 103,   // 进程不存在
    ERR_START_TCPDUMP_FAILED = 104,      // tcpdump 启动失败
    ERR_START_CREATE_FILE_FAILED = 105,  // 创建输出文件失败

    // 运行失败 (200-299)
    ERR_RUN_TCPDUMP_DIED = 200,          // tcpdump 意外退出
    ERR_RUN_DISK_FULL = 201,             // 磁盘满
    ERR_RUN_TIMEOUT = 202,               // 超时
    ERR_RUN_CANCELLED = 203,             // 用户取消
    ERR_RUN_PROCESS_DIED = 204,          // 目标进程退出

    // 清理失败 (300-399)
    ERR_CLEAN_COMPRESS_FAILED = 300,     // 压缩失败
    ERR_CLEAN_DELETE_FAILED = 301,       // 删除失败
    ERR_CLEAN_DISK_FULL = 302            // 磁盘满
};
```

### 3.2 配置快照结构

```cpp
struct CaptureConfigSnapshot {
    // 输出配置
    std::string output_dir;              // 输出目录
    std::string filename_template;       // 文件名模板
    int file_rotate_size_mb;             // 文件轮转大小（MB）
    int file_rotate_count;               // 文件轮转数量

    // 资源限制
    int max_duration_sec;                // 最大时长（秒）
    long max_bytes;                      // 最大字节数
    int max_packets;                     // 最大包数
    int snaplen;                         // 抓包截断长度

    // 压缩策略
    bool compress_enabled;               // 是否启用压缩
    int compress_threshold_mb;           // 压缩阈值（MB）
    std::string compress_format;         // 压缩格式（tar.gz/zip）
    int compress_level;                  // 压缩级别（1-9）

    // 清理策略
    int retain_days;                     // 保留天数
    int clean_batch_size;                // 批量清理数量
    long disk_threshold_gb;              // 磁盘阈值（GB）

    // 配置版本
    uint32_t config_hash;                // 配置哈希
    int64_t config_timestamp;            // 配置时间戳
};
```

### 3.3 抓包规格结构

```cpp
struct CaptureSpec {
    // 抓包目标
    ECaptureMode capture_mode;
    std::string iface;                   // 网卡名
    std::string proc_name;               // 进程名
    pid_t target_pid;                    // 目标 PID
    std::string container_id;            // 容器 ID
    std::string netns_path;              // 网络命名空间

    // 过滤条件
    std::string filter;                  // BPF 过滤器
    std::string protocol_filter;         // 协议过滤
    std::string ip_filter;               // IP 过滤
    int port_filter;                     // 端口过滤

    // 输出配置
    std::string output_file;             // 输出文件路径（完整路径）
    std::string output_pattern;          // 输出模式（用于轮转）

    // 资源限制
    int max_duration_sec;
    long max_bytes;
    int max_packets;
    int snaplen;
};
```

### 3.4 统计信息结构

```cpp
struct CaptureStats {
    unsigned long packets;               // 包数
    unsigned long bytes;                 // 字节数
    int64_t first_packet_ts;             // 首包时间戳（微秒）
    int64_t last_packet_ts;              // 末包时间戳（微秒）
    unsigned long file_size;             // 当前文件大小
    double cpu_seconds;                  // CPU 时间（秒）
};
```

### 3.5 文件信息结构

```cpp
struct CaptureFileInfo {
    std::string file_path;               // 文件绝对路径
    unsigned long file_size;             // 文件大小（字节）
    int segment_index;                   // 分段索引（从 0 开始）
    int64_t created_ts;                  // 创建时间戳（微秒）
    std::string md5;                     // MD5（可选）
};
```

## 4. 消息定义

### 4.1 Manager -> Worker 消息

#### 4.1.1 启动抓包（扩展现有 SRxStartCaptureMsg）

```cpp
struct SRxCaptureStartMsg : public normal_msg {
    // 任务标识
    int capture_id;
    std::string key;
    int op_version;                      // 操作版本（幂等）

    // 抓包规格
    CaptureSpec spec;

    // 配置快照
    CaptureConfigSnapshot config;

    // 时间戳
    int64_t ts_usec;

    SRxCaptureStartMsg()
        : normal_msg(RX_MSG_CAPTURE_START)
        , capture_id(-1)
        , op_version(0)
        , ts_usec(0)
    {}
};
```

#### 4.1.2 停止抓包

```cpp
struct SRxCaptureStopMsg : public normal_msg {
    int capture_id;
    std::string key;
    std::string stop_reason;
    int64_t ts_usec;

    SRxCaptureStopMsg()
        : normal_msg(RX_MSG_CAPTURE_STOP)
        , capture_id(-1)
        , ts_usec(0)
    {}
};
```

#### 4.1.3 取消抓包

```cpp
struct SRxCaptureCancelMsg : public normal_msg {
    int capture_id;
    std::string key;
    std::string cancel_reason;
    int64_t ts_usec;

    SRxCaptureCancelMsg()
        : normal_msg(RX_MSG_CAPTURE_CANCEL)
        , capture_id(-1)
        , ts_usec(0)
    {}
};
```

### 4.2 Worker -> Manager 消息

#### 4.2.1 抓包已启动

```cpp
struct SRxCaptureStartedMsg : public normal_msg {
    int capture_id;
    std::string key;
    int op_version;

    int worker_id;                       // Worker 线程 ID
    pid_t capture_pid;                   // tcpdump 进程 PID
    int64_t start_ts_usec;

    uint32_t applied_config_hash;        // 应用的配置哈希
    std::string output_file;             // 实际输出文件路径

    SRxCaptureStartedMsg()
        : normal_msg(RX_MSG_CAPTURE_STARTED)
        , capture_id(-1)
        , op_version(0)
        , worker_id(-1)
        , capture_pid(-1)
        , start_ts_usec(0)
        , applied_config_hash(0)
    {}
};
```

#### 4.2.2 抓包进度

```cpp
struct SRxCaptureProgressMsg : public normal_msg {
    int capture_id;
    std::string key;

    CaptureStats stats;
    int64_t ts_usec;

    SRxCaptureProgressMsg()
        : normal_msg(RX_MSG_CAPTURE_PROGRESS)
        , capture_id(-1)
        , ts_usec(0)
    {}
};
```

**进度上报策略：**
- 时间间隔：≥ 1 秒（建议 2 秒）
- 或包数阈值：每 10,000 包
- 或字节阈值：每 100 MB

#### 4.2.3 文件就绪

```cpp
struct SRxCaptureFileReadyMsg : public normal_msg {
    int capture_id;
    std::string key;

    std::vector<CaptureFileInfo> files;  // 支持批量通知
    int total_segments;                  // 总分段数（如果已知）

    int64_t ts_usec;

    SRxCaptureFileReadyMsg()
        : normal_msg(RX_MSG_CAPTURE_FILE_READY)
        , capture_id(-1)
        , total_segments(0)
        , ts_usec(0)
    {}
};
```

#### 4.2.4 抓包完成

```cpp
struct SRxCaptureFinishedMsg : public normal_msg {
    int capture_id;
    std::string key;

    int64_t finish_ts_usec;
    CaptureStats final_stats;

    int exit_code;
    std::string finish_reason;

    std::vector<std::string> result_files;  // 产出文件列表

    int64_t ts_usec;

    SRxCaptureFinishedMsg()
        : normal_msg(RX_MSG_CAPTURE_FINISHED)
        , capture_id(-1)
        , finish_ts_usec(0)
        , exit_code(0)
        , ts_usec(0)
    {}
};
```

#### 4.2.5 抓包失败

```cpp
struct SRxCaptureFailedMsg : public normal_msg {
    int capture_id;
    std::string key;

    ECaptureErrorCode error_code;
    std::string error_msg;

    CaptureStats last_stats;             // 失败前的最后统计
    int64_t failed_ts_usec;

    int64_t ts_usec;

    SRxCaptureFailedMsg()
        : normal_msg(RX_MSG_CAPTURE_FAILED)
        , capture_id(-1)
        , error_code(ERR_UNKNOWN)
        , failed_ts_usec(0)
        , ts_usec(0)
    {}
};
```

### 4.3 Manager -> Clean 消息

#### 4.3.1 文件入队

```cpp
struct SRxFileEnqueueMsg : public normal_msg {
    int capture_id;
    std::string key;

    std::string file_path;
    unsigned long file_size;
    int64_t capture_finish_ts;

    // 压缩策略（从配置快照复制）
    bool compress_enabled;
    int compress_threshold_mb;
    std::string compress_format;
    int compress_level;

    int64_t ts_usec;

    SRxFileEnqueueMsg()
        : normal_msg(RX_MSG_FILE_ENQUEUE)
        , capture_id(-1)
        , file_size(0)
        , capture_finish_ts(0)
        , compress_enabled(true)
        , compress_threshold_mb(100)
        , compress_format("tar.gz")
        , compress_level(6)
        , ts_usec(0)
    {}
};
```

#### 4.3.2 清理配置刷新

```cpp
struct SRxCleanConfigRefreshMsg : public normal_msg {
    int retain_days;
    int clean_batch_size;
    long disk_threshold_gb;

    bool compress_enabled;
    int compress_threshold_mb;
    std::string compress_format;
    int compress_level;

    int64_t ts_usec;

    SRxCleanConfigRefreshMsg()
        : normal_msg(RX_MSG_CLEAN_CFG_REFRESH)
        , retain_days(7)
        , clean_batch_size(100)
        , disk_threshold_gb(10)
        , compress_enabled(true)
        , compress_threshold_mb(100)
        , compress_format("tar.gz")
        , compress_level(6)
        , ts_usec(0)
    {}
};
```

### 4.4 Clean -> Manager 消息

#### 4.4.1 文件记录完成

```cpp
struct SRxCleanStoredMsg : public normal_msg {
    std::string file_path;
    bool success;

    size_t pending_count;
    unsigned long pending_bytes;

    int64_t ts_usec;

    SRxCleanStoredMsg()
        : normal_msg(RX_MSG_CLEAN_STORED)
        , success(false)
        , pending_count(0)
        , pending_bytes(0)
        , ts_usec(0)
    {}
};
```

#### 4.4.2 压缩完成

```cpp
struct SRxCleanCompressDoneMsg : public normal_msg {
    std::string archive_path;
    std::vector<std::string> compressed_files;
    unsigned long compressed_bytes;
    int duration_ms;

    int64_t ts_usec;

    SRxCleanCompressDoneMsg()
        : normal_msg(RX_MSG_CLEAN_COMPRESS_DONE)
        , compressed_bytes(0)
        , duration_ms(0)
        , ts_usec(0)
    {}
};
```

#### 4.4.3 压缩失败

```cpp
struct SRxCleanCompressFailedMsg : public normal_msg {
    ECaptureErrorCode error_code;
    std::string error_msg;

    std::vector<std::string> failed_files;

    int64_t ts_usec;

    SRxCleanCompressFailedMsg()
        : normal_msg(RX_MSG_CLEAN_COMPRESS_FAILED)
        , error_code(ERR_CLEAN_COMPRESS_FAILED)
        , ts_usec(0)
    {}
};
```

## 5. SafeTaskMgr 扩展接口

建议在 `safe_task_mgr.h` 中添加以下便捷方法：

```cpp
class SafeTaskMgr {
public:
    // ... 现有接口 ...

    /**
     * 更新进度（便捷方法）
     */
    bool update_progress(int capture_id, const CaptureStats& stats)
    {
        return update_task(capture_id, [&](SRxCaptureTask& task) {
            task.packet_count = stats.packets;
            task.bytes_captured = stats.bytes;
            // 不改变状态
        });
    }

    /**
     * 设置抓包已启动
     */
    bool set_capture_started(int capture_id, int64_t start_ts, pid_t capture_pid, const std::string& output_file)
    {
        return update_task(capture_id, [&](SRxCaptureTask& task) {
            task.status = STATUS_RUNNING;
            task.start_time = start_ts / 1000000;  // 转换为秒
            task.capture_pid = capture_pid;
            task.output_file = output_file;
        });
    }

    /**
     * 设置抓包已完成
     */
    bool set_capture_finished(int capture_id, int64_t finish_ts, const CaptureStats& final_stats)
    {
        return update_task(capture_id, [&](SRxCaptureTask& task) {
            task.status = STATUS_COMPLETED;
            task.end_time = finish_ts / 1000000;
            task.packet_count = final_stats.packets;
            task.bytes_captured = final_stats.bytes;
        });
    }

    /**
     * 设置抓包失败
     */
    bool set_capture_failed(int capture_id, ECaptureErrorCode error_code, const std::string& error_msg)
    {
        return update_task(capture_id, [&](SRxCaptureTask& task) {
            task.status = STATUS_FAILED;
            task.error_message = error_msg;
            // 可以扩展 SRxCaptureTask 添加 error_code 字段
        });
    }
};
```

## 6. 消息处理时序图

### 6.1 正常抓包流程

```
HTTP Thread     Manager Thread    Worker Thread     Clean Thread
     |                |                 |                 |
     |--START-------->|                 |                 |
     |                |--START_MSG----->|                 |
     |                |                 |                 |
     |<--HTTP_REPLY---|                 |                 |
     |                |<--STARTED-------|                 |
     |                | update_task()   |                 |
     |                |                 |                 |
     |                |<--PROGRESS------|                 |
     |                | update_progress()|                |
     |                |                 |                 |
     |                |<--FILE_READY----|                 |
     |                | update_task()   |                 |
     |                |--FILE_ENQUEUE--------------->     |
     |                |                 |                 |
     |                |<--FINISHED------|                 |
     |                | update_task()   |                 |
     |                | cleanup_pending_deletes()         |
     |                |                 |                 |
     |                |<--COMPRESS_DONE------------------ |
     |                | update_task()   |                 |
     |                |                 |                 |
```

### 6.2 失败流程

```
HTTP Thread     Manager Thread    Worker Thread     Clean Thread
     |                |                 |                 |
     |--START-------->|                 |                 |
     |                |--START_MSG----->|                 |
     |                |                 X (启动失败)      |
     |                |<--FAILED--------|                 |
     |                | update_task()   |                 |
     |                | (status=FAILED) |                 |
     |<--HTTP_REPLY---|                 |                 |
     |   (错误信息)   |                 |                 |
```

## 7. 实现建议

### 7.1 消息分发（Manager 线程）

```cpp
void CRxCaptureManagerThread::handle_msg(std::tr1::shared_ptr<normal_msg>& msg)
{
    switch (msg->_msg_op) {
        // HTTP -> Manager
        case RX_MSG_START_CAPTURE:
            handle_start_capture(msg);
            break;
        case RX_MSG_STOP_CAPTURE:
            handle_stop_capture(msg);
            break;

        // Worker -> Manager
        case RX_MSG_CAPTURE_STARTED:
            handle_capture_started(msg);
            break;
        case RX_MSG_CAPTURE_PROGRESS:
            handle_capture_progress(msg);
            break;
        case RX_MSG_CAPTURE_FILE_READY:
            handle_capture_file_ready(msg);
            break;
        case RX_MSG_CAPTURE_FINISHED:
            handle_capture_finished(msg);
            break;
        case RX_MSG_CAPTURE_FAILED:
            handle_capture_failed(msg);
            break;

        // Clean -> Manager
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
            // 未知消息
            break;
    }
}
```

### 7.2 典型处理示例

```cpp
void CRxCaptureManagerThread::handle_capture_started(std::tr1::shared_ptr<normal_msg>& msg)
{
    std::tr1::shared_ptr<SRxCaptureStartedMsg> started_msg =
        std::tr1::static_pointer_cast<SRxCaptureStartedMsg>(msg);

    // 更新 SafeTaskMgr（单写线程模型）
    proc_data::instance()->capture_task_mgr().set_capture_started(
        started_msg->capture_id,
        started_msg->start_ts_usec,
        started_msg->capture_pid,
        started_msg->output_file
    );

    // 可选：转发给 SAM 线程
    if (_same_thread) {
        std::tr1::shared_ptr<SRxSameEventMsg> event_msg(new SRxSameEventMsg());
        event_msg->event_type = SAME_EVENT_STARTED;
        event_msg->capture_id = started_msg->capture_id;
        event_msg->key = started_msg->key;
        event_msg->ts_usec = get_current_usec();
        _same_thread->send_msg(event_msg);
    }

    // 日志
    LOG_INFO("Capture %d started, pid=%d, file=%s",
        started_msg->capture_id,
        started_msg->capture_pid,
        started_msg->output_file.c_str());
}
```

### 7.3 run_process() 末尾清理

```cpp
void CRxCaptureManagerThread::run_process()
{
    while (!_stop) {
        // 处理消息
        handle_messages();

        // 处理定时器
        handle_timers();

        // 安全点：清理待释放对象
        proc_data::instance()->capture_task_mgr().cleanup_pending_deletes();
    }
}
```

## 8. 配置快照生成示例

```cpp
CaptureConfigSnapshot proc_data::get_capture_config_snapshot()
{
    CaptureConfigSnapshot config;

    // 从配置文件读取
    CRxStrategyConfigManager* strategy = _strategy_dict->cur();
    if (strategy) {
        config.output_dir = strategy->get_output_dir();
        config.filename_template = strategy->get_filename_template();
        config.file_rotate_size_mb = strategy->get_file_rotate_size_mb();
        // ... 其他字段 ...
    }

    // 计算哈希
    config.config_hash = calc_config_hash(config);
    config.config_timestamp = get_current_usec();

    return config;
}
```

## 9. 注意事项

1. **C++98 兼容性**
   - 使用 `std::tr1::shared_ptr` 而非 `std::shared_ptr`
   - 不使用 C++11 特性（auto、lambda 等）
   - 结构体构造函数使用初始化列表

2. **时间戳精度**
   - 统一使用 `int64_t ts_usec`（微秒）
   - 使用 `gettimeofday()` 获取当前时间
   - SRxCaptureTask 的 start_time/end_time 是秒，需要转换

3. **消息大小控制**
   - 文件列表不要过长，建议批量上限 100 个
   - 错误消息不要过长，建议截断到 1KB

4. **幂等性保证**
   - Worker 可能重复发送消息（网络抖动、重试等）
   - Manager 需要检查 op_version，忽略重复消息

5. **内存管理**
   - 使用 `std::tr1::shared_ptr` 管理消息生命周期
   - SafeTaskMgr 定期调用 `cleanup_pending_deletes()`

## 10. 总结

这套消息设计：

-  **完整覆盖**：所有组件间通信都有对应消息
-  **类型安全**：继承 normal_msg，编译时类型检查
-  **易于扩展**：新增消息只需添加枚举和结构体
-  **调试友好**：消息类型和字段一目了然
-  **性能优化**：批量通知、配置快照减少交互次数

建议实施步骤：

1. 先扩展 `rx_msg_types.h` 添加新枚举
2. 在 `rxcapturemessages.h` 中添加新消息结构
3. 扩展 `SafeTaskMgr` 添加便捷接口
4. 实现 Manager 线程的消息处理
5. 实现 Worker 和 Clean 线程的消息发送
6. 编写单元测试验证消息流程
