# Dev Notes – Legacy HTTP Integration (2025-10-09)

## 当前进度
- 已将 `core` 全量同步，并恢复其 http/base_net 体系。
- `CRxBaseNetThread` 回退为旧框架适配层；HTTP 主线程改为 listener + worker 模式。
- 新增 `CRxHttpResDataProcess`，并重写 `CRxHttpApi` 以支持无 libevent 的 REST 请求。
- Biz/Capture 线程改为通过 `NORMAL_MSG_HTTP_REPLY` 异步回应 HTTP 请求。
- `Makefile` 已重新加入 legacy 源码并移除 libevent 链接依赖。
- 2025-10-20：整理核心编译告警（LOG_INIT 重定义、`main.cpp` 缓冲区/未使用参数、`rxsamplethread.cpp` `fgets` 检查、`capturethread.cpp` `memset` 使用）并确保 `make server` 无告警。
- 2025-10-20：为抓包启动链路补充调试日志（HTTP handler → Manager → HTTP 回复），记录排队耗时并透出 `X-Debug-QueueMs`；实现采样阈值告警链路（`proc_data::add_sample_alert` 缓存最近 128 条 + `RX_MSG_SAMPLE_TRIGGER` 推送至 HTTP 线程并落日志）。
- 2025-10-21：核心 `http_base_process` 提供异步发送接口（`async_response_pending/notify_send_ready`），业务侧不再直接调用 `change_http_status`，异步响应统一由框架管理。
- 2025-10-21：清理线程支持记录文件滚动、归档压缩与失败上报；SafeTaskMgr/查询接口返回抓包文件与压缩归档元信息；配置新增 cleanup 段（记录目录、压缩阈值、归档目录等）。

## 未完成事项 / 下一步
- 调整 SafeTaskMgr::is_key_active 仅在状态为 pending/resolving/running 时判定重复，
  使已完成任务允许重新启动抓包。
1. **HTTP API / Handler 验证**
   - 跑全量 curl/CLI 回归，复核 `X-Debug-QueueMs` 与 HTTP worker 日志，确认耗时来源（进程解析、SafeTaskMgr 等）是否合理。
   - 检查 `/default` handler 是否需要自定义 404 payload 或链路追踪字段。

2. **运行特性完善**
   - 为 sample 告警提供查询接口（REST/CLI）并设计过期策略；目前仅缓存最近 128 条日志。
   - Capture manager 的阈值检查/压缩逻辑已接入定时器，但需结合真实配置验证周期是否合理。

## 建议恢复点
- 若需恢复旧 libevent 架构，可参考 `src_backup_20251009_023317/`。
- 重新 `make server`、`make cli` 验证构建，持续关注 legacy 宏导致的潜在告警。

## 抓包服务当前验证要点
- 启动 / 停止 / 状态 API 的响应均包含 `capture_id` 与 `key` 字段（见 `src/rxcapturemanagerthread.cpp:736`, `src/rxcapturemanagerthread.cpp:818`），CLI 输出在 `tools/rxcli_main.cpp:288`。
- 进程名抓包依赖 `resolve_target_processes` 返回的 `listening_ports` 组合 BPF（`src/rxcapturemanagerthread.cpp:654`），端口集合在 `src/rxprocessresolver.cpp:72` 去重。
- 采样线程仍未与 manager 打通，只在 `src/rxsamplethread.cpp:57`、`82` 记录日志。
- 测试流程：`make server`、`make cli` 后使用 `./bin/rxcli tools/cli_tasks.json`，服务启动前记得准备 `conf` 软链接与 `strategy.conf`；重复任务需等待或停止旧任务避免 duplicate。
