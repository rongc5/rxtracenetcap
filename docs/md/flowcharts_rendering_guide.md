# 流程图图片生成指南

## 方法一：使用在线工具 (推荐)

1. 访问 https://mermaid.live/
2. 将 `images/` 目录下的 .mmd 文件内容复制到编辑器中
3. 点击右上角的 "Actions" → "Download PNG" 或 "Download SVG"
4. 将生成的图片保存到 `images/` 目录

## 方法二：使用本地工具

### 安装 mermaid-cli
```bash
npm install -g @mermaid-js/mermaid-cli
```

### 批量生成图片
```bash
cd docs/flowcharts/images
for file in *.mmd; do
    mmdc -i "$file" -o "${file%.mmd}.png" -t dark -b transparent
done
```

## 方法三：使用 Docker

```bash
docker run --rm -v $(pwd):/data minlag/mermaid-cli -i /data/input.mmd -o /data/output.png
```

## 生成的文件列表

- `01_system_overall_flow.mmd` → 系统总体流程图
- `02_packet_processing_flow.mmd` → 数据包处理流程图
- `03_plugin_loading_flow.mmd` → 插件加载流程图
- `04_tcp_reassembly_flow.mmd` → TCP 流重组流程图
- `05_ipc_communication_flow.mmd` → IPC 通信流程图
- `06_gui_workflow_flow.mmd` → GUI 工作流程图

## 建议的图片格式

- **PNG**: 适合文档嵌入，支持透明背景
- **SVG**: 矢量格式，可无损缩放
- **PDF**: 适合打印和正式文档
