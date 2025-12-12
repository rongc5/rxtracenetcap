# 大小端自动检测设计方案

## 1. 需求分析

### 1.1 当前问题
- PDEF 配置需要明确指定字段的大小端（ENDIAN_BIG/ENDIAN_LITTLE）
- 用户可能不清楚协议使用的字节序
- 配置错误会导致匹配失败

### 1.2 目标
- 支持可选的字节序配置
- 不配置时，系统自动检测字节序
- 检测策略：先大端，失败则小端
- 学习机制：一旦确定字节序，后续使用确定的字节序

## 2. 设计方案

### 2.1 核心思路

**双字节码 + 运行时学习 + 持久化**

1. **编译阶段**：为每个 filter 生成两套字节码
   - `bytecode_be`：大端字节码
   - `bytecode_le`：小端字节码

2. **运行时阶段**：智能选择字节码
   - 首次匹配：先尝试大端，失败则尝试小端
   - 记录成功的字节序
   - 后续匹配：直接使用已确定的字节序

3. **持久化阶段**：回写 PDEF 文件
   - 检测成功后，将结果写回 PDEF 文件
   - 下次加载无需重新检测
   - 使用文件锁避免并发写入冲突

### 2.2 字节序模式

```c
typedef enum {
    ENDIAN_MODE_BIG,     // 强制大端
    ENDIAN_MODE_LITTLE,  // 强制小端
    ENDIAN_MODE_AUTO     // 自动检测（默认）
} EndianMode;

typedef enum {
    ENDIAN_TYPE_UNKNOWN, // 未检测
    ENDIAN_TYPE_BIG,     // 检测为大端
    ENDIAN_TYPE_LITTLE   // 检测为小端
} EndianType;
```

### 2.3 数据结构扩展

#### ProtocolDef
```c
typedef struct {
    char name[64];
    uint16_t* ports;
    uint32_t port_count;
    StructDef* structs;
    uint32_t struct_count;
    FilterRule* filters;
    uint32_t filter_count;

    // 新增：协议级别字节序配置
    EndianMode endian_mode;        // 字节序模式
    EndianType detected_endian;    // 运行时检测结果（线程安全）
    char pdef_file_path[256];      // PDEF 文件路径（用于回写）
    bool endian_writeback_done;    // 是否已回写（避免重复写入）
} ProtocolDef;
```

#### FilterRule
```c
typedef struct {
    char name[64];

    // 双字节码支持
    Instruction* bytecode_be;      // 大端字节码
    uint32_t bytecode_be_len;      // 大端字节码长度
    Instruction* bytecode_le;      // 小端字节码
    uint32_t bytecode_le_len;      // 小端字节码长度

    // 滑动窗口
    bool sliding_window;
    uint32_t sliding_max_offset;
} FilterRule;
```

### 2.4 匹配流程

```
packet_filter_match(packet, port, proto):
    # 1. 端口匹配
    if proto.port_count > 0 and port not in proto.ports:
        return false

    # 2. 遍历所有 filter 规则
    for each rule in proto.filters:
        matched = try_filter_with_endian(packet, rule, proto)
        if matched:
            return true

    return false

try_filter_with_endian(packet, rule, proto):
    mode = proto.endian_mode
    detected = proto.detected_endian

    # 情况1：强制大端
    if mode == ENDIAN_MODE_BIG:
        return execute_filter(packet, rule.bytecode_be, rule.bytecode_be_len)

    # 情况2：强制小端
    if mode == ENDIAN_MODE_LITTLE:
        return execute_filter(packet, rule.bytecode_le, rule.bytecode_le_len)

    # 情况3：自动模式
    if mode == ENDIAN_MODE_AUTO:
        # 已经检测出字节序
        if detected == ENDIAN_TYPE_BIG:
            return execute_filter(packet, rule.bytecode_be, rule.bytecode_be_len)

        if detected == ENDIAN_TYPE_LITTLE:
            return execute_filter(packet, rule.bytecode_le, rule.bytecode_le_len)

        # 首次检测：先大端后小端
        if detected == ENDIAN_TYPE_UNKNOWN:
            # 尝试大端
            if execute_filter(packet, rule.bytecode_be, rule.bytecode_be_len):
                proto.detected_endian = ENDIAN_TYPE_BIG  # 原子操作
                return true

            # 尝试小端
            if execute_filter(packet, rule.bytecode_le, rule.bytecode_le_len):
                proto.detected_endian = ENDIAN_TYPE_LITTLE  # 原子操作
                return true

            return false
```

### 2.5 PDEF 语法扩展（可选）

在 protocol 定义中添加可选的 `endian` 配置：

```pdef
protocol IEC104 {
    endian auto;  // 或 big, little（默认 auto）

    port 2404;

    filter message_start {
        ...
    }
}
```

如果不配置，默认为 `auto`。

### 2.6 编译器修改

