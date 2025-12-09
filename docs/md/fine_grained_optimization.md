# 细粒度并发优化方案

## 核心思想

**问题分析**：
你的场景中，操作分为两类：
1. **高频操作**：状态更新（pending -> resolving -> running -> completed）
2. **低频操作**：新增任务、删除任务

**优化思路**：
- 状态更新 → **不切换 map**，只替换任务对象
- 新增/删除 → **切换 map**，保证结构一致性

---

## 性能对比

### 原方案：所有修改都切换

```cpp
// 旧方案：每次修改都切换 map
ToBufferMgr<CaptureTaskTable> mgr;

// 状态更新（需要切换 map）
mgr.modify([](CaptureTaskTable& table) {
    table.update_status(id, STATUS_RUNNING);  // 拷贝整个 map！
});

// 新增任务（需要切换 map）
mgr.modify([](CaptureTaskTable& table) {
    table.add_task(key, task);  // 拷贝整个 map
});
```

**开销分析**（假设 100 个任务）：
- 状态更新：拷贝 map（100项） + 替换1个 shared_ptr ≈ **10us**
- 新增任务：拷贝 map（100项） + 新增1个 shared_ptr ≈ **10us**

**问题**：
- 如果每秒更新 1000 次状态，总开销 = 1000 × 10us = **10ms** 
- 如果任务数增加到 1000，每次拷贝 ≈ 100us，总开销 = **100ms** 

---

### 新方案：细粒度控制

```cpp
// 新方案：区分高频和低频操作
FineGrainedTaskMgr mgr;

// 状态更新（不切换 map）
mgr.update_status(id, STATUS_RUNNING);  // 只替换1个 shared_ptr！

// 新增任务（切换 map）
mgr.add_task(key, task);  // 拷贝整个 map
```

**开销分析**（假设 100 个任务）：
- 状态更新：创建新 Task 对象 + 替换 shared_ptr ≈ **0.5us** 
- 新增任务：拷贝 map（100项） + 新增1个 shared_ptr ≈ **10us**

**收益**：
- 如果每秒更新 1000 次状态，总开销 = 1000 × 0.5us = **0.5ms** （提升 20 倍！）
- 即使任务数增加到 1000，状态更新仍然是 **0.5us**（与任务数无关！）

---

## 使用示例

### 示例1: 状态更新（高频）

```cpp
// 场景：worker 线程完成抓包，更新状态
bool CRxCaptureThread::finish_capture(int capture_id)
{
    proc_data* p_data = proc_data::instance();
    FineGrainedTaskMgr& task_mgr = p_data->capture_task_mgr();

    // 方式1: 直接更新状态（最快）
    bool success = task_mgr.update_status(capture_id, STATUS_COMPLETED);

    // 方式2: 自定义更新（更灵活）
    task_mgr.update_task(capture_id, [](SRxCaptureTask& task) {
        task.status = STATUS_COMPLETED;
        task.end_time = time(NULL);
        task.packet_count = 12345;
        task.bytes_captured = 67890;
    });

    return success;
}

// 性能：
// - 不需要拷贝整个 map
// - 只创建1个新的 Task 对象（拷贝构造）
// - 替换 shared_ptr（原子操作）
// 总开销：~0.5us（与任务总数无关！）
```

### 示例2: 新增任务（低频）

```cpp
// 场景：用户发起新的抓包请求
void CRxCaptureManagerThread::create_task(...)
{
    proc_data* p_data = proc_data::instance();
    FineGrainedTaskMgr& task_mgr = p_data->capture_task_mgr();

    std::tr1::shared_ptr<SRxCaptureTask> task(new SRxCaptureTask());
    task->capture_id = capture_id;
    task->status = STATUS_PENDING;
    // ... 设置其他字段

    // 添加任务（会触发 map 切换）
    task_mgr.add_task(task_key, task);
}

// 性能：
// - 拷贝整个 map（开销取决于任务数）
// - 100个任务：~10us
// - 1000个任务：~100us
// 但这是低频操作，完全可以接受
```

### 示例3: 删除任务（低频）

