Capture Service Design (HTTP + Worker Pool)

Overview
- Goal: Long-running capture service receiving JSON over HTTP or from a CLI, dispatching business tasks to worker threads, capturing packets to files with rotation and compression, and exposing status/controls.
- Constraints: C++98, libevent (evhttp + event_pthreads), pthread, libpcap. Main thread handles HTTP; worker threads (each with its own event_base) handle business tasks.

Architecture
- HTTP Coordinator Thread
  - Implemented as `CRxHttpThread : CRxBaseNetThread` (libevent `evhttp` registered on its `event_base`).
  - Parses JSON, canonicalizes `key`, performs dedup + cooldown + capacity checks synchronously.
  - Assigns an `id`, selects a worker, dispatches a `StartMsg` via `socketpair`, and immediately replies to the client.
  - Maintains `running_map` (key→ExecRecord) and `id_map` (id→ExecRecord). Handles `status` and `stop` using these maps.
  - Receives `DoneMsg/FailMsg/StoppedMsg` from workers and updates the maps accordingly.
- Worker Threads (CRxBizWorker : CRxBaseNetThread)
  - Each worker has its own `event_base` and `socketpair` recv fd.
  - Upon notification, drains its queue, finalizes the task (e.g., proc_name→ports→BPF), executes capture via `CRxCaptureManager`.
  - Sends a `DoneMsg` back to the HTTP thread; does not modify `running_map` directly.
- Monitoring (Monitor)
  - A background sampler thread periodically reads `/proc` to collect CPU, memory, and network stats.
  - Provides snapshot APIs for the worker logic to evaluate conditional captures.
- Capture Execution (CaptureManager)
  - Manages capture tasks (start/stop/status) using libpcap (`pcap_open_live`, `pcap_dump_open`).
  - Supports duration/timeboxing, BPF filters, size/time-based rolling (extensible), and per-task state.
- Storage & Rotation (StorageManager)
  - Defines directory schema, filename patterns, rotation, compression (e.g., `gzip -9`), and retention cleanup.

HTTP API (proposed)
- Base path: `/api/capture`
- `POST /api/capture/start`
  - Body (JSON):
    - `iface` (string): Interface name, default from config (e.g., `eth0`).
    - `filter` (string): BPF expression, optional.
    - `duration_sec` (int): 0 for indefinite; otherwise seconds.
    - `category` (string): `incident|diag|audit` to classify files.
    - `file_pattern` (string): Filename template, overrides default.
    - `max_bytes` (int): Single file size cap (optional).
    - `cond` (object, optional): `{ cpu_pct_gt, mem_pct_gt, net_rx_kbps_gt, window_sec }`.
    - `proc_name` (string, optional): Target process name; used to derive ports.
    - `pid` (int, optional): Target PID.
    - `ports` (array<int>, optional): Prefer explicit ports when available.
    - `key` (string, optional): Task key; if absent, derived from fields.
    - `coalesce_window_sec` (int, optional): Merge window for identical keys.
    - `cooldown_sec` (int, optional): Per-key cooldown to throttle repeats.
  - Response: `{ "id": 101, "coalesced": true, "msg": "merged with running task" }`.
- `POST /api/capture/stop`
  - Body: `{ "id": 101 }` => `{ "ok": true }`.
- `GET /api/capture/status?id=101`
  - Response: `{ "id": 101, "running": true, "packets": 12345, "exit": 0 }`.
- Optional: `/api/config/reload`, `/health`, `/metrics`.

Configuration File
- Path: `/etc/rxtrace/config.json` (or env `RXTRACE_CONFIG`).
- Example:
  - `defaults`: `{ "iface": "eth0", "duration_sec": 60, "category": "diag", "file_pattern": "{date}/{category}/{ts}-{iface}-{proc}-{seq}.pcap", "max_bytes": 1073741824 }`
  - `thresholds`: `{ "cpu_pct_gt": 85, "mem_pct_gt": 90, "net_rx_kbps_gt": 8000 }`
  - `limits`: `{ "max_concurrent_captures": 3, "queue_capacity": 1024, "per_key_cooldown_sec": 120, "per_client_rate": { "window_sec": 10, "max_req": 20 } }`
  - `storage`: `{ "base_dir": "/var/log/rxtrace", "rotate": { "max_age_days": 7, "max_size_gb": 100 }, "compress": { "enabled": true, "cmd": "gzip -9" } }`
- Precedence: request JSON > defaults; requests exceeding configured limits are rejected or clamped.

Process-Name Capture (proc_name)
- Classic BPF cannot filter by process. Practical approach first (Plan A): derive a BPF from process-to-ports mapping.
  - Discover listening and active ports for a process by parsing `/proc` or invoking `ss -ntup`.
  - Build a BPF like: `tcp and (dst port 80 or src port 443 ...)`.
  - Update mapping periodically to reflect dynamic ports.
- Optional advanced path (Plan B): eBPF/cgroup-level filtering (requires higher complexity and kernel support), considered later.

