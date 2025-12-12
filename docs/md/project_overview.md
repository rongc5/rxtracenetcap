# ProtoParse - TCP 协议抓包与解析工具

一个高性能的 TCP 层抓包工具，支持插件化的协议解析，提供现代化的图形界面。

## 项目特点

-  **高性能抓包**：基于 C++ 和 libpcap，支持高吞吐量网络流量捕获
-  **插件化架构**：灵活的协议解析插件系统，支持自定义内部协议
-  **现代化界面**：基于 PySide6 的直观图形界面
-  **跨平台支持**：支持 Linux、macOS 和 Windows
-  **实时分析**：实时显示协议解析结果和统计信息

## 快速开始

```bash
# 克隆项目
git clone <repository-url>
cd ProtoParse

# 构建 C++ 核心
cd core
mkdir build && cd build
cmake ..
make

# 安装 Python 依赖
cd ../../gui
pip install -r requirements.txt

# 运行程序
python main.py
```

## 项目结构

详见 `docs/design.md` 中的完整设计文档。

## 许可证

MIT License
