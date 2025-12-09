Core HTTP demo built on the legacy `core` networking stack.

Build
-----
```
cd examples/core_demo
make
```

Run
---
1. Start the server (uses `listen_thread`, `base_connect`, and `CRxBaseNetThread`):
   ```
   ./bin/core_http_server
   ```
   The process logs timer ticks and demo cross-thread messages to `logs/`.

2. From another terminal, run the multi-threaded client to generate concurrent requests:
   ```
   ./bin/core_http_client
   ```
   Each thread issues a GET request and prints the raw HTTP response.

The server responds with JSON payloads describing the handling worker, and internal timers emit log entries every 2 seconds demonstrating the legacy timeout flow. Cross-thread messaging is exercised at startup when worker 0 posts a message to worker 1.
