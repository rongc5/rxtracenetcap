# 抓包系统消息架构实施指南

## 概览

本指南说明如何整合新的消息结构（`rxcapturemessages.h`）和配置系统，实现完整的多线程抓包架构。

---

## 1. 配置文件设置

### 1.1 在 `strategy.ini` 中添加进度上报配置

```ini
[storage]
# 进度上报间隔（秒），默认 10 秒
progress_interval_sec = 10

# 或者每捕获 N 个包后上报（默认 50000）
progress_packet_threshold = 50000

# 或者每捕获 N MB 后上报（默认 100MB）
progress_bytes_threshold_mb = 100

# 压缩批处理间隔（秒），默认 300 秒
compress_batch_interval_sec = 300
```

### 1.2 读取配置

在代码中使用以下方式读取配置：

```cpp
#include "rxstrategyconfig.h"

CRxStrategyConfigManager& config_mgr = CRxStrategyConfigManager::getInstance();

// 获取进度上报配置
int progress_interval = config_mgr.get_progress_interval_sec();           // 默认 10
int packet_threshold = config_mgr.get_progress_packet_threshold();        // 默认 50000
long bytes_threshold = config_mgr.get_progress_bytes_threshold();         // 默认 100MB
```

---

## 2. 消息结构使用

### 2.1 包含头文件

```cpp
#include "rxcapturemessages.h"
```

### 2.2 Manager → Worker 消息

#### 启动抓包任务

```cpp
SRxCaptureStartMsg start_msg;
start_msg.capture_id = 123;
start_msg.key = "eth0|port 80";
start_msg.op_version = 1;
start_msg.ts_usec = get_current_time_usec();
start_msg.config_hash = compute_config_hash();

// 从配置读取并填充配置快照
CaptureConfigSnapshot snapshot;
snapshot.iface = "eth0";
snapshot.bpf_filter = "port 80";
snapshot.duration_sec = 300;
snapshot.base_dir = "/var/log/rxtrace";
snapshot.compress_enabled = true;
snapshot.progress_interval_sec = 10;
snapshot.progress_packet_threshold = 50000;

start_msg.config = snapshot;
start_msg.worker_id = 0;  // 目标工作线程 ID

// 发送消息
worker_thread->add_msg(&start_msg);
```

### 2.3 Worker → Manager 消息

#### 上报进度

```cpp
SRxCaptureProgressMsg progress_msg;
progress_msg.capture_id = 123;
progress_msg.key = "eth0|port 80";
progress_msg.ts_usec = get_current_time_usec();

progress_msg.progress.packets = 5000;
progress_msg.progress.bytes = 2048000;
progress_msg.progress.first_packet_ts_usec = start_ts;
progress_msg.progress.last_packet_ts_usec = get_current_time_usec();
progress_msg.progress.file_size = 2048000;
progress_msg.progress.cpu_sec = 0.5;

progress_msg.report_ts_usec = get_current_time_usec();

// 发送给 Manager
manager_thread->add_msg(&progress_msg);
```

#### 文件就绪（批量）

```cpp
SRxCaptureFileReadyMsg file_msg;
file_msg.capture_id = 123;
file_msg.key = "eth0|port 80";

// 支持多个文件
CaptureFileInfo file1;
file1.file_path = "/var/log/rxtrace/2025-01-16/eth0-001.pcap";
file1.file_size = 1024000;
file1.segment_index = 1;
file1.total_segments = 3;
file1.file_ready_ts_usec = get_current_time_usec();

file_msg.files.push_back(file1);

// 可继续添加更多文件，最多 100 个
file_msg.ready_ts_usec = get_current_time_usec();

manager_thread->add_msg(&file_msg);
```

#### 抓包完成