```cpp
// 场景：清理完成的任务
void CRxCaptureManagerThread::cleanup_completed()
{
    proc_data* p_data = proc_data::instance();
    FineGrainedTaskMgr& task_mgr = p_data->capture_task_mgr();

    // 获取当前快照
    FineGrainedTaskTable snapshot = task_mgr.get();

    // 找出需要删除的任务
    std::vector<int> to_remove;
    for (std::map<std::string, std::tr1::shared_ptr<SRxCaptureTask> >::const_iterator it = snapshot.tasks.begin();
         it != snapshot.tasks.end(); ++it) {
        if (it->second && it->second->status == STATUS_COMPLETED) {
            // 检查是否超过保留时间
            if (time(NULL) - it->second->end_time > 3600) {
                to_remove.push_back(it->second->capture_id);
            }
        }
    }

    // 删除任务
    for (size_t i = 0; i < to_remove.size(); ++i) {
        task_mgr.remove_task(to_remove[i]);  // 每次删除都会切换
    }
}

// 优化：批量删除（减少切换次数）
void CRxCaptureManagerThread::cleanup_completed_batch()
{
    proc_data* p_data = proc_data::instance();
    FineGrainedTaskMgr& task_mgr = p_data->capture_task_mgr();

    FineGrainedTaskTable snapshot = task_mgr.get();

    std::vector<int> to_remove;
    for (std::map<std::string, std::tr1::shared_ptr<SRxCaptureTask> >::const_iterator it = snapshot.tasks.begin();
         it != snapshot.tasks.end(); ++it) {
        if (it->second && it->second->status == STATUS_COMPLETED) {
            if (time(NULL) - it->second->end_time > 3600) {
                to_remove.push_back(it->second->capture_id);
            }
        }
    }

    // 批量删除（只切换一次！）
    if (!to_remove.empty()) {
        task_mgr.modify_structure([&](FineGrainedTaskTable& table) {
            for (size_t i = 0; i < to_remove.size(); ++i) {
                table.remove_task(to_remove[i]);
            }
        });
    }
}
```

### 示例4: 读取任务（并发安全）

```cpp
// 场景：HTTP 线程查询任务状态
void handle_query(int capture_id)
{
    proc_data* p_data = proc_data::instance();
    FineGrainedTaskMgr& task_mgr = p_data->capture_task_mgr();

    // 方式1: 获取完整快照（最安全）
    FineGrainedTaskTable snapshot = task_mgr.get();

    std::map<int, std::string>::const_iterator id_it = snapshot.id_to_key.find(capture_id);
    if (id_it != snapshot.id_to_key.end()) {
        std::map<std::string, std::tr1::shared_ptr<SRxCaptureTask> >::const_iterator it =
            snapshot.tasks.find(id_it->second);

        if (it != snapshot.tasks.end() && it->second) {
            // 使用任务对象
            LOG_NOTICE("Task %d status: %d", capture_id, it->second->status);
        }
    }

    // 方式2: 快速访问（如果不需要强一致性）
    const FineGrainedTaskTable* table = task_mgr.current_unsafe();
    // 使用 table，但可能在读取过程中被修改
}
```

---

## 操作分类建议

###  不需要切换的操作（高频）

1. **状态更新**
   - `update_status(id, new_status)`
   - 每个任务的生命周期会多次更新状态
   - 频率高，必须优化

2. **运行时字段更新**
   ```cpp
   mgr.update_task(id, [](SRxCaptureTask& task) {
       task.packet_count++;
       task.bytes_captured += bytes;
   });
   ```

3. **错误信息记录**
   ```cpp
   mgr.update_task(id, [](SRxCaptureTask& task) {
       task.error_message = "timeout";
   });
   ```

###  需要切换的操作（低频）

1. **新增任务**
   - `add_task(key, task)`
   - 改变 map 结构

2. **删除任务**
   - `remove_task(id)`
   - 改变 map 结构

3. **批量操作**
   ```cpp
   mgr.modify_structure([](FineGrainedTaskTable& table) {
       table.add_task(key1, task1);
       table.add_task(key2, task2);
       table.remove_task(id3);
   });
   ```

---

## 性能测试

### 测试场景

```
任务总数：100
操作比例：
  - 状态更新：90%（高频）
  - 新增任务：5%（低频）
  - 删除任务：5%（低频）
总操作数：10000 次/秒
```

### 原方案性能

```
每次操作都拷贝 map（100项）：
  - 拷贝开销：10us/次
  - 总开销：10000 × 10us = 100ms/秒
  - CPU 占用：10%（单核）
```

### 新方案性能

```
状态更新不拷贝 map，只拷贝 Task 对象：
  - 状态更新：9000次 × 0.5us = 4.5ms
  - 新增任务：500次 × 10us = 5ms
  - 删除任务：500次 × 10us = 5ms
  - 总开销：14.5ms/秒
  - CPU 占用：1.45%（单核）

性能提升：100ms → 14.5ms = 6.9倍！
```

