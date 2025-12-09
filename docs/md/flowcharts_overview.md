# ProtoParse 流程图集合

本目录包含了 ProtoParse 系统的各种流程图，帮助开发人员理解系统的工作原理和数据流向。

## 流程图列表

### 1. [系统总体流程图](01_system_overall_flow.md)
展示了 ProtoParse 系统从启动到运行的完整生命周期，包括：
- 系统初始化流程
- 用户交互处理
- 数据处理循环
- 程序退出清理

### 2. [数据包处理流程图](02_packet_processing_flow.md)
详细展示了从网络数据包捕获到协议解析的完整数据处理pipeline：
- libpcap 数据包捕获
- BPF 过滤器处理
- TCP 流重组
- 协议解析和输出

### 3. [插件加载流程图](03_plugin_loading_flow.md)
展示了协议解析插件的动态加载过程：
- 插件发现和扫描
- 动态库加载和验证
- 插件注册和初始化
- 错误处理机制

### 4. [TCP 流重组流程图](04_tcp_reassembly_flow.md)
详细展示了 TCP 数据包重组的过程：
- 连接跟踪和管理
- 序列号处理
- 乱序和重传处理
- 完整性检查

### 5. [IPC 通信流程图](05_ipc_communication_flow.md)
展示了 C++ 核心引擎与 Python GUI 之间的进程间通信：
- Unix Domain Socket 通信
- 双向数据交换
- 异常处理和重连
- 消息协议定义

### 6. [GUI 工作流程图](06_gui_workflow_flow.md)
展示了 Python GUI 的用户交互工作流程：
- 界面初始化
- 用户操作处理
- 数据展示更新
- 配置管理

## 如何查看流程图

### 在线查看
推荐使用支持 Mermaid 语法的 Markdown 编辑器或在线工具：
- [Mermaid Live Editor](https://mermaid.live/)
- GitHub/GitLab（原生支持 Mermaid）
- Typora、Mark Text 等编辑器

### 本地查看
1. 使用 VS Code + Mermaid Preview 插件
2. 使用 Obsidian 等支持 Mermaid 的笔记软件
3. 使用 mermaid-cli 生成 PNG/SVG 图片

### 生成图片文件
```bash
# 安装 mermaid-cli
npm install -g @mermaid-js/mermaid-cli

# 生成 PNG 图片
mmdc -i 01_system_overall_flow.md -o 01_system_overall_flow.png

# 生成 SVG 图片
mmdc -i 01_system_overall_flow.md -o 01_system_overall_flow.svg
```

## 流程图更新说明

这些流程图会随着系统设计的演进而更新。如果您发现流程图与实际实现不符，请及时更新相应的文档。

## 贡献指南

如果需要添加新的流程图或修改现有流程图：
1. 遵循现有的命名规范（编号_描述_flow.md）
2. 使用 Mermaid 语法绘制流程图
3. 提供详细的说明文档
4. 更新本 README 文件的索引