```cpp
SRxCaptureFinishedMsg finished_msg;
finished_msg.capture_id = 123;
finished_msg.key = "eth0|port 80";

finished_msg.result.total_packets = 50000;
finished_msg.result.total_bytes = 20480000;
finished_msg.result.start_ts_usec = start_ts;
finished_msg.result.finish_ts_usec = get_current_time_usec();
finished_msg.result.exit_code = 0;  // 0 表示成功

manager_thread->add_msg(&finished_msg);
```

#### 抓包失败

```cpp
SRxCaptureFailedMsg failed_msg;
failed_msg.capture_id = 123;
failed_msg.key = "eth0|port 80";

failed_msg.error_code = ERR_RUN_TCPDUMP_DIED;  // 使用枚举值
failed_msg.error_msg = "tcpdump process died unexpectedly";
failed_msg.last_progress.packets = 5000;  // 最后已知的进度

manager_thread->add_msg(&failed_msg);
```

---

## 3. Manager → Clean 消息

### 3.1 文件排队处理

```cpp
SRxFileEnqueueMsg enqueue_msg;
enqueue_msg.capture_id = 123;
enqueue_msg.file_path = "/var/log/rxtrace/2025-01-16/eth0-001.pcap";
enqueue_msg.file_size = 1024000;
enqueue_msg.capture_finish_ts_usec = finish_ts;

// 在此携带压缩/清理策略
enqueue_msg.clean_policy.base_dir = config.storage().base_dir;
enqueue_msg.clean_policy.compress_enabled = config.storage().compress_enabled;
enqueue_msg.clean_policy.compress_cmd = config.storage().compress_cmd;
enqueue_msg.clean_policy.compress_batch_interval_sec = config.storage().compress_batch_interval_sec;

clean_thread->add_msg(&enqueue_msg);
```

---

## 4. Clean → Manager 消息

### 4.1 压缩完成

```cpp
SRxCleanCompressDoneMsg done_msg;
done_msg.capture_id = 123;

done_msg.compressed_files.push_back("/var/log/rxtrace/archive/eth0-001.pcap.gz");
done_msg.compressed_bytes = 512000;  // 压缩后大小
done_msg.duration_ms = 1500;  // 耗时 1.5 秒

manager_thread->add_msg(&done_msg);
```

---

## 5. 错误码参考

| 错误码 | 含义 | 处理方式 |
|--------|------|---------|
| ERR_START_INVALID_PARAMS | 启动参数无效 | 检查用户输入 |
| ERR_START_IFACE_NOT_FOUND | 网卡不存在 | 列出可用网卡并重试 |
| ERR_START_BPF_COMPILE_FAILED | BPF 编译失败 | 检查 BPF 语法 |
| ERR_RUN_TCPDUMP_DIED | 抓包进程崩溃 | 查看系统日志并重试 |
| ERR_RUN_OUTPUT_WRITE_FAILED | 文件写入失败 | 检查磁盘空间 |
| ERR_CLEAN_COMPRESS_FAILED | 压缩失败 | 检查磁盘空间和权限 |
| ERR_CLEAN_DISK_FULL | 磁盘满 | 清理过期文件 |

---

## 6. 时间戳使用

所有消息使用 **微秒（usec）** 作为时间单位：

```cpp
#include <sys/time.h>

int64_t get_current_time_usec() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000000 + tv.tv_usec;
}
```

---

## 7. 幂等性检查

处理消息时，使用以下组合检查幂等性：

```cpp
struct MessageKey {
    int capture_id;
    std::string key;
    int op_version;
    
    bool operator<(const MessageKey& other) const {
        if (capture_id != other.capture_id) return capture_id < other.capture_id;
        if (key != other.key) return key < other.key;
        return op_version < other.op_version;
    }
};

// 使用 std::set 去重
std::set<MessageKey> processed_messages;

void process_message(const SRxCaptureProgressMsg& msg) {
    MessageKey mk = {msg.capture_id, msg.key, msg.op_version};
    if (processed_messages.count(mk)) {
        LOG_DEBUG_MSG("Duplicate message, ignoring");
        return;
    }
    processed_messages.insert(mk);
    
    // 处理消息...
}
```

