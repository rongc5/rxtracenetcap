Libevent + pthread thread pool HTTP example

## Architecture
- **Main thread**: Runs libevent HTTP server, accepts connections and HTTP requests
- **Worker threads**: pthread-based pool handles business logic processing
- **Thread safety**: Workers use `event_base_once()` to schedule replies back on main event loop
- **Static linking**: Uses libevent static libraries for standalone deployment

## Thread Model
This example demonstrates a **single-threaded event loop + worker thread pool** pattern:
1. Main thread runs `event_base_dispatch()` and handles all network I/O
2. HTTP requests are queued to worker threads for processing
3. Worker threads never touch libevent objects directly
4. Response scheduling happens thread-safely via the main event loop

## Build Requirements

### libevent compilation
First, build libevent with static libraries:
```bash
cd libevent-2.1.12-stable
./configure --enable-static --disable-shared --enable-function-sections
make
```

Recommended configure options:
- `--enable-static --disable-shared`: Build only static libraries
- `--enable-function-sections`: Optimize static library for smaller binaries with --gc-sections
- `--disable-openssl`: Skip OpenSSL support if not needed
- `--disable-samples`: Skip building sample programs

### Build (Make)
```bash
make
```

### Build (CMake)
```bash
cmake -S . -B build && cmake --build build
```

Both build systems automatically use the local static libevent build from `../../libevent-2.1.12-stable/.libs/`.

Run
- `./http_server 0.0.0.0 8080 4`
  - args: <bind_address> <port> <threads>

Test
- `curl "http://127.0.0.1:8080/echo?x=1" -d 'hello'`

