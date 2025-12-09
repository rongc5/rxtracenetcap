# 消息架构集成清单

## 完整的集成步骤

### 第一阶段：基础准备（已完成）

- [x] 创建 `src/rxcapturemessages.h` - 完整的消息定义
- [x] 扩展 `src/rxstrategyconfig.h/cpp` - 添加进度上报配置
- [x] 创建 `config/strategy.ini.example` - 配置示例
- [x] 编写文档
  - [x] `docs/CAPTURE_MESSAGES_IMPLEMENTATION.md` - 实施指南
  - [x] `docs/CAPTURE_MESSAGES_SUMMARY.md` - 工作总结
  - [x] `docs/SAFE_TASK_MGR_HELPERS.md` - 便捷方法示例

### 第二阶段：代码集成（进行中）

#### 2.1 Manager 线程 (CRxCaptureManagerV3)

**需要修改的文件**：
- `src/rxcapturemanager_v3.h`
- `src/rxcapturemanager_v3.cpp`

**具体改动**：

1. **包含新头文件**
   ```cpp
   #include "rxcapturemessages.h"
   #include "safe_task_mgr.h"
   ```

2. **添加成员变量**
   ```cpp
   private:
       SafeTaskMgr task_mgr_;
       CRxCaptureThread* worker_threads_;   // 指向工作线程
       CRxPostThread* clean_thread_;        // 指向清理线程
   ```

3. **修改 `handle_msg()` 方法**
   处理所有新的消息类型：
   - `RX_MSG_CAPTURE_STARTED` → 调用 `handle_capture_started()`
   - `RX_MSG_CAPTURE_PROGRESS` → 调用 `handle_capture_progress()`
   - `RX_MSG_CAPTURE_FILE_READY` → 调用 `handle_capture_file_ready()`
   - `RX_MSG_CAPTURE_FINISHED` → 调用 `handle_capture_finished()`
   - `RX_MSG_CAPTURE_FAILED` → 调用 `handle_capture_failed()`
   - `RX_MSG_CLEAN_COMPRESS_DONE` → 调用 `handle_clean_compress_done()`
   - `RX_MSG_CLEAN_COMPRESS_FAILED` → 调用 `handle_clean_compress_failed()`

4. **实现消息处理方法**
   参考 `docs/SAFE_TASK_MGR_HELPERS.md` 中的示例代码

5. **在消息循环末尾清理**
   ```cpp
   void run_process() override {
       // ... 处理消息队列 ...
       
       // 在安全点清理待释放的对象
       task_mgr_.cleanup_pending_deletes();
   }
   ```

#### 2.2 Worker 线程 (CRxCaptureThread)

**需要修改的文件**：
- `src/capturethread.h`
- `src/capturethread.cpp`

**具体改动**：

1. **包含新头文件**
   ```cpp
   #include "rxcapturemessages.h"
   ```

2. **修改 `run_job()` 方法**
   按以下顺序发送消息：
   - 步骤1: 发送 `RX_MSG_CAPTURE_STARTED`
   - 步骤2: 进度上报循环 (按时间或阈值发送 `RX_MSG_CAPTURE_PROGRESS`)
   - 步骤3: 发送 `RX_MSG_CAPTURE_FILE_READY` (支持批量)
   - 步骤4: 发送 `RX_MSG_CAPTURE_FINISHED` 或 `RX_MSG_CAPTURE_FAILED`

3. **参考实现**
   详见 `docs/SAFE_TASK_MGR_HELPERS.md` 中的 `CRxCaptureThread` 示例

#### 2.3 Clean 线程 (CRxPostThread)

**需要修改的文件**：
- `src/rxpostthread.h`
- `src/rxpostthread.cpp`

**具体改动**：

1. **包含新头文件**
   ```cpp
   #include "rxcapturemessages.h"
   ```

2. **修改 `handle_msg()` 方法**
   处理新消息类型：
   - `RX_MSG_FILE_ENQUEUE` → 文件入队处理
   - `RX_MSG_CLEAN_CFG_REFRESH` → 更新压缩策略

3. **实现消息处理**
   - 接收 `SRxFileEnqueueMsg`
   - 记录文件到队列
   - 按照批量策略触发压缩
   - 压缩完成后发送 `SRxCleanCompressDoneMsg` 或 `SRxCleanCompressFailedMsg`

4. **发送心跳消息（可选）**
   ```cpp
   // 定期发送状态
   SRxCleanHeartbeatMsg heartbeat;
   heartbeat.queue_length = pending_files_.size();
   heartbeat.pending_bytes = total_pending_bytes_;
   manager_thread_->add_msg(&heartbeat);
   ```

### 第三阶段：配置与测试

- [ ] 在 `strategy.ini` 中配置进度上报参数
  ```ini
  [storage]
  progress_interval_sec = 10
  progress_packet_threshold = 50000
  progress_bytes_threshold_mb = 100
  compress_batch_interval_sec = 300
  ```

- [ ] 修改系统初始化代码 (system_init_v3.cpp)
  - 确保 SafeTaskMgr 被正确初始化
  - 传递配置给各线程

- [ ] 编写单元测试
  - [ ] 消息序列化/反序列化测试
  - [ ] 进度上报频率测试
  - [ ] 错误码路径测试
  - [ ] 内存泄漏检查（valgrind）