---

## 8. 配置快照哈希

计算配置哈希用于检测配置变更：

```cpp
#include <cstring>

uint32_t compute_config_hash(const CaptureConfigSnapshot& snapshot) {
    uint32_t hash = 5381;
    
    const char* fields[] = {
        snapshot.iface.c_str(),
        snapshot.bpf_filter.c_str(),
        snapshot.base_dir.c_str()
    };
    
    for (int i = 0; i < 3; ++i) {
        const char* field = fields[i];
        for (size_t j = 0; field && j < std::strlen(field); ++j) {
            hash = ((hash << 5) + hash) + field[j];
        }
    }
    
    return hash;
}
```

---

## 9. 消息处理示例

### Manager 接收来自 Worker 的进度消息

```cpp
class CRxCaptureManagerV3 : public CRxBaseNetThread {
protected:
    virtual void handle_msg(CRxMsg* ct) {
        if (!ct) return;
        
        if (ct->op == RX_MSG_CAPTURE_PROGRESS) {
            SRxCaptureProgressMsg* msg = dynamic_cast<SRxCaptureProgressMsg*>(ct);
            if (msg) handle_capture_progress(msg);
        }
        else if (ct->op == RX_MSG_CAPTURE_FINISHED) {
            SRxCaptureFinishedMsg* msg = dynamic_cast<SRxCaptureFinishedMsg*>(ct);
            if (msg) handle_capture_finished(msg);
        }
        else if (ct->op == RX_MSG_CAPTURE_FAILED) {
            SRxCaptureFailedMsg* msg = dynamic_cast<SRxCaptureFailedMsg*>(ct);
            if (msg) handle_capture_failed(msg);
        }
    }
    
private:
    void handle_capture_progress(SRxCaptureProgressMsg* msg) {
        // 更新 SafeTaskMgr
        task_mgr_.update_progress(msg->capture_id, msg->progress);
        
        // 可选：转发给 HTTP 线程
        if (http_peer_id_) {
            // ... 发送通知消息
        }
    }
    
    void handle_capture_finished(SRxCaptureFinishedMsg* msg) {
        // 更新任务状态
        task_mgr_.set_capture_finished(msg->capture_id, msg->result.finish_ts_usec, msg->result);
        
        // 触发清理
        for (size_t i = 0; i < msg->result.output_files.size(); ++i) {
            enqueue_file_for_cleanup(msg->capture_id, msg->result.output_files[i]);
        }
    }
};
```

---

## 10. 验证检查清单

在系统上线前检查以下项目：

- [ ] 配置文件中添加了进度上报配置
- [ ] Manager 正确读取配置并填充 CaptureConfigSnapshot
- [ ] Worker 按照阈值或时间间隔上报进度
- [ ] Manager 正确接收和处理所有消息类型
- [ ] Clean 线程正确处理文件排队和压缩完成消息
- [ ] 所有时间戳使用微秒（int64_t）
- [ ] 配置哈希在配置变更时更新
- [ ] 错误消息包含详细的错误码和文本
- [ ] 消息队列不溢出（监控队列深度）
- [ ] 日志输出清晰（便于问题排查）

---

## 11. 常见问题

### Q: 进度上报间隔太频繁会怎样？

A: 消息队列可能溢出，导致性能下降。建议最小间隔为 1 秒。

### Q: 如何动态修改进度上报间隔？

A: 修改 `strategy.ini` 中的 `progress_interval_sec`，Manager 会自动重新加载配置（通过 `CConfigReloader`）。

### Q: 文件分段时如何处理？

A: 使用 `SRxCaptureFileReadyMsg` 的 `segment_index` 和 `total_segments` 字段。Clean 线程收到所有分段后再触发压缩。

### Q: 如何确保消息不丢失？

A: 使用 `capture_id + key + op_version` 组合检查幂等性。Manager 接收到重复消息时忽略即可。

---