Deduplication and Rate Limiting
- Task key: derived from normalized fields (`iface+proc_name+category+filter`) or client-provided `key`.
- Centralized dedup in HTTP coordinator:
  - `already_running` → 200 `{ "already_running": true, "id": <id> }`
  - `cooldown` → 429 `{ "cooldown": true }`
  - `over_capacity` → 429/503 (configurable)
  - `accepted` → 200 `{ "id": <id> }` and dispatch to a worker
- Pending (conditional): if conditions not met, HTTP thread records a pending entry and replies 202; evaluator (HTTP or scheduler) triggers later.
- Capacity limits: global queue capacity, per-key queue depth, max concurrent captures.
- Per-client rate: token-bucket by client IP to handle bursts.

Monitoring
- Sampling thread reads `/proc` every N seconds.
  - CPU: `/proc/stat` => utilization %.
  - Memory: `/proc/meminfo` => usage %.
  - Network: `/proc/net/dev` (or Netlink) => rx/tx kbps.
- Exposes a thread-safe snapshot API for evaluating `cond` in worker logic.

Storage and Rotation
- Directory scheme: `{base_dir}/{date}/{category}/`.
- Filename template variables: `{host} {date} {ts} {iface} {proc} {pid} {seq}`.
- Rolling: size/time-based; upon roll, optionally compress previous file via configured command.
- Cleanup: scheduled task to enforce `max_age_days` and `max_size_gb` (e.g., by mtime or LRU).

CLI Client
- Reads a JSON file and POSTs to the service.
- Implement a small C++98 HTTP client with libevent (preferred) or shell-out to `curl` initially for speed.
- Commands: `start`, `stop`, `status`, `config-reload`.

Implementation Plan (Phased)
1) Core plumbing
   - ConfigManager: load defaults/limits/thresholds; precedence resolution.
   - BizWorker: extend Task with key/coalesce/cooldown/category/file_pattern/max_bytes/cond/proc_name/ports/pid.
   - Dedup/cooldown logic; enqueue capacity checks.
   - HTTP routes: accept POST JSON for start/stop; status.
2) Process mapping
   - `/proc`/`ss`-based process→ports discovery; BPF generator.
3) CaptureManager extensions
   - Filename templating + categorized directories; optional size/time rolling.
   - Hook for compression on rollover.
4) Monitoring
   - Sampler thread + snapshot API; condition evaluation for `cond`.
   - `/metrics` and `/health` endpoints (optional).
5) StorageManager
   - Retention and capacity-based cleanup; compression job scheduling.
6) CLI client
   - C++98 libevent HTTP client; JSON file → POST.

Risks and Mitigations
- Proc-name filtering: BPF can’t match PID/process; mitigate via port mapping first, later consider eBPF.
- Request bursts: enforce per-key cooldown + coalescing + per-client rate limiting + queue capacity + max concurrency.
- Long-running captures: enforce hard limits (duration/size) from config; require explicit stop for indefinite captures.
- Disk usage: rolling + compression + cleanup by retention and size budgets.

Current Repo Status (as implemented)
- HTTP server: `src/http_capture_server.cpp` (main entry). Args: `<bind_ip> <port> <workers>`.
- Worker pool: `src/biz_worker.*` (socketpair notify + per-worker event_base + task queue).
- Capture manager: `src/capture_manager.*` (pcap dump to file; basic start/stop/status).
- Make: `src/Makefile.http` builds `http_capture_server` against vendored libevent and libpcap.
- Next: add ConfigManager, Monitor, process→ports, coalescing/cooldown, and POST JSON parsing.

Augmented Design Extensions (Adopted)

1) Priority Scheduling and Scheduler Policy
- Request JSON adds:
  - `priority`: `"high"|"normal"|"low"` (default `normal`).
  - `scheduler` (optional override or config-only):
    - `high_priority_slots` (int): concurrent slots reserved for high-priority tasks.
    - `preempt_low_priority` (bool): if true, delay/deny new low-priority starts when high-priority pending; true preemption of running captures is future work.
    - `queue_policy`: `"fifo"|"priority"|"fair"`.
- Worker pool logic:
  - Three ready-queues by priority; dispatch order high→normal→low.
  - High-priority slots enforced against total concurrency.
  - With `preempt_low_priority=true`, new low-priority tasks may be enqueued but not started until high-priority pressure subsides (soft-preempt via deferral).

2) Smarter Overload Protection (LoadBalancer)
- `LoadBalancer` component monitors:
  - queue length, waiting time percentiles, running captures, system load (Monitor snapshots).
- Actions:
  - Dynamic scale-out (create additional BizWorkers) when thresholds exceeded; scale-in left for later.
  - Adaptive throttling: raise per-key cooldown, reject/merge low-priority tasks first, enforce per-client token bucket.
- Config additions:
  - `limits.autoscale`: `{ enable: true, max_workers: 8, queue_len_high: 200, p95_wait_ms: 200 }`.

