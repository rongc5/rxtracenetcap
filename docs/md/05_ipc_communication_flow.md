# IPC 通信流程图

## 描述
此流程图展示了 C++ 核心引擎与 Python GUI 之间通过 Unix Domain Socket 进行进程间通信的详细流程。

## 流程图

```mermaid
flowchart TD
    A[C++ 核心启动] --> B[创建 Unix Socket]
    B --> C[绑定到本地路径]
    C --> D[开始监听连接]
    D --> E[等待客户端连接]
    
    F[Python GUI 启动] --> G[创建客户端 Socket]
    G --> H[连接到服务器]
    H --> I{连接成功?}
    I -->|失败| J[重试连接]
    I -->|成功| K[建立 IPC 连接]
    
    E --> L{有连接请求?}
    L -->|是| M[接受连接]
    L -->|否| E
    M --> K
    
    K --> N[开始数据通信]
    N --> O{通信方向}
    
    O -->|C++ → Python| P[数据包解析完成]
    P --> Q[生成 JSON 消息]
    Q --> R[发送到 Python]
    R --> S[Python 接收数据]
    S --> T[更新 GUI 显示]
    
    O -->|Python → C++| U[用户操作指令]
    U --> V[生成控制命令]
    V --> W[发送到 C++]
    W --> X[C++ 接收命令]
    X --> Y[执行相应操作]
    
    T --> Z[继续监听]
    Y --> Z
    Z --> N
    
    AA[连接异常] --> BB[重新建立连接]
    BB --> G
    
    CC[程序退出] --> DD[关闭 Socket]
    DD --> EE[清理资源]
    
    style A fill:#e3f2fd
    style F fill:#f3e5f5
    style K fill:#e8f5e8
    style AA fill:#ffebee
    style CC fill:#fff3e0
```

## 详细说明

### 1. 服务器端（C++ 核心）
- **Socket 创建**：创建 Unix Domain Socket 服务器
- **路径绑定**：绑定到 `/tmp/protoparse.sock` 路径
- **监听连接**：等待客户端连接请求
- **连接管理**：支持多个客户端同时连接

### 2. 客户端（Python GUI）
- **Socket 连接**：创建客户端 Socket 并连接到服务器
- **重连机制**：连接失败时自动重试
- **心跳检测**：定期发送心跳包检测连接状态

### 3. 数据通信协议
- **消息格式**：使用 JSON 格式进行数据交换
- **消息头**：包含消息类型、长度等元信息
- **消息体**：具体的数据内容

### 4. 双向通信
**C++ → Python（数据推送）**：
- 解析完成的协议数据
- 系统状态更新
- 错误和警告信息

**Python → C++（控制命令）**：
- 开始/停止抓包指令
- 配置更新命令
- 插件管理操作

### 5. 异常处理
- **连接断开**：自动重连机制
- **消息丢失**：消息确认和重传
- **缓冲区溢出**：流量控制机制

## 消息类型定义

### 数据消息（C++ → Python）
```json
{
    "type": "packet_data",
    "timestamp": "2025-01-31T11:19:31Z",
    "connection": {...},
    "parsed_data": {...}
}
```

### 控制消息（Python → C++）
```json
{
    "type": "control",
    "action": "start_capture",
    "params": {...}
}
```

### 状态消息（双向）
```json
{
    "type": "status",
    "component": "core_engine",
    "status": "running",
    "details": {...}
}
```
