# 简单同步日志库

基于参考框架设计的轻量级同步日志库，支持多线程安全、日志级别过滤、文件大小轮转。

## 特性

- **C++98 兼容**: 使用传统 C++，无需 C++11 特性
- **双重输出方式**: 支持传统 printf 风格和现代流式输出
- **线程安全**: 使用 pthread_mutex 保护并发访问
- **日志级别**: 支持 FATAL、WARNING、NOTICE、TRACE、DEBUG 五个级别
- **文件轮转**: 支持按文件大小自动轮转，备份文件添加时间戳
- **多文件输出**: 不同级别的日志写入不同文件（.ft、.wn、.nt、.tc、.db）
- **格式兼容**: 日志格式与参考框架保持一致

## 日志格式

```
[时间戳] 级别:[线程ID]:[行号:函数名:文件名] 消息内容
```

示例：
```
[2025-09-16 02:47:10] DEBUG:[268562811056480]:[9:worker_thread:test_logger.cpp] Worker thread 1, iteration 0
[2025-09-16 02:47:10] WARNING:[268562816858880]:[15:handle_request:server.cpp] Slow query detected
[2025-09-16 02:47:10] FATAL:[268562816858880]:[32:main:server.cpp] Database connection failed
```

## 使用方法

### 1. 初始化日志系统

```cpp
#include "logger.h"

// 基本初始化
LOG_INIT("logs", "myapp", 100*1024*1024, LOG_DEBUG);

// 参数说明：
// - log_path: 日志目录路径
// - prefix: 日志文件前缀（如果为 null 则使用进程名）
// - max_file_size: 单个日志文件最大大小（字节）
// - log_level: 日志级别（可组合，如 LOG_WARNING | LOG_FATAL）
```

### 2. 写入日志

#### 方式一：传统 printf 风格
```cpp
LOG_DEBUG_MSG("Debug info: value=%d", value);
LOG_TRACE_MSG("Function called with param: %s", param);
LOG_NOTICE_MSG("Server started on port %d", port);
LOG_WARNING_MSG("Connection timeout for client %s", client_ip);
LOG_FATAL_MSG("Critical error: %s", error_msg);
```

#### 方式二：流式输出（推荐）
```cpp
LOG_DEBUG_STREAM << "Debug info: value=" << value;
LOG_TRACE_STREAM << "Function called with param: " << param;
LOG_NOTICE_STREAM << "Server started on port " << port;
LOG_WARNING_STREAM << "Connection timeout for client " << client_ip;
LOG_FATAL_STREAM << "Critical error: " << error_msg;

// 支持复杂表达式
LOG_DEBUG_STREAM << "Calculation: " << x << " + " << y << " = " << (x + y);
```

### 3. 动态设置日志级别

```cpp
// 只记录 WARNING 和 FATAL 级别
LOG_SET_LEVEL(LOG_WARNING | LOG_FATAL);

// 记录所有级别
LOG_SET_LEVEL(LOG_FATAL | LOG_WARNING | LOG_NOTICE | LOG_TRACE | LOG_DEBUG);
```

## 文件命名规则

- FATAL: `prefix.ft`
- WARNING: `prefix.wn`
- NOTICE: `prefix.nt`
- TRACE: `prefix.tc`
- DEBUG: `prefix.db`

当文件超过最大大小时，会自动重命名为 `prefix.level.YYYYMMDD_HHMMSS` 格式。

## 编译和测试

```bash
# 编译测试程序
make

# 运行测试
make test

# 清理
make clean
```

## 示例程序

- `test_logger.cpp`: 基本功能测试，包括多线程、日志级别、文件轮转
- `stream_example.cpp`: 演示两种日志输出方式的对比
- `example_with_logger.cpp`: HTTP 服务器场景的日志使用示例

## 配置建议

### 开发环境
```cpp
LOG_INIT("logs", "myapp", 10*1024*1024, LOG_DEBUG);  // 10MB, 全部级别
```

### 生产环境
```cpp
LOG_INIT("/var/log/myapp", "myapp", 100*1024*1024, LOG_NOTICE | LOG_WARNING | LOG_FATAL);
```

### 调试模式
```cpp
LOG_INIT("debug_logs", "myapp", 1*1024*1024, LOG_DEBUG | LOG_TRACE);  // 1MB, 详细日志
```

## 性能特点

- **同步写入**: 保证日志完整性，适合中小型应用
- **内存占用小**: 单例模式，最小化内存使用
- **线程安全**: 适合多线程环境
- **自动轮转**: 防止单个日志文件过大

## 注意事项

1. 日志是同步写入的，在高频日志场景下可能影响性能
2. 日志目录需要有写权限
3. 文件轮转时短暂阻塞其他线程的日志写入
4. 程序退出时会自动关闭所有日志文件