3) Enhanced Conditional Capture
- Request JSON:
```
{
  "conditions": {
    "cpu_spike": { "threshold": 85, "duration_sec": 30 },
    "mem_pct_gt": 90,
    "net_rx_kbps_gt": 8000,
    "process_restart": { "proc_name": "nginx", "event": "start|exit" },
    "custom_script": "/path/to/condition_checker.sh"
  }
}
```
- Notes:
  - `cpu_spike` uses Monitor deltas/window; `process_restart` requires future event source; `custom_script` runs under rate-limit, timeout, whitelist.

4) Pluggable Capture Strategy
- Interface (conceptual, C ABI recommended later):
```
class CaptureStrategy {
public:
  virtual bool shouldCapture(const Context& ctx) = 0;
  virtual std::string buildFilter(const Request& req) = 0;
  virtual ~CaptureStrategy() {}
};
```
- Built-ins: process→ports, explicit ports, traffic pattern (later), custom script adapter.
- Future: load `.so` and register strategies via dlsym (avoid C++ ABI pitfalls in C++98).

5) Storage Enhancements
- Config templates by category:
```
"storage": {
  "templates": {
    "incident": "{date}/incidents/{ts}-{severity}-{proc}.pcap",
    "audit":    "{date}/audit/{user}-{action}-{ts}.pcap"
  },
  "lifecycle": {
    "hot_tier_days": 1,
    "warm_tier_days": 7,
    "archive_tier_days": 30
  }
}
```
- Phase 1: implement naming templates + retention/size cleanup + compression; tiering/migration later.

6) CLI Enhancements
- Commands:
  - `rxtrace batch --config batch_tasks.json`
  - `rxtrace watch --filter "priority=high"`
  - `rxtrace export --format=json --timerange="1h"`
- Client authenticates via API key header; supports JSON file submission and progress/status watching.

7) Security and Audit
- Config example:
```
"security": {
  "auth": { "api_key_required": true, "jwt_token": false },
  "audit": { "log_all_requests": true, "sensitive_data_mask": true },
  "isolation": { "chroot_capture": "/var/lib/rxtrace", "drop_privileges": "nobody:nogroup" }
}
```
- Implementation (phase 1): API key validation (X-API-Key), audit logs (request summary, masked fields), privilege drop. JWT optional later.

8) Ops & Observability
- Endpoints:
  - `GET /health` → `{ "status": "ok", "checks": {...} }`.
  - `GET /api/metrics` → Prometheus text: queue_depth, running_captures, tasks_started_total, errors_total, p95_wait_ms, cpu_pct, mem_pct, net_rx_kbps.
  - `POST /api/admin/reload` → reload config hot.
  - `GET /api/debug/workers` → worker states, queue sizes, recent tasks (guarded by auth).

9) Advanced Merge (TaskMerger) and PredictiveCapture (Future)
- TaskMerger: merge by time-window similarity, resource-aware coalescing, optional geo/host grouping.
- PredictiveCapture: hook for ML-based pre-triggering; not in initial phases.

Phased Implementation Update
1) Phase A
   - POST JSON for `/api/capture/start|stop`; ConfigManager; extend Task fields (priority/key/coalesce/cooldown/category/file_pattern/max_bytes/conditions).
   - Priority queues + high_priority_slots + soft-preempt (deferral). Per-key cooldown + per-client rate limit. Basic /health and /api/admin/reload.
2) Phase B
   - Monitor sampler + adaptive throttling + `/api/metrics`.
   - Process→ports mapper + BPF generator for `proc_name`.
3) Phase C
   - Storage cleanup/retention/compression; filename templates by category. LoadBalancer scale-out.
4) Phase D (later)
   - Plugin strategies, process event triggers, storage tiering, JWT, debug endpoints, cluster/distribution.
Coordinator Data Structures (HTTP thread)
- `running_map`: key → `{ id, worker_idx, started_at, priority, category }`
- `id_map`: id → same record for quick lookup
- `cooldown_map`: key → last_start_ts
- `next_worker_idx`: round-robin index (can be replaced by smarter policy)

Message Flow
- Start:
  1) HTTP receives start, parses JSON, builds `key`.
  2) Dedup/cooldown/capacity checks on HTTP thread.
  3) If accepted: allocate `id`, select worker, send `StartMsg` (id, key, iface, filter/file_pattern, duration, etc.). Reply 200 with id. Record in `running_map`.
  4) Worker executes capture (possibly enrich filter from `proc_name`), and on completion sends `DoneMsg` back to HTTP.
  5) HTTP processes `DoneMsg`, removes from `running_map` / `id_map`.
- Stop: HTTP finds worker from `id_map`, sends `StopMsg` to that worker; worker stops capture and replies `StoppedMsg`.
- Status: HTTP replies from `running_map` / `id_map`.

CRxBizWorker Static Interface (optional convenience)
- `Init(CRxTaskScheduler* sched)`, `CreateAndAddWorker()`, `AddWorkerThread(CRxBaseNetThread*)`, `Dispatch(SRxTask*)`, `StopAll()`
- Internally uses a simple manager (round-robin) to pick a worker.

No Double-Buffer
- Since the HTTP coordinator serializes all writes to the running maps, and other threads only communicate via messages, a simple in-memory map with mutex suffices. No need for a lock-free double-buffer.
