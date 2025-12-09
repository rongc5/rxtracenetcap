CLI 请求 JSON 示例
====================

本目录存放供 rxcli 使用的示例请求 JSON。每个文件描述一个抓包场景，可通过 `rxcli <json 文件>` 执行。

文件说明：
- capture_interface.json: 仅指定网卡名称的基础抓包
- capture_bpf.json: 只包含 BPF 过滤表达式的抓包
- capture_process.json: 按进程名启动抓包
- capture_process_self.json: 针对 rxtracenetcap 自身进程的抓包
- capture_pid.json: 通过 PID 启动抓包