#### 当前编译流程
```
PDEF 文件 -> 词法分析 -> 语法分析 -> 字节码生成
```

#### 修改后
```
PDEF 文件 -> 词法分析 -> 语法分析
            -> 字节码生成（大端版本）
            -> 字节码生成（小端版本）
```

**实现要点**：
- 编译器在生成 `OP_LOAD_*` 指令时：
  - 大端版本：生成 `OP_LOAD_U16_BE`, `OP_LOAD_U32_BE` 等
  - 小端版本：生成 `OP_LOAD_U16_LE`, `OP_LOAD_U32_LE` 等
- 其他指令（比较、跳转）保持一致

### 2.7 PDEF 文件回写机制

#### 2.7.1 回写时机

```
首次检测成功  触发回写（异步）
```

**触发条件**：
1. `endian_mode == ENDIAN_MODE_AUTO`（自动模式）
2. `detected_endian` 从 `UNKNOWN` 变为 `BIG` 或 `LITTLE`
3. `endian_writeback_done == false`（未回写过）

#### 2.7.2 回写格式

**原始 PDEF 文件**：
```pdef
protocol IEC104 {
    port 2404;

    filter message_start {
        u8 start == 0x68;
        u8 apdu_len;
    }
}
```

**回写后**：
```pdef
protocol IEC104 {
    endian big;  # auto-detected on 2025-12-08 15:30:45

    port 2404;

    filter message_start {
        u8 start == 0x68;
        u8 apdu_len;
    }
}
```

**插入位置**：
- 在 `protocol XXX {` 后的第一行
- 保留原有缩进和格式

#### 2.7.3 回写实现

```c
// 在 src/runtime/protocol.c 中添加
bool writeback_endian_to_pdef(ProtocolDef* proto, EndianType detected) {
    if (proto->endian_writeback_done) {
        return true;  // 已回写，跳过
    }

    // 1. 使用文件锁（flock）避免并发写入
    int fd = open(proto->pdef_file_path, O_RDWR);
    if (fd < 0) return false;

    if (flock(fd, LOCK_EX) != 0) {
        close(fd);
        return false;
    }

    // 2. 读取整个文件内容
    char* content = read_file(proto->pdef_file_path);
    if (!content) {
        flock(fd, LOCK_UN);
        close(fd);
        return false;
    }

    // 3. 查找插入位置（protocol XXX { 之后）
    char* insert_pos = find_protocol_block_start(content, proto->name);
    if (!insert_pos) {
        free(content);
        flock(fd, LOCK_UN);
        close(fd);
        return false;
    }

    // 4. 构造插入行
    time_t now = time(NULL);
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));

    char endian_line[256];
    snprintf(endian_line, sizeof(endian_line),
             "\n    endian %s;  # auto-detected on %s",
             (detected == ENDIAN_TYPE_BIG) ? "big" : "little",
             timestamp);

    // 5. 检查是否已存在 endian 配置
    if (strstr(content, "endian ") != NULL) {
        // 已存在，不再插入（避免重复）
        free(content);
        flock(fd, LOCK_UN);
        close(fd);
        proto->endian_writeback_done = true;
        return true;
    }

    // 6. 插入并写回文件
    char* new_content = insert_string(content, insert_pos, endian_line);

    ftruncate(fd, 0);
    lseek(fd, 0, SEEK_SET);
    write(fd, new_content, strlen(new_content));

    // 7. 释放资源
    free(content);
    free(new_content);
    flock(fd, LOCK_UN);
    close(fd);

    proto->endian_writeback_done = true;
    return true;
}
```

#### 2.7.4 调用时机

在 `try_filter_with_endian()` 中，检测成功后立即调用：

```c
// 尝试大端
if (execute_filter(packet, rule->bytecode_be, rule->bytecode_be_len)) {
    EndianType old_val = ENDIAN_TYPE_UNKNOWN;

    // 原子更新 detected_endian
    if (atomic_compare_exchange_strong(&proto->detected_endian,
                                        &old_val,
                                        ENDIAN_TYPE_BIG)) {
        // 首次检测成功，触发回写（异步）
        writeback_endian_to_pdef(proto, ENDIAN_TYPE_BIG);
    }

    return true;
}
```

#### 2.7.5 错误处理

**文件权限不足**：
- 记录警告日志
- 不影响正常过滤功能
- 下次启动仍会重新检测

**并发写入冲突**：
- 使用 `flock()` 文件锁
- 超时则放弃回写（不影响功能）

**文件格式损坏**：
- 查找插入位置失败时，放弃回写
- 记录错误日志

## 3. 性能分析

### 3.1 性能开销

| 场景 | 字节码执行次数 | 性能影响 |
|------|---------------|---------|
| 强制大端/小端 | 1次 | 无 |
| 自动模式（已学习） | 1次 | 无 |
| 自动模式（首次检测，成功） | 1次（大端成功）或2次（小端成功） | 仅首次包有开销 |