- [ ] 集成测试
  - [ ] 端到端抓包流程测试
  - [ ] 多任务并发测试
  - [ ] 压缩完成通知测试
  - [ ] HTTP 前端通知测试

### 第四阶段：性能验证

- [ ] 监控消息队列深度
- [ ] 验证内存使用稳定性
- [ ] 测试高并发场景（10+ 同时抓包任务）
- [ ] 验证进度上报不会导致 CPU 尖峰
- [ ] 测试文件批量处理效率

---

## 文件修改检查清单

### CRxCaptureManagerV3 (Manager 线程)

```
[ ] 包含 rxcapturemessages.h
[ ] 包含 safe_task_mgr.h
[ ] 添加 SafeTaskMgr 成员变量
[ ] 添加工作线程/清理线程指针
[ ] 实现 handle_msg() 分发逻辑
[ ] 实现 handle_capture_started()
[ ] 实现 handle_capture_progress()
[ ] 实现 handle_capture_file_ready()
[ ] 实现 handle_capture_finished()
[ ] 实现 handle_capture_failed()
[ ] 实现 handle_clean_compress_done()
[ ] 实现 handle_clean_compress_failed()
[ ] 在消息循环末尾调用 cleanup_pending_deletes()
[ ] 向 HTTP 线程转发关键消息
```

### CRxCaptureThread (Worker 线程)

```
[ ] 包含 rxcapturemessages.h
[ ] 修改 run_job() 的消息发送序列
[ ] 发送 RX_MSG_CAPTURE_STARTED
[ ] 按时间/阈值发送 RX_MSG_CAPTURE_PROGRESS
[ ] 发送 RX_MSG_CAPTURE_FILE_READY (支持批量)
[ ] 发送 RX_MSG_CAPTURE_FINISHED
[ ] 处理异常情况，发送 RX_MSG_CAPTURE_FAILED
```

### CRxPostThread (Clean 线程)

```
[ ] 包含 rxcapturemessages.h
[ ] 修改 handle_msg() 处理新消息
[ ] 处理 RX_MSG_FILE_ENQUEUE
[ ] 处理 RX_MSG_CLEAN_CFG_REFRESH
[ ] 实现文件入队逻辑
[ ] 实现批量压缩触发
[ ] 发送 RX_MSG_CLEAN_COMPRESS_DONE
[ ] 发送 RX_MSG_CLEAN_COMPRESS_FAILED
[ ] 可选：发送 RX_MSG_CLEAN_HEARTBEAT
```

---

## 测试检查清单

### 基础单元测试

```
[ ] 测试消息结构大小不超过合理限制
[ ] 测试配置快照的序列化/反序列化
[ ] 测试错误码枚举完整性
[ ] 测试时间戳计算精度（微秒）
```

### 功能集成测试

```
[ ] 单个抓包任务完整流程
[ ] 多个并发抓包任务
[ ] 文件分段处理
[ ] 进度上报频率验证
[ ] 压缩完成通知流程
[ ] 异常处理：启动失败
[ ] 异常处理：运行异常
[ ] 异常处理：清理失败
```

### 性能与负载测试

```
[ ] 10 个并发抓包任务
[ ] 100 个并发抓包任务（压力测试）
[ ] 进度消息不会导致队列溢出
[ ] 内存使用在长时间运行中保持稳定
[ ] CPU 使用率在可接受范围内
```

### 线程安全验证

```
[ ] ThreadSanitizer 检查
[ ] Valgrind 内存泄漏检查
[ ] 死锁检测（长时间运行）
[ ] 竞态条件检查
```

---

## 消息流验证

验证完整的消息流：

```
1. HTTP 请求
   ↓
2. Manager: 创建 SRxCaptureStartMsg
   ↓
3. Worker: 接收 START → 发送 STARTED
   ↓
4. Manager: 接收 STARTED → 更新 SafeTaskMgr
   ↓
5. Worker: 定期发送 PROGRESS
   ↓
6. Manager: 接收 PROGRESS → 更新统计
   ↓
7. Worker: 发送 FILE_READY (批量)
   ↓
8. Manager: 接收 FILE_READY → 转发 FILE_ENQUEUE
   ↓
9. Clean: 接收 FILE_ENQUEUE → 触发压缩
   ↓
10. Worker: 发送 FINISHED
    ↓
11. Manager: 接收 FINISHED → 更新 SafeTaskMgr
    ↓
12. Clean: 发送 CLEAN_COMPRESS_DONE
    ↓
13. Manager: 接收 COMPRESS_DONE → 最终状态更新
    ↓
14. HTTP 线程获取状态显示给用户
```

---

## 重要注意事项

1. **单写线程模型**: 所有 SafeTaskMgr 的写操作必须由同一个线程（Manager）执行
2. **内存清理**: 必须定期调用 `cleanup_pending_deletes()`，通常在消息循环末尾
3. **消息顺序**: 不要依赖消息顺序，使用 `op_version` 和 `capture_id` 进行识别
4. **时间戳**: 所有时间戳使用微秒（usec），而不是毫秒
5. **配置快照**: 确保配置快照包含所有必要的参数，避免运行时读取配置

---

## 预期收益

完成此集成后，您将获得：

- 完整的消息驱动架构
- 可配置的进度上报机制
- 详细的错误诊断信息
- 线程安全的任务管理
- 可扩展的事件系统
- 便于调试和监控的完整日志

---