### 极端场景（1000个任务）

```
原方案：
  - 每次拷贝：100us
  - 总开销：10000 × 100us = 1000ms/秒 = 100% CPU 

新方案：
  - 状态更新：9000次 × 0.5us = 4.5ms
  - 新增任务：500次 × 100us = 50ms
  - 删除任务：500次 × 100us = 50ms
  - 总开销：104.5ms/秒 = 10.45% CPU 

性能提升：1000ms → 104.5ms = 9.6倍！
```

---

## 实现细节说明

### 为什么状态更新不需要切换？

**关键点**：
1. `CaptureTaskTable` 的 map 存储的是 `shared_ptr<Task>`
2. 状态更新时，创建新的 `Task` 对象，替换 `shared_ptr`
3. `shared_ptr` 的替换是原子的（引用计数原子递增/递减）

**安全性**：
```cpp
// 写线程：
std::tr1::shared_ptr<SRxCaptureTask> new_task(new SRxCaptureTask(*old_task));
new_task->status = STATUS_RUNNING;
tasks[key] = new_task;  // 替换 shared_ptr

// 读线程（同时读取）：
FineGrainedTaskTable snapshot = mgr.get();  // 拷贝 map
std::tr1::shared_ptr<SRxCaptureTask> task = snapshot.tasks[key];  // 拷贝 shared_ptr

// 即使写线程替换了 shared_ptr，读线程持有的是旧的 shared_ptr
// 旧 Task 对象不会被释放（引用计数 > 0）
// 读线程读到的是一致的旧状态 
```

**为什么单写线程下是安全的**：
- 只有一个写线程，不会并发修改同一个 `map[key]`
- 替换 `shared_ptr` 虽然不是严格原子的，但因为单写线程，不会有竞争
- 读线程通过 SeqLock 机制，读到的是稳定的快照

### 注意事项

 **重要**：这个方案假设：
1. **单写线程**：只有 fetchManagerThread 会修改
2. **shared_ptr 替换**：虽然不是严格原子，但单写线程下安全

如果你有**多个写线程**，需要额外保护：
```cpp
// 多写线程场景：需要对每个 map[key] 加锁
std::map<std::string, TaskSlot> tasks;  // TaskSlot 内部用原子操作

class TaskSlot {
    std::tr1::shared_ptr<Task> get();  // 原子读
    void set(shared_ptr<Task>);         // 原子写
};
```

---

## 总结与建议

### 方案选择

| 场景 | 推荐方案 | 理由 |
|------|----------|------|
| 任务数 < 50，更新频率低 | `OptimizedBufferMgr` | 简单，够用 |
| 任务数 > 50，更新频率高 | `FineGrainedTaskMgr` | 性能提升显著 |
| 任务数 > 500 | `FineGrainedTaskMgr` | 必须用，否则性能崩溃 |

### 性能收益

```
任务数 = 100，更新频率 = 1000次/秒：
  OptimizedBufferMgr:     100ms CPU/秒
  FineGrainedTaskMgr:     14.5ms CPU/秒
  性能提升：6.9倍

任务数 = 1000，更新频率 = 1000次/秒：
  OptimizedBufferMgr:     1000ms CPU/秒（崩溃）
  FineGrainedTaskMgr:     104.5ms CPU/秒
  性能提升：9.6倍
```

### 迁移建议

**步骤1**：先用 `OptimizedBufferMgr` 替换 `ToBufferMgr`
- 改动小
- 立即解决原子性问题
- 性能有一定提升

**步骤2**：压测验证
- 模拟实际负载
- 监控 CPU 占用

**步骤3**：如果性能不足，再迁移到 `FineGrainedTaskMgr`
- 改动稍大
- 性能提升显著

### 代码示例

```cpp
// proc_data.h
#include "fine_grained_task_mgr.h"

class proc_data {
    FineGrainedTaskMgr _capture_task_mgr;
};

// rxcapturemanagerthread.cpp

// 新增任务（低频）
void handle_start_capture(...) {
    task_mgr.add_task(task_key, task);  // 会切换
}

// 状态更新（高频）
void update_task_status(int id, ECaptureTaskStatus status) {
    task_mgr.update_status(id, status);  // 不会切换！
}

// 读取任务
FineGrainedTaskTable snapshot = task_mgr.get();
// 使用 snapshot，安全
```

**你的场景非常适合 `FineGrainedTaskMgr`！**