### 3.2 内存开销

- 每个 FilterRule：双倍字节码内存（通常 < 1KB）
- 每个 ProtocolDef：2个枚举字段（8字节）

### 3.3 优化点

1. **原子操作**：`detected_endian` 使用原子写入，避免竞态条件
2. **延迟学习**：只有首次包需要双重检测
3. **缓存友好**：检测后直接使用单一字节码分支

## 4. 线程安全

### 4.1 潜在问题
- 多个过滤线程可能同时访问 `proto->detected_endian`
- 首次检测时可能有竞态条件

### 4.2 解决方案

**使用原子操作（C11 stdatomic.h）**：

```c
#include <stdatomic.h>

typedef struct {
    ...
    atomic_int detected_endian;  // 使用原子类型
} ProtocolDef;

// 写入
atomic_store(&proto->detected_endian, ENDIAN_TYPE_BIG);

// 读取
EndianType detected = atomic_load(&proto->detected_endian);
```

**竞态条件分析**：
- 即使两个线程同时检测并写入相同值，结果也是正确的
- 即使写入不同值（极少数情况），后续会收敛到正确的字节序

## 5. 实现步骤

### 5.1 阶段1：删除 rxwriterthread
- [ ] 从 Makefile 移除 rxwriterthread.cpp
- [ ] 删除 rxwriterthread.h 和 rxwriterthread.cpp
- [ ] 验证编译

### 5.2 阶段2：扩展数据结构
- [ ] 修改 `pdef_types.h`：添加 EndianMode 和 EndianType 枚举
- [ ] 扩展 ProtocolDef 结构：添加 endian_mode 和 detected_endian
- [ ] 扩展 FilterRule 结构：添加双字节码字段

### 5.3 阶段3：修改编译器
- [ ] 修改 `compiler.c`：为每个 filter 生成双字节码
- [ ] 更新解析器：支持 `endian` 配置（可选）

### 5.4 阶段4：修改运行时
- [ ] 修改 `protocol.c`：实现 `try_filter_with_endian` 逻辑
- [ ] 添加原子操作支持：处理 `detected_endian` 的读写

### 5.5 阶段5：实现 PDEF 回写
- [ ] 实现 `writeback_endian_to_pdef()` 函数
- [ ] 实现文件内容读取和修改工具函数
- [ ] 实现插入位置查找逻辑
- [ ] 添加文件锁机制（flock）
- [ ] 在检测成功时触发回写

### 5.6 阶段6：测试
- [ ] 单元测试：测试大端、小端、自动模式
- [ ] 集成测试：使用真实 IEC104 数据包测试
- [ ] 回写测试：验证 PDEF 文件正确回写
- [ ] 并发测试：多进程同时回写不冲突

## 6. 向后兼容性

### 6.1 现有 PDEF 文件
- 不需要修改现有 PDEF 文件
- 默认使用 `endian auto` 模式
- 自动检测不会破坏现有行为

### 6.2 性能回归
- 强制指定字节序时，性能与之前完全一致
- 自动模式下，仅首次包有轻微开销

## 7. 未来扩展

### 7.1 统计信息
- 记录检测结果：`detected_endian_count[BIG/LITTLE]`
- 暴露给监控接口

### 7.2 混合字节序协议
- 某些字段大端，某些字段小端
- 需要字段级别的字节序配置（暂不实现）

## 8. 风险和注意事项

### 8.1 误检测风险
- 如果协议字段值恰好在大端和小端都能匹配过滤器，可能误判
- **缓解措施**：编写更严格的过滤规则，降低误匹配概率

### 8.2 调试困难
- 自动模式下，用户可能不清楚实际使用的字节序
- **缓解措施**：添加日志，记录检测结果

### 8.3 性能回退
- 极少数情况下，检测失败会导致首次包执行两次字节码
- **缓解措施**：文档中说明，建议已知字节序时显式配置

### 8.4 PDEF 回写风险

#### 8.4.1 文件权限问题
- PDEF 文件可能只读，回写失败
- **缓解措施**：记录警告日志，不影响过滤功能

#### 8.4.2 并发写入
- 多个进程同时运行可能冲突
- **缓解措施**：使用 `flock()` 文件锁

#### 8.4.3 文件格式损坏
- 回写逻辑有 bug 可能损坏 PDEF 文件
- **缓解措施**：
  - 先读取全部内容，构建新内容，再写入
  - 在查找插入位置失败时放弃回写
  - 建议用户使用版本控制（git）保护 PDEF 文件

#### 8.4.4 误检测回写
- 如果首次检测错误，会将错误结果写回
- **缓解措施**：
  - 用户可以手动修改 PDEF 文件中的 `endian` 配置
  - 使用注释标明是自动检测的，提醒用户验证

## 9. 文档更新

- [ ] 更新 PDEF 语法文档
- [ ] 添加大小端自动检测使用示例
- [ ] 更新性能基准测试文档
