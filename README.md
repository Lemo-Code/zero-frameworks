# Zero Framework

> 高性能 C++ 协程服务器框架 — 网络库 · HTTP/WebSocket · KV 存储引擎 · RPC · Sentinel 高可用

Zero 是一个基于 **epoll + M:N 非对称协程** 的 C++14 服务器端框架，提供日志库、协程网络库、HTTP/WebSocket 服务、Redis 协议兼容的 KV 存储引擎、RPC 框架、Sentinel 高可用集群、安全模块等开箱即用的基础设施。

协程框架思路源自 [Sylar](https://github.com/sylar-yin/sylar)，在协程栈分配、I/O 超时机制、调度器本地队列、连接亲和性等方面做了大量重构与性能优化。

### vs Sylar 性能对比

> 相同条件：ARM64 / 500 连接 / 64B payload / 零错误

| 协议 | Sylar 基线 | Zero Framework | 提升 |
|------|-----------|----------------|------|
| HTTP Echo | ~300,000 req/s | **439,692 req/s** | **+46.6%** |
| KV GET   | ~300,000 rps   | **382,838 rps**   | **+27.6%** |
| KV SET   | ~300,000 rps   | **300,590 rps**   | 持平 |

---

## 目录

- [性能一览](#性能一览)
- [快速开始](#快速开始)
- [架构设计](#架构设计)
- [1. 日志库](#1-日志库)
- [2. 协程与调度](#2-协程与调度)
- [3. 网络库](#3-网络库)
- [4. HTTP 模块](#4-http-模块)
- [5. WebSocket 模块](#5-websocket-模块)
- [6. KV 存储引擎](#6-kv-存储引擎)
- [7. RPC 框架](#7-rpc-框架)
- [8. Sentinel 高可用](#8-sentinel-高可用)
- [9. DB/ORM](#9-dborm)
- [10. 安全模块](#10-安全模块)
- [11. 配置中心](#11-配置中心)
- [12. 服务注册发现](#12-服务注册发现)
- [13. 工具库](#13-工具库)
- [测试](#测试)
- [bench2 QPS 压测工具](#bench2-qps-压测工具)
- [main/ — Spring Boot 风格启动入口](#main--spring-boot-风格启动入口)
- [目录结构](#目录结构)
- [API 速查](#api-速查)
- [配置参考](#配置参考)

---

## 性能一览

> 测试环境：ARM64 (Kunpeng 920) / Linux 6.8 / GCC 13.3 / Release 编译  
> bench2 压测工具：500 连接 × 15 秒，64B payload，零错误

### 多线程 QPS 缩放 —— 总览

| Workers | TCP Echo | HTTP Echo | KV GET | KV SET |
|---------|----------|-----------|--------|--------|
| **2**   | 440,440  | 324,595   | 273,915 | 227,415 |
| **4**   | 618,556  | 392,899   | 370,738 | 265,960 |
| **6**   | 611,380  | 412,332   | 382,838 | 281,362 |
| **8**   | 614,091  | 439,692   | 380,385 | 300,590 |

> **结论：** TCP 在 4 线程即达峰值（~62 万 QPS）；HTTP 线性缩放至 8 线程（~44 万 QPS）；KV GET 在 4-6 线程最优（~38 万 rps），KV SET 持续受益于更多线程（8 线程 ~30 万 rps）。推荐生产环境使用 **4-6 worker 线程**。

### TCP Echo

| Workers | QPS | 吞吐量 |
|---------|-----|--------|
| 2 | 440,440 req/s | 451 Mbps |
| 4 | **618,556 req/s** | 633 Mbps |
| 6 | 611,380 req/s | 626 Mbps |
| 8 | 614,091 req/s | 629 Mbps |

### HTTP Echo (POST)

| Workers | QPS | 吞吐量 |
|---------|-----|--------|
| 2 | 324,595 req/s | 654 Mbps |
| 4 | 392,899 req/s | 792 Mbps |
| 6 | 412,332 req/s | 831 Mbps |
| 8 | **439,692 req/s** | 886 Mbps |

### KV (Redis 协议)

| Workers | GET (rps) | SET (rps) | 说明 |
|---------|-----------|-----------|------|
| 2 | 273,915   | 227,415   | |
| 4 | 370,738   | 265,960   | |
| 6 | **382,838** | 281,362 | GET 峰值 |
| 8 | 380,385   | **300,590** | SET 持续提升 |

> 所有测试 0 errors。GET 吞吐在 6 线程达到峰值（~38 万 rps），SET 可线性缩放至 8 线程。

---

## 快速开始

### 环境要求

| 依赖 | 最低版本 | 说明 |
|------|----------|------|
| C++ 编译器 | GCC 7+ / Clang 10+ | 需要 C++14 |
| CMake | 3.10+ | 构建系统 |
| Boost | 1.60+ | 头文件库 |
| OpenSSL | 1.1+ | 加密/TLS |
| yaml-cpp | 0.5+ | YAML 配置解析 |
| jsoncpp | 1.0+ | JSON 序列化 |
| zlib | 1.2+ | 流压缩 |
| Protobuf | 3.0+ | RPC 序列化（可选） |
| LuaJIT | 2.0+ | Lua 脚本（可选） |
| GTest | 1.10+ | 单元测试（可选） |
| MySQL Client | 5.7+ | DB/ORM（可选） |

### 安装依赖 (Ubuntu/Debian)

```bash
sudo apt-get install -y build-essential cmake \
  libboost-all-dev libjsoncpp-dev libyaml-cpp-dev \
  libssl-dev zlib1g-dev libluajit-5.1-dev \
  libprotobuf-dev protobuf-compiler \
  libgtest-dev libgmock-dev
```

### 编译

```bash
cd zero-framework

# Release 编译
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Debug 编译（含调试符号）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j$(nproc)
```

### 编译产物

| 文件 | 说明 |
|------|------|
| `lib/libzero.so` | Zero 框架共享库 |
| `bin/kv_server` | KV 服务（Redis 协议兼容） |
| `bin/sentinel_server` | Sentinel 高可用服务 |
| `bin/echo_server` | TCP Echo 服务器 |
| `bin/bench_server` | TCP Echo 压测服务器 |
| `bin/http_bench_server` | HTTP Echo 服务器 |
| `bin/ws_bench_server` | WebSocket Echo 服务器 |
| `bin/tcp_echo_server` | bench2 - TCP Echo 服务器 |
| `bin/http_echo_server` | bench2 - HTTP Echo 服务器 |
| `bin/ws_echo_server` | bench2 - WebSocket Echo 服务器 |
| `bin/tcp_echo_client` | bench2 - TCP QPS 客户端 |
| `bin/http_echo_client` | bench2 - HTTP QPS 客户端 |
| `bin/ws_echo_client` | bench2 - WebSocket QPS 客户端 |
| `bin/kv_bench_client` | bench2 - KV Redis 协议 QPS 客户端 |
| `bin/qps_client` | TCP QPS 客户端 |
| `bin/http_qps_client` | HTTP QPS 客户端 |
| `bin/ws_qps_client` | WebSocket QPS 客户端 |
| `bin/log_demo` | 日志功能演示 |
| `bin/log_qps_matrix` | 日志 QPS 矩阵工具 |
| `bin/zero_app` | 🆕 统一应用 (HTTP+KV+RPC+WS) |
| `bin/http_server` | 🆕 Spring Boot 风格 HTTP 服务器 |
| `bin/kv_main` | 🆕 Spring Boot 风格 KV 服务器 |
| `bin/test_*` | 48 套单元测试可执行文件 |

### 运行示例

```bash
# ============ Spring Boot 风格启动 (推荐) ============
# HTTP 服务器（6 worker 线程，YAML 配置驱动）
./bin/http_server
curl http://localhost:8080/healthz

# 命令行覆盖端口和线程
./bin/http_server --app.workers=8 --servers.http.address=0.0.0.0:9090

# KV 服务器 + AOF 持久化 + HTTP 管理接口
./bin/kv_main --servers.kv.aof_enabled=1 --servers.kv.http_admin_port=16379
redis-cli -p 6379 PING

# 统一应用（同时启动 HTTP + KV）
./bin/zero_app --servers.kv.enabled=1

# 守护进程模式
./bin/http_server -d --app.pid_file=/var/run/http-server.pid

# ============ bench2 轻量压测工具 ============
# KV 服务（Redis 协议兼容，redis-cli 直连）
./bin/kv_server -p 6379 -w 4
redis-cli -p 6379 SET hello world

# HTTP Echo 服务器
./bin/http_echo_server -p 8080 -w 4
curl -X POST -d "hello" http://localhost:8080/echo

# TCP Echo 服务器
./bin/tcp_echo_server -p 8020 -w 4

# QPS 压测（500 并发，15 秒）
./bin/tcp_echo_client -p 8020 -c 500 -d 15 -s 64
./bin/http_echo_client -p 8080 -c 500 -d 15 -s 64
./bin/kv_bench_client -p 6379 -c 500 -d 15 -t GET

# 运行全部单元测试
ctest --test-dir build -j$(nproc)
```

---

## 架构设计

### 整体分层架构

```
┌──────────────────────────────────────────────────────────────────┐
│              应用层 — redis-cli / HTTP / WebSocket / RPC          │
└─────────────────────────────┬────────────────────────────────────┘
                              │ TCP
┌─────────────────────────────▼────────────────────────────────────┐
│      协议层 — HTTP Parser / WS Frame / RESP Codec / RPC Frame     │
│        (Ragel HTTP 解析器 / RESP 流式解析器 / Protobuf)           │
└─────────────────────────────┬────────────────────────────────────┘
                              │
┌─────────────────────────────▼────────────────────────────────────┐
│      服务层 — HttpServer / WSServer / KvServer / RpcServer       │
│        (TcpServer 子类，每连接一个协程)                             │
└─────────────────────────────┬────────────────────────────────────┘
                              │
┌─────────────────────────────▼────────────────────────────────────┐
│      业务逻辑层                                                    │
│  ┌──────────┬──────────┬──────────┬──────────┬──────────────┐    │
│  │  HTTP    │  WS      │  KV      │  RPC     │  Sentinel     │    │
│  │  Servlet │  Servlet │  Command │  Service │  Manager      │    │
│  │  +MW     │          │  Handler │          │  (Raft-like)  │    │
│  └──────────┴──────────┴──────────┴──────────┴──────────────┘    │
└─────────────────────────────┬────────────────────────────────────┘
                              │
┌─────────────────────────────▼────────────────────────────────────┐
│      协程层 — Scheduler (M:N) + IOManager (epoll) + Timer         │
│      每线程一个本地队列，work-stealing，内联定时器                    │
└─────────────────────────────┬────────────────────────────────────┘
                              │
┌─────────────────────────────▼────────────────────────────────────┐
│      基础设施 — Log / Config / Fiber / Mutex / Hook              │
└──────────────────────────────────────────────────────────────────┘
```

### 协程模型

```
IOManager (继承 Scheduler + TimerManager)
  ├── epoll 事件循环 (m_epfd)
  │   ├── READ/WRITE 事件注册
  │   ├── 内联定时器 (epoll_wait timeout = 最近到期 timer)
  │   └── tickle 唤醒机制 (eventfd)
  │
  └── Scheduler (M:N 协程调度)
      ├── Worker 0 — Fiber pool (thread_local m_fibers)
      ├── Worker 1 — Fiber pool
      ├── Worker 2 — Fiber pool
      └── Worker 3 — Fiber pool
```

每个 TCP 连接分配一个协程，连接按 fd 哈希绑定到固定 worker 线程（连接亲和性），减少锁竞争和缓存失效。

### KV 请求处理流程

```
TCP 接收 → RespReader::tryParse (流式半包解析，memchr 加速)
  → CommandDispatch::dispatch (O(1) 命令路由表)
    → requirepass 鉴权检查
    → 事务队列检查 (MULTI 模式)
    → 阻塞队列检查 (BLPOP 挂起)
    → Handler 执行 (per-shard 锁 + watch_versions)
      → RDB/AOF 持久化写入
      → 主从复制传播
      → SlowLog 记录
  → RespEncoder::encodeInto (预编码常量快速路径)
→ TCP 发送 (flushResponses 批量写回，支持 Pipeline)
```

---

## 1. 日志库

Zero 日志库是整个框架最核心的基础设施之一，提供丰富的 Appender、多样的格式化输出和极高的吞吐性能。

### 1.1 日志级别 (10 级)

```
TRACE = 0    — 最详细的跟踪信息
DEBUG = 1    — 调试信息
INFO  = 2    — 一般信息
WARN  = 3    — 警告
ERROR = 4    — 错误
FATAL = 5    — 致命错误（触发 assert）
ALERT = 6    — 告警（需立即处理）
CRIT  = 7    — 严重错误
NOTICE = 8   — 通知（比 INFO 重要）
EMERG = 9    — 紧急（系统不可用）
```

### 1.2 日志输出模式

| 模式 | 说明 | 适用场景 |
|------|------|----------|
| **同步模式** | 直接写入 Appender，低延迟 | 低吞吐场景、调试 |
| **异步模式** | 环形缓冲区 + 后台线程批量写入 | 高吞吐生产环境 |

### 1.3 LogAppender 输出器（16+ 种）

| Appender | 类名 | 说明 |
|----------|------|------|
| 控制台 | `StdoutLogAppender` | 标准输出，支持彩色主题 |
| 文件 | `FileLogAppender` | 普通文件写入 |
| 滚动文件 | `RollingFileLogAppender` | 按大小滚动（如 100MB 一个文件） |
| 时间滚动 | `TimeRollingFileLogAppender` | 按时间滚动（如每小时/每天） |
| syslog | `SyslogAppender` | 系统日志 |
| UDP | `UDPAppender` | 网络 UDP 发送 |
| TCP | `TCPAppender` | 网络 TCP 发送 |
| 内存 | `MemoryAppender` | 内存环形缓冲（适合压测） |
| 回调 | `CallbackAppender` | 自定义回调处理 |
| Null | `NullAppender` | 丢弃所有日志（性能测试基线） |
| 异步装饰 | `AsyncAppender` | 将任意 Appender 包装为异步 |

### 1.4 LogFormatter 格式化器（30+ 种）

支持 **标准格式化** 和 **结构化格式化** 两大类：

| 格式 | 说明 |
|------|------|
| `%d{format}` | 时间戳（支持 strftime 格式） |
| `%p` | 日志级别 |
| `%t` | 线程 ID |
| `%F` | 协程 ID (Fiber ID) |
| `%N` | 线程名称 |
| `%f` | 源文件名 |
| `%l` | 行号 |
| `%m` | 日志消息 |
| `%n` | 换行符 |
| `%T` | Tab 字符 |
| `%c` | Logger 名称 |
| `%C` | 类名/分类名 |
| `%X{key}` | MDC 上下文变量 |
| `%v` | 日志原始内容 |
| **JSON** | 结构化 JSON 格式化器 |
| **XML** | XML 格式化器 |
| **LTSV** | Labeled Tab-Separated Values |
| **CEF** | Common Event Format（SIEM 兼容） |

### 1.5 LogFilter 过滤器（5 种）

| 过滤器 | 说明 |
|--------|------|
| `LevelFilter` | 按日志级别过滤 |
| `CategoryFilter` | 按分类名过滤 |
| `RateLimitFilter` | 令牌桶限速过滤器 |
| `SamplingFilter` | 采样过滤器（如 10% 采样） |
| `TimeRangeFilter` | 按时间段过滤 |

### 1.6 日志上下文

| 功能 | API | 说明 |
|------|-----|------|
| **MDC** | `LogContext::put(key, val)` / `LogContext::get(key)` | Mapped Diagnostic Context — 线程/协程级 key-value |
| **NDC** | `LogContext::push(msg)` / `LogContext::pop()` | Nested Diagnostic Context — 嵌套消息栈 |
| **Trace ID** | `TraceContext::generateTraceId()` / `TraceContext::getCurrent()` | 分布式追踪 ID，支持跨协程传播 |

### 1.7 日志降级保护

当日志产生速度超过写出速度时，启用降级保护：

```
环形缓冲区 (Ring Buffer)
  ├── 正常模式: 生产者写入 → 消费者批量写出
  ├── 缓冲区满 80%: 丢弃 DEBUG/TRACE 级别
  ├── 缓冲区满 90%: 仅保留 WARN+ 级别
  └── 缓冲区满 100%: 非阻塞丢弃，记录丢弃计数
```

### 1.8 日志配置（YAML）

```yaml
logs:
  - name: root
    level: INFO
    appenders:
      - type: stdout
        pattern: "%d{%Y-%m-%d %H:%M:%S}%T%t%T%N%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"
        color: auto
      - type: file
        file: /var/log/zero/app.log
        rolling: true
        max_size: 104857600  # 100MB
      - type: async
        appender:
          type: file
          file: /var/log/zero/async.log
        buffer_size: 65536
        flush_interval_ms: 100
```

### 1.9 日志使用示例

```cpp
#include "zero/core/log/log.h"

// 声明 logger
static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");
static zero::Logger::ptr g_kv_logger = ZERO_LOG_NAME("kv");

// 流式日志
ZERO_LOG_INFO(g_logger) << "Server started on port " << 8080;
ZERO_LOG_DEBUG(g_logger) << "Connection from " << addr << " established";

// 格式化日志
ZERO_LOG_FMT_WARN(g_logger, "Request from %s timeout after %d ms", ip, ms);

// MDC 上下文
zero::LogContext::put("request_id", "req-12345");
ZERO_LOG_INFO(g_logger) << "Processing request";
zero::LogContext::remove("request_id");

// Trace ID
std::string traceId = zero::TraceContext::generateTraceId();
zero::TraceContext ctx(traceId);
zero::TraceContext::setCurrent(&ctx);
ZERO_LOG_INFO(g_logger) << "Tracing enabled";
```

### 1.10 日志性能

使用 `log_qps_matrix` 工具测试（NullAppender 为吞吐基线）：

| 配置 | QPS |
|------|-----|
| NullAppender 同步 1 线程 | 183.6 万 msg/s |
| NullAppender 同步 8 线程 | **410.9 万 msg/s** |
| NullAppender 异步 4 线程 | 288.2 万 msg/s |
| FileAppender 异步 4 线程 | 266.1 万 msg/s |
| FileAppender 异步 8 线程 | **270.9 万 msg/s** |

---

## 2. 协程与调度

### 2.1 Fiber（协程）

非对称协程模型（asymmetric coroutine），每个协程独立栈空间：

```cpp
#include "zero/core/concurrency/fiber.h"

// API
class Fiber {
public:
    typedef std::shared_ptr<Fiber> ptr;

    // 创建协程（指定回调和栈大小）
    Fiber(std::function<void()> cb, size_t stacksize = 0, bool use_caller = false);

    // 协程控制
    void swapIn();                                    // 切入
    void swapOut();                                   // 切出
    void reset(std::function<void()> cb);             // 重置回调（复用协程）
    void call();                                      // 在主协程中执行

    // 静态方法
    static Fiber::ptr GetThis();                      // 获取当前协程
    static uint64_t GetFiberId();                     // 获取当前协程 ID
    static uint64_t TotalFibers();                    // 总协程数
    static void YieldToReady();                       // 切回就绪队列
    static void YieldToHold();                        // 切出并挂起
};
```

**栈分配策略：** 使用 `mmap + MAP_GROWSDOWN + guard page`，按需分配物理内存，栈溢出触发 SIGSEGV 被 guard page 捕获。协程销毁后栈放入 freelist 复用，避免频繁 mmap/munmap。

### 2.2 Scheduler（调度器）

M:N 协程调度器，每个线程维护独立的协程队列：

```cpp
class Scheduler {
public:
    Scheduler(size_t threads = 1, bool use_caller = true, const std::string& name = "");

    void start();                                     // 启动线程池
    void stop();                                      // 优雅停止

    // 调度协程/回调
    template<class FiberOrCb>
    void schedule(FiberOrCb fc, int thread = -1);     // -1 表示任意线程

    // 批量调度
    template<class InputIterator>
    void schedule(InputIterator begin, InputIterator end);

    static Scheduler* GetThis();                      // 获取当前调度器
};
```

**调度策略：**
- 每个线程维护独立的 `thread_local` 协程队列，减少锁竞争
- 调度新协程时选择队列最短的线程（负载均衡）
- 空闲线程可 work-stealing 从其他线程队列取协程

### 2.3 IOManager（I/O 管理器）

基于 epoll 的事件循环，继承 Scheduler + TimerManager：

```cpp
class IOManager : public Scheduler, public TimerManager {
public:
    IOManager(size_t threads = 1, bool use_caller = true, const std::string& name = "");

    // 事件注册
    enum Event { READ = 0x1, WRITE = 0x4 };
    int addEvent(int fd, Event event, std::function<void()> cb = nullptr);
    bool delEvent(int fd, Event event);
    bool cancelEvent(int fd, Event event);
    bool cancelAll(int fd);                           // 取消 fd 全部事件

    static IOManager* GetThis();

    // 定时器（继承自 TimerManager）
    Timer::ptr addTimer(uint64_t ms, std::function<void()> cb,
                        bool recurring = false);      // 添加定时器
};
```

**内联定时器：** epoll_wait timeout 设置为最近到期的 timer 时间，避免额外线程管理定时器。添加更早的 timer 时自动 tickle 唤醒 epoll_wait。

### 2.4 Timer（定时器）

```cpp
class Timer {
public:
    typedef std::shared_ptr<Timer> ptr;

    bool cancel();                                    // 取消定时器
    bool refresh();                                   // 刷新（重新开始计时）
    bool reset(uint64_t ms, bool from_now = true);    // 重置间隔
};
```

### 2.5 Hook（系统调用劫持）

对 libc 标准 I/O 函数进行 hook，使同步阻塞调用在协程中自动变为异步非阻塞：

| 被 Hook 的函数 |
|---------------|
| `sleep`, `usleep`, `nanosleep` |
| `socket`, `connect`, `accept` |
| `read`, `readv`, `recv`, `recvfrom`, `recvmsg` |
| `write`, `writev`, `send`, `sendto`, `sendmsg` |
| `close`, `fcntl`, `ioctl` |
| `getsockopt`, `setsockopt` |

```cpp
#include "zero/core/io/hook.h"

// 开启/关闭 hook（默认关闭）
zero::set_hook_enable(true);

// 带超时的 connect
int fd = zero::connect_with_timeout(fd, addr, addrlen, 5000);
```

### 2.6 锁机制

```cpp
#include "zero/core/concurrency/mutex.h"

// 互斥锁
zero::Mutex mtx;
zero::Mutex::Lock lock(mtx);               // RAII 加锁

// 读写锁
zero::RWMutex rwmtx;
zero::RWMutex::ReadLock rlock(rwmtx);      // 读锁
zero::RWMutex::WriteLock wlock(rwmtx);     // 写锁

// 自旋锁
zero::Spinlock spin;

// CAS 锁（无锁 CAS 操作）
zero::CASLock cas;

// 协程信号量（阻塞当前协程而非线程）
zero::FiberSemaphore sem(10);             // 初始 10 个许可
sem.wait();                                // 协程级阻塞
sem.notify();                              // 唤醒一个等待协程

// 线程信号量
zero::Semaphore sem(5);
```

---

## 3. 网络库

### 3.1 TcpServer

```cpp
#include "zero/net/tcp/tcp_server.h"

class TcpServer : public std::enable_shared_from_this<TcpServer> {
public:
    typedef std::shared_ptr<TcpServer> ptr;

    virtual bool bind(Address::ptr addr, bool ssl = false);
    virtual bool bind(const std::vector<Address::ptr>& addrs,
                      std::vector<Address::ptr>& fails);
    virtual void start();
    virtual void stop();

    // 子类重写此方法处理客户端连接
    virtual void handleClient(Socket::ptr client);

    uint64_t getRecvTimeout() const;
    uint64_t getSendTimeout() const;
    void setName(const std::string& v);
};
```

**连接亲和性：** 新连接按 `fd % worker_count` 分配到固定 worker 线程，同一连接的后续 I/O 事件也在同一线程处理，充分利用 CPU 缓存。

### 3.2 Socket

```cpp
#include "zero/core/io/socket.h"

class Socket {
public:
    typedef std::shared_ptr<Socket> ptr;

    // 创建
    static Socket::ptr CreateTCP(sa_family_t family = AF_INET);
    static Socket::ptr CreateUDP(sa_family_t family = AF_INET);

    // TCP 操作
    virtual bool connect(Address::ptr addr);
    virtual bool bind(Address::ptr addr);
    virtual bool listen(int backlog = SOMAXCONN);
    virtual Socket::ptr accept();

    // I/O（协程友好）
    virtual int send(const void* buffer, size_t length);
    virtual int recv(void* buffer, size_t length);
    virtual int sendTo(const void* buffer, size_t length, Address::ptr to);
    virtual int recvFrom(void* buffer, size_t length, Address::ptr from);

    // 属性
    virtual void close();
    bool isConnected() const;
    Address::ptr getRemoteAddress();
    Address::ptr getLocalAddress();
    void setSendTimeout(uint64_t v);
    void setRecvTimeout(uint64_t v);

    // SSL
    void setSSL(bool v);
    bool isSSL() const;
};
```

### 3.3 ByteArray（零拷贝字节流）

```cpp
class ByteArray {
public:
    typedef std::shared_ptr<ByteArray> ptr;

    // 写入（可变长编码）
    void writeFint8(int8_t v);
    void writeFint16(int16_t v);
    void writeFint32(int32_t v);
    void writeFint64(int64_t v);
    void writeStringF16(const std::string& v);
    void writeStringF32(const std::string& v);

    // 读取
    int8_t readFint8();
    int16_t readFint16();
    int32_t readFint32();
    int64_t readFint64();
    std::string readStringF16();

    // 位置管理
    size_t getReadPosition() const;
    void setReadPosition(size_t v);
    size_t getWritePosition() const;
    size_t getReadSize() const;
};
```

### 3.4 Address（网络地址）

```cpp
class Address {
public:
    typedef std::shared_ptr<Address> ptr;

    static Address::ptr Create(const sockaddr* addr, socklen_t len);
    static bool Lookup(std::vector<Address::ptr>& result,
                       const std::string& host, int family = AF_UNSPEC);
    static Address::ptr LookupAny(const std::string& host);
    static Address::ptr LookupAnyIPAddress(const std::string& host);

    int getFamily() const;
    std::string toString() const;
    int getPort() const;
    void setPort(int v);
    std::string getIP() const;
    std::string getBroadcastAddress(uint32_t prefix_len) const;
    std::string getNetworkAddress(uint32_t prefix_len) const;
    std::string getSubnetMask(uint32_t prefix_len) const;

    // CIDR 子网操作
    static Address::ptr GetInterfaceAddress(const std::string& iface);
    static bool GetInterfaceAddresses(
        std::vector<std::pair<std::string, Address::ptr>>& result);
};
```

### 3.5 Stream 体系

```
Stream (抽象基类)
├── read/write 接口
├── readFixSize/writeFixSize — 定长读写
│
├── SocketStream (TCP Socket 流)
│   └── SSL SocketStream
│
├── ZlibStream (压缩流)
│   ├── ZlibOutputStream (压缩写入)
│   └── ZlibInputStream (解压读取)
│
└── AsyncStream (异步流)
```

---

## 4. HTTP 模块

完整的 HTTP/1.1 协议支持，Ragel 状态机解析器，可嵌入任何 Zero 服务。

### 4.1 HttpServer

```cpp
#include "zero/http/http_server.h"

class HttpServer : public TcpServer {
public:
    typedef std::shared_ptr<HttpServer> ptr;

    HttpServer(bool keepalive = false);

    ServletDispatch::ptr getServletDispatch() const;
    void setServletDispatch(ServletDispatch::ptr v);

    // 添加固定路由（高性能精确匹配）
    void addFixedRoute(const std::string& path,
                       std::function<int32_t(HttpRequest::ptr, HttpResponse::ptr,
                                             HttpSession::ptr)> handler);
};
```

### 4.2 Servlet 路由分发

| 路由方式 | 说明 |
|----------|------|
| 精确匹配 | `/api/user/login` 精确路径匹配 |
| 通配符匹配 | `/api/user/*` 前缀通配 |
| 优先级匹配 | 精确 > 通配符 |

### 4.3 内置 Servlet

| Servlet | 路径 | 说明 |
|---------|------|------|
| `ConfigServlet` | `/config` | 查看/修改运行时配置（JSON） |
| `StatusServlet` | `/status` | 框架运行状态（版本、连接数、内存） |
| `HealthzServlet` | `/healthz` | K8s 健康检查 |
| `ReadyzServlet` | `/readyz` | K8s 就绪检查 |
| `LivezServlet` | `/livez` | K8s 存活检查 |

### 4.4 Middleware 中间件（7 种）

所有中间件继承 `Middleware` 基类，实现 `handle(req, rsp, session) → int32_t`。

#### AccessLogMiddleware
```cpp
AccessLogMiddleware::create(format, output_stream);
// 记录每个请求的方法、路径、状态码、耗时
```

#### RateLimitMiddleware
```cpp
// 三种算法
enum RateLimitAlgorithm { TokenBucket, SlidingWindow, LeakyBucket };

auto rl = TokenBucketRateLimiter::create(1000, 100);  // 1000 tokens, 100/s refill
auto mw = RateLimitMiddleware::create(rl);
```

#### CORSMiddleware
```cpp
CORSConfig cfg;
cfg.allowOrigins = {"https://example.com"};
cfg.allowMethods = {"GET", "POST"};
cfg.allowHeaders = {"Content-Type", "Authorization"};
cfg.maxAge = 3600;
auto cors = CORSMiddleware::create(cfg);
```

#### CircuitBreakerMiddleware
```cpp
CircuitBreakerConfig cfg;
cfg.failureThreshold = 5;      // 5 次失败开启熔断
cfg.recoveryTimeoutMs = 30000; // 30 秒后半开
cfg.halfOpenMaxRequests = 3;   // 半开时允许 3 个试探请求
auto cb = CircuitBreakerMiddleware::create(cfg);
```

#### TimeoutMiddleware
```cpp
auto tmw = TimeoutMiddleware::create(5000); // 5 秒超时
```

#### CacheMiddleware
```cpp
auto cmw = CacheMiddleware::create(cacheInstance, ttlSeconds);
```

### 4.5 API 网关

```cpp
#include "zero/http/gateway/gateway_servlet.h"

GatewayServlet gw;
// 路由注册
gw.addRoute("/api/user", userHandler);
gw.addRoute("/api/order", orderHandler);

// 上游代理
#include "zero/http/gateway/http_reverse_proxy.h"
HttpReverseProxy proxy("backend.example.com", 8080);
```

### 4.6 HTTP/2

支持 HTTP/2 (h2c) 协议，包括 HPACK 头部压缩：

```cpp
#include "zero/http/http2.h"
#include "zero/http/hpack.h"

// HTTP/2 帧类型
enum FrameType {
    DATA = 0x0, HEADERS = 0x1, PRIORITY = 0x2,
    RST_STREAM = 0x3, SETTINGS = 0x4, PUSH_PROMISE = 0x5,
    PING = 0x6, GOAWAY = 0x7, WINDOW_UPDATE = 0x8,
    CONTINUATION = 0x9
};
```

### 4.7 Session 管理

```cpp
#include "zero/http/session_data.h"

// Cookie 读写
class SessionData {
    std::string get(const std::string& key);
    void set(const std::string& key, const std::string& value);
    void del(const std::string& key);
};
```

### 4.8 HTTP 使用示例

```cpp
#include "zero/http/http_server.h"
#include "zero/core/io/iomanager.h"

void run() {
    auto server = std::make_shared<zero::http::HttpServer>(true); // keepalive

    // 注册路由
    server->getServletDispatch()->addServlet("/api/hello",
        [](auto req, auto rsp, auto session) {
            rsp->setStatus(zero::http::HttpStatus::OK);
            rsp->setHeader("Content-Type", "application/json");
            rsp->setBody("{\"msg\":\"Hello, Zero!\"}");
            return 0;
        });

    // 通配符路由
    server->getServletDispatch()->addGlobServlet("/api/user/*",
        [](auto req, auto rsp, auto session) {
            std::string id = req->getPath().substr(10);  // 提取 /api/user/ 后的部分
            rsp->setBody("User: " + id);
            return 0;
        });

    // 添加中间件
    server->getServletDispatch()->addMiddleware(
        AccessLogMiddleware::create("%m %U %s %Dms",
            std::make_shared<std::ofstream>("/var/log/zero/access.log")));

    auto addr = zero::Address::LookupAny("0.0.0.0:8080");
    server->bind(addr);
    server->start();
}

int main() {
    zero::IOManager iom(4);
    iom.schedule(run);
    return 0;
}
```

---

## 5. WebSocket 模块

基于 HTTP Upgrade 实现的 WebSocket 服务，支持文本帧 (opcode=1) 和二进制帧 (opcode=2)。

### 5.1 WSServer

```cpp
#include "zero/http/ws_server.h"
#include "zero/http/ws_servlet.h"

int main() {
    zero::IOManager iom(4);
    iom.schedule([]() {
        auto server = std::make_shared<zero::http::WSServer>();

        server->getWSServletDispatch()->addServlet("/chat",
            [](zero::http::HttpRequest::ptr req,
               zero::http::WSFrameMessage::ptr msg,
               zero::http::WSSession::ptr session) -> int32_t {
                // 收到消息，广播给所有客户端
                session->sendMessage(msg);
                return 0;
            });

        server->getWSServletDispatch()->addServlet("/echo",
            [](auto req, auto msg, auto session) {
                session->sendMessage(msg);  // 原样返回
                return 0;
            });

        auto addr = zero::Address::LookupAny("0.0.0.0:8024");
        server->bind(addr);
        server->start();
    });
    return 0;
}
```

### 5.2 WSFrameMessage

```cpp
class WSFrameMessage {
public:
    typedef std::shared_ptr<WSFrameMessage> ptr;

    int getOpcode() const;         // 1=text, 2=binary, 8=close, 9=ping, 10=pong
    std::string getData() const;   // 帧负载
};
```

---

## 6. KV 存储引擎

基于 Zero 协程框架实现的 **Redis 协议兼容**内存数据库。标准 Redis 客户端（`redis-cli`、Jedis、go-redis、StackExchange.Redis）可直连使用。

### 6.1 功能矩阵

| 类别 | 功能 | 命令数 |
|------|------|--------|
| **数据类型** | String, Hash, List, Set, Sorted Set, Stream | 6 种 |
| **TTL** | EXPIRE/PEXPIRE/EXPIREAT/PEXPIREAT/PERSIST/TTL/PTTL + 主动过期扫描 | 7 条 |
| **Lua 脚本** | EVAL/EVALSHA/SCRIPT LOAD/EXISTS/FLUSH/KILL，LuaJIT 5.1，redis.call/pcall | 6 条 |
| **事务** | MULTI/EXEC/DISCARD + WATCH/UNWATCH 乐观锁 | 5 条 |
| **阻塞队列** | BLPOP/BRPOP/BRPOPLPUSH（协程挂起，非 busy-loop） | 3 条 |
| **Pub/Sub** | SUBSCRIBE/UNSUBSCRIBE/PSUBSCRIBE/PUNSUBSCRIBE/PUBLISH（glob 模式匹配） | 5 条 |
| **持久化** | RDB v2 二进制快照 + AOF 追加日志 + BGREWRITEAOF | — |
| **内存管理** | maxmemory + 6 种淘汰策略（allkeys/volatile × lru/random/ttl + noeviction） | — |
| **主从复制** | SLAVEOF/PSYNC（全量+增量）/ROLE/READONLY | 4 条 |
| **可观测** | INFO/SLOWLOG/CLIENT LIST/CONFIG GET/SET | 5 条 |
| **安全** | AUTH (requirepass) | 1 条 |
| **管理** | HTTP Admin API (/redis/info, /redis/ping, /redis/save 等) | — |

### 6.2 完整命令清单

#### String (17 条)
`GET` `SET` `SETNX` `SETEX` `GETSET` `GETDEL` `GETEX` `MGET` `MSET` `MSETNX` `INCR` `DECR` `INCRBY` `DECRBY` `INCRBYFLOAT` `APPEND` `STRLEN`

#### Hash (13 条)
`HSET` `HSETNX` `HGET` `HGETALL` `HDEL` `HEXISTS` `HLEN` `HMSET` `HMGET` `HKEYS` `HVALS` `HINCRBY` `HSCAN`

#### List (15 条)
`LPUSH` `RPUSH` `LPOP` `RPOP` `BLPOP` `BRPOP` `BRPOPLPUSH` `LLEN` `LRANGE` `LINDEX` `LSET` `LREM` `LTRIM` `LINSERT` `RPOPLPUSH`

#### Set (13 条)
`SADD` `SREM` `SMEMBERS` `SCARD` `SISMEMBER` `SPOP` `SINTER` `SUNION` `SDIFF` `SINTERSTORE` `SUNIONSTORE` `SDIFFSTORE` `SSCAN`

#### Sorted Set (15 条)
`ZADD` `ZREM` `ZSCORE` `ZCARD` `ZRANGE` `ZREVRANGE` `ZRANGEBYSCORE` `ZREVRANGEBYSCORE` `ZCOUNT` `ZINCRBY` `ZRANK` `ZREVRANK` `ZPOPMIN` `ZPOPMAX` `ZSCAN`

#### Stream (12 条)
`XADD` `XLEN` `XRANGE` `XREVRANGE` `XREAD` `XDEL` `XGROUP CREATE/DESTROY` `XREADGROUP` `XACK` `XTRIM` `XPENDING` `XCLAIM`

#### Key / Generic (18 条)
`KEYS` `SCAN` `TYPE` `EXISTS` `DEL` `UNLINK` `RENAME` `RENAMENX` `DBSIZE` `FLUSHDB` `FLUSHALL` `DUMP` `RESTORE` `PERSIST` `EXPIRE` `EXPIREAT` `PEXPIRE` `PEXPIREAT` `TTL` `PTTL` `RANDOMKEY`

#### Connection / Server (11 条)
`PING` `ECHO` `QUIT` `AUTH` `SELECT` `TIME` `SHUTDOWN` `INFO` `CLIENT LIST` `CLIENT KILL` `CLIENT SETNAME`

#### Lua Scripting (6 条)
`EVAL` `EVALSHA` `SCRIPT LOAD` `SCRIPT EXISTS` `SCRIPT FLUSH` `SCRIPT KILL`

#### Transactions (5 条)
`MULTI` `EXEC` `DISCARD` `WATCH` `UNWATCH`

#### Pub/Sub (5 条)
`SUBSCRIBE` `UNSUBSCRIBE` `PSUBSCRIBE` `PUNSUBSCRIBE` `PUBLISH`

#### Replication / Persistence / Admin (10 条)
`ROLE` `SLAVEOF` `PSYNC` `SAVE` `BGSAVE` `LASTSAVE` `BGREWRITEAOF` `CONFIG GET` `CONFIG SET` `SLOWLOG GET`

### 6.3 RESP 协议实现

```cpp
// 响应类型
enum class RespType {
    Null,             // $-1\r\n
    SimpleString,     // +OK\r\n
    Error,            // -ERR message\r\n
    Integer,          // :1000\r\n
    BulkString,       // $5\r\nhello\r\n
    Array             // *2\r\n...
};

// RESP 值结构
struct RespValue {
    RespType type;
    std::string bulk;           // SimpleString / Error / BulkString 内容
    int64_t integer = 0;        // Integer 值
    std::vector<RespValue> array; // Array 子元素
};

// 流式解析器
class RespReader {
    ParseStatus tryParse(const char* data, size_t len, size_t& consumed);
};
```

**解析优化：** 使用 `memchr` 扫描 `\r\n` 分界符（而非逐字节扫描），预编码常量字符串（`+OK\r\n`、`$-1\r\n`）避免重复分配。

### 6.4 KvStore API

```cpp
class KvStore {
public:
    // 基础 CRUD
    bool get(int db, const std::string& key, std::string& value, bool& found);
    void set(int db, const std::string& key, const std::string& value);
    void setex(int db, const std::string& key, const std::string& value, int64_t ttl_sec);
    int setOpt(int db, const std::string& key, const std::string& value,
               bool nx, bool xx, int64_t ex, int64_t px);  // SET 全选项
    bool del(int db, const std::string& key);
    int64_t delMany(int db, const std::vector<std::string>& keys);

    // 类型和存在性
    bool exists(int db, const std::string& key);
    int64_t existsMany(int db, const std::vector<std::string>& keys);
    std::string type(int db, const std::string& key);

    // TTL
    int64_t expire(int db, const std::string& key, int64_t seconds);
    int64_t pexpire(int db, const std::string& key, int64_t ms);
    int64_t expireAt(int db, const std::string& key, int64_t unix_sec);
    int64_t pexpireAt(int db, const std::string& key, int64_t unix_ms);
    int64_t ttl(int db, const std::string& key);
    int64_t pttl(int db, const std::string& key);
    int64_t persist(int db, const std::string& key);

    // 数值操作
    bool incrBy(int db, const std::string& key, int64_t delta, int64_t& out);

    // 数据库操作
    int64_t dbsize(int db);
    void flushdb(int db);
    void flushall();
    std::vector<std::string> keys(int db, const std::string& pattern);
    ScanResult scan(int db, uint64_t cursor, const std::string& pattern, int count);

    // 主动过期
    int64_t purgeExpiredActive(int maxScanPerDb = 20);
};
```

**Shard 设计：** KV 数据按 key 哈希分片到多个 Shard，每个 Shard 独立加锁（cache-line 对齐消除伪共享），实现高并发读写。

### 6.5 持久化

#### RDB v2
```cpp
// 二进制快照格式
// 文件头: "ZERODB" + version(2) + checksum
// 数据块: type(1B) + expire_flag(1B) + expire_ms(8B, optional)
//         + key_len(4B) + key + value_len(4B) + value
// 文件尾: EOF_MARKER(0xFF) + checksum(8B)
```

触发方式：
- `SAVE` — 同步阻塞快照
- `BGSAVE` — 后台异步快照（fork 子进程）
- 自动间隔快照 (`--save 60`)

#### AOF (Append-Only File)
```cpp
// 追加日志格式：纯文本 RESP 命令
// *3\r\n$3\r\nSET\r\n$3\r\nkey\r\n$5\r\nvalue\r\n
```

| 同步策略 | fsync 频率 | 安全性 |
|----------|-----------|--------|
| `always` | 每条写命令 | 最高（不丢数据） |
| `everysec` | 每秒一次 | 中等（最多丢 1 秒） |
| `no` | 由 OS 决定 | 最低 |

**AOF Rewrite:** `BGREWRITEAOF` 后台重写 AOF，用当前数据集的 SET 命令替代历史日志，减少文件大小。

### 6.6 主从复制

```cpp
class ReplicationManager {
public:
    enum class Role { Master, Slave };

    void setSlaveOf(const std::string& host, int port);     // 设为从节点
    void promoteToMaster();                                  // 提升为主节点

    int connectedSlaves() const;
    void propagateCommand(const RespValue& command);         // 命令传播给从节点

    // PSYNC 协议
    bool tryCollectPartial(const std::string& replid, int64_t offset,
                           std::string& out);               // 尝试增量同步
};
```

**复制流程：**
```
Slave → Master: PSYNC <replid> <offset>
  ├── 增量同步（offset 在 backlog 范围内）
  │   └── Master 发送 backlog 中 offset 之后的命令
  └── 全量同步（offset 过期或首次连接）
      ├── Master 执行 BGSAVE 生成 RDB
      ├── Master 发送 RDB 文件给 Slave
      └── Slave 加载 RDB 后继续接收增量命令
```

### 6.7 阻塞队列

```cpp
// BLPOP key1 key2 ... timeout
// 协程挂起等待，非 busy-loop 轮询
// 超时自动唤醒，新数据到达时通知对应协程

class BlockingListHub {
    void waitForPop(const std::vector<std::string>& keys,
                    std::function<void(std::string, std::string)> callback,
                    int64_t timeoutMs);
};
```

### 6.8 Lua 脚本

基于 LuaJIT 5.1，提供完整 `redis.*` API：

```lua
-- redis.call / redis.pcall
redis.call('SET', KEYS[1], ARGV[1])
local val = redis.call('GET', KEYS[1])

-- redis.log
redis.log(redis.LOG_WARNING, 'processing key: ' .. KEYS[1])

-- redis.status_reply / redis.error_reply
return redis.status_reply('OK')

-- redis.sha1hex
local sha = redis.sha1hex(script_body)
```

**安全限制：**
- 5M 指令上限（防止死循环）
- `SCRIPT KILL` 可终止运行中的脚本
- 线程安全的单 VM 实例
- 默认 per-shard 锁 → 可用 `CONFIG SET lua-global-atomic yes` 切换全局锁

### 6.9 内存管理

```
maxmemory <bytes>               # 最大内存限制（0 = 无限制）
maxmemory-policy <policy>       # 淘汰策略

淘汰策略（6 种）：
  noeviction          — 不淘汰，写满拒绝写入
  allkeys-lru         — 所有 key 中 LRU 淘汰
  allkeys-random      — 所有 key 中随机淘汰
  volatile-lru        — 有过期时间的 key 中 LRU 淘汰
  volatile-random     — 有过期时间的 key 中随机淘汰
  volatile-ttl        — 有过期时间的 key 中淘汰 TTL 最短的
```

### 6.10 CONFIG 选项

| 选项 | 默认值 | 可 SET | 说明 |
|------|--------|--------|------|
| `maxmemory` | 0（无限制） | ✅ | 最大内存字节数 |
| `maxmemory-policy` | allkeys-lru | ✅ | 淘汰策略 |
| `appendfsync` | everysec | ✅ | AOF 同步频率 |
| `requirepass` | "" | ✅ | 鉴权密码 |
| `slowlog-log-slower-than` | 10000 | ✅ | 慢查询阈值 (μs) |
| `slowlog-max-len` | 128 | ✅ | 慢查询最大条数 |
| `dbfilename` | dump.rdb | ✅ | RDB 文件名 |
| `lua-global-atomic` | no | ✅ | Lua 全局原子性开关 |
| `save` | 60 | ❌ RO | RDB 自动保存间隔 (秒) |
| `appendonly` | no | ❌ RO | AOF 开关 |
| `timeout` | 0 | ❌ RO | 客户端超时 (秒) |
| `tcp-keepalive` | 300 | ❌ RO | TCP keepalive (秒) |
| `databases` | 16 | ❌ RO | 数据库数量 |
| `dir` | ./ | ❌ RO | 工作目录 |

### 6.11 KV 启动参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `-p` | 6379 | KV 监听端口 |
| `-w` | 4 | Worker 线程数 |
| `-rdb` | ./dump.rdb | RDB 快照文件路径 |
| `--aof` | 关闭 | 启用 AOF |
| `-aof` | ./appendonly.aof | AOF 文件路径 |
| `--save` | 60 | 自动 RDB 间隔 (秒) |
| `-http` | 0（关闭） | 管理 HTTP 端口 |
| `-rpc` | 0（关闭） | RPC 端口（供 Sentinel） |
| `--rpc` combined with `--sentinel` | — | Sentinel 模式 |

### 6.12 HTTP Admin API

启用 `-http 8080` 后可用：

| 端点 | 方法 | 说明 |
|------|------|------|
| `/redis/info` | GET | 完整 INFO JSON（内存、连接、keyspace、复制状态） |
| `/redis/ping` | GET | PONG |
| `/redis/save` | POST | 触发 BGSAVE |
| `/redis/role` | GET | 复制角色（master/slave + 详情） |
| `/redis/stats` | GET | 统计 JSON（QPS、命中率、命令分布） |
| `/redis/config` | GET | 当前配置 |
| `/redis/config` | POST | 修改配置 |

### 6.13 与官方 Redis 的差异

| 特性 | Zero KV | 官方 Redis |
|------|---------|------------|
| 单机核心命令 | ✅ 130+ 条 | ✅ |
| Lua 脚本 (LuaJIT) | ✅ EVAL/EVALSHA/SCRIPT | ✅ |
| AUTH | ✅ requirepass + CONFIG 动态设置 | ✅ |
| Pipeline | ✅ ×3~5 吞吐提升 | ✅ |
| 主从复制 (PSYNC) | ✅ 全量+增量 | ✅ 完整 (含 diskless) |
| Sentinel 高可用 | ✅ Raft-like 选举 + 故障转移 | ✅ |
| RPC 框架 | ✅ TCP + Protobuf | ❌ |
| 集群 (Cluster) | ❌ | ✅ |
| ACL 多用户 | ❌ | ✅ |
| Bitmap / HyperLogLog / Geo | ❌ | ✅ |
| RESP3 | ❌ (RESP2) | ✅ |
| LFU 淘汰 | ❌ (仅 LRU 系) | ✅ |
| Module API | ❌ | ✅ |

---

## 7. RPC 框架

基于 **TCP + 4 字节大端长度前缀 + Protobuf** 的自定义 RPC 协议。

### 7.1 架构

```
┌─────────────┐                    ┌─────────────────┐
│  RpcChannel  │ ── TCP ────────── │  RpcServer      │
│  (客户端)     │   raw POSIX I/O   │  (TcpServer 子类) │
│              │   4B len + proto   │                 │
│  连接池       │                    │  协程上下文       │
│  cleanupStale │                    │  Service 分发    │
└─────────────┘                    └─────────────────┘
```

**设计特点：**
- 客户端使用真实系统调用（`connect_f`/`send_f`/`recv_f`），不经协程 hook，可在任意线程调用
- 服务端基于 TcpServer 在协程上下文中运行
- 4 字节大端长度前缀 + Protobuf payload 的帧格式
- 连接池 + 自动清理过期连接 (cleanupStale)

### 7.2 协议格式

```
┌──────────────┬─────────────────────────┐
│  Length (4B) │  Protobuf Payload (NB)   │
│  big-endian  │                          │
└──────────────┴─────────────────────────┘
```

### 7.3 Proto 服务定义

```protobuf
// kv_node.proto
service KvNodeService {
    rpc HealthCheck(HealthCheckRequest) returns (HealthCheckResponse);
    rpc ReplicaOf(ReplicaOfRequest) returns (ReplicaOfResponse);
    rpc GetNodeStatus(GetNodeStatusRequest) returns (GetNodeStatusResponse);
}

// sentinel.proto
service SentinelService {
    rpc RequestVote(VoteRequest) returns (VoteResponse);
    rpc Heartbeat(HeartbeatRequest) returns (HeartbeatResponse);
    rpc GetMaster(GetMasterRequest) returns (GetMasterResponse);
    rpc WatchMaster(WatchMasterRequest) returns (stream MasterChangeEvent);
}
```

### 7.4 KV 启动 (RPC 模式)

```bash
# 开启 RPC 端口
./bin/kv_server -p 6379 --rpc 50051

# Sentinel 模式 (RESP + RPC + 选举)
./bin/sentinel_server -p 6379 --rpc 50051 --sentinel \
    --sentinel-id node1 \
    --peers 127.0.0.1:50052,127.0.0.1:50053 \
    --monitor 127.0.0.1:6379,127.0.0.1:6380
```

### 7.5 负载均衡

```cpp
// 客户端侧负载均衡策略
enum LoadBalanceStrategy {
    RoundRobin,        // 轮询
    LeastConnections,  // 最少连接
    Random             // 随机
};

// RPC 发现桥接
class RpcDiscoveryBridge {
    // 从服务注册中心获取节点列表
    // 自动订阅变更，更新连接池
};
```

### 7.6 熔断器

```cpp
class RpcCircuitBreaker {
    // 状态: Closed → Open → HalfOpen → Closed
    // 和 HTTP 熔断器共享 CircuitBreakerMiddleware 逻辑
};
```

---

## 8. Sentinel 高可用

类 Raft 共识算法实现的分布式哨兵机制，提供自动故障检测和主从切换。

### 8.1 架构

```
┌───────────────────────────────────────────────┐
│               Sentinel 集群                    │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐       │
│  │ Node 1  │  │ Node 2  │  │ Node 3  │       │
│  │ (Leader)│  │(Follower)│  │(Follower)│       │
│  └────┬────┘  └────┬────┘  └────┬────┘       │
│       │            │            │             │
│       │  心跳/选举  │            │             │
│       └────────────┴────────────┘             │
└──────────────────────┬────────────────────────┘
                       │ 健康检查
         ┌─────────────┼─────────────┐
    ┌────▼────┐   ┌────▼────┐   ┌────▼────┐
    │ KV      │   │ KV      │   │ KV      │
    │ Master  │   │ Slave   │   │ Slave   │
    │ :6379   │   │ :6380   │   │ :6381   │
    └─────────┘   └─────────┘   └─────────┘
```

### 8.2 SentinelManager API

```cpp
class SentinelManager {
public:
    enum SentinelRole { Follower, Candidate, Leader };

    void setNodeId(const std::string& id);                    // 节点唯一标识
    void setBindAddr(const std::string& host, int port);      // 绑定地址
    void setSentinelPeers(const std::vector<std::pair<std::string, int>>& peers);
    void setMonitoredNodes(const std::vector<std::pair<std::string, int>>& nodes);

    void setFailoverCallback(FailoverCallback cb);            // 故障转移回调
    void start();
    void stop();

    // Raft-like 接口
    bool handleRequestVote(uint64_t term, const std::string& candidateId);
    bool handleHeartbeat(uint64_t term, const std::string& leaderId, ...);

    SentinelRole role() const;                                // 当前角色
};
```

### 8.3 工作流程

```
1. 启动 → Follower 状态，等待 Leader 心跳

2. 心跳超时 → 转为 Candidate，发起选举
   - term++
   - 投票给自己
   - 向所有 peers 发送 RequestVote
   - 收到多数票 → 成为 Leader

3. Leader 职责
   - 定期发送心跳给 Followers (heartbeatInterval)
   - 定期健康检查监控的 KV 节点 (healthCheckInterval)
   - 检测到 Master 下线 → 执行故障转移
     a. 选择最新 Slave 提升为新 Master
     b. 通知其他 Slave 复制新 Master
     c. 回调 FailoverCallback

4. 故障转移过程
   - 从所有 Slave 中选出数据最新的
   - promoteToMaster (SLAVEOF NO ONE)
   - 其他 Slave → SLAVEOF new_master_host new_master_port
   - 通知 Sentinel Followers 新拓扑
```

---

## 9. DB/ORM

MySQL ORM 框架，提供模型映射、Repository 模式、查询构建器、事务、迁移、读写分离、分库分表路由、缓存等功能。

### 9.1 模型定义

```cpp
#include "zero/db/db_model.h"

class User : public zero::db::DbModel {
public:
    ZERO_DB_MODEL(User, "users")              // 表名

    ZERO_DB_FIELD_INT64(id, "id",
        PrimaryKey | AutoIncrement)             // 主键 + 自增

    ZERO_DB_FIELD_STRING(name, "name",
        NotNull | MaxLength(100))               // NOT NULL VARCHAR(100)

    ZERO_DB_FIELD_STRING(email, "email",
        Unique | NotNull)                       // UNIQUE NOT NULL

    ZERO_DB_FIELD_INT32(age, "age",
        Default(0))                             // DEFAULT 0

    ZERO_DB_FIELD_TIMESTAMP(created_at, "created_at",
        AutoTimestamp)                          // 自动时间戳

    ZERO_DB_MODEL_END()
};

// 关联关系
class Order : public zero::db::DbModel {
public:
    ZERO_DB_MODEL(Order, "orders")
    ZERO_DB_FIELD_INT64(id, "id", PrimaryKey | AutoIncrement)
    ZERO_DB_FIELD_INT64(user_id, "user_id", NotNull)
    ZERO_DB_FIELD_STRING(product, "product", NotNull)
    ZERO_DB_MODEL_END()

    // 关联: Order 属于 User
    ZERO_DB_BELONGS_TO(User, user_id)
};

class User : public zero::db::DbModel {
    // ... 字段定义 ...
    // 关联: User 有多个 Order
    ZERO_DB_HAS_MANY(Order, user_id)
};
```

### 9.2 字段类型宏

| 宏 | C++ 类型 | SQL 类型 |
|----|----------|----------|
| `ZERO_DB_FIELD_INT32` | int32_t | INT |
| `ZERO_DB_FIELD_INT64` | int64_t | BIGINT |
| `ZERO_DB_FIELD_STRING` | std::string | VARCHAR |
| `ZERO_DB_FIELD_TEXT` | std::string | TEXT |
| `ZERO_DB_FIELD_BOOL` | bool | TINYINT(1) |
| `ZERO_DB_FIELD_FLOAT` | double | DOUBLE |
| `ZERO_DB_FIELD_TIMESTAMP` | std::chrono::system_clock::time_point | TIMESTAMP |
| `ZERO_DB_FIELD_DATETIME` | std::string | DATETIME |
| `ZERO_DB_FIELD_JSON` | std::string | JSON |

### 9.3 字段选项

| 选项 | 说明 |
|------|------|
| `PrimaryKey` | 主键 |
| `AutoIncrement` | 自增 |
| `NotNull` | NOT NULL 约束 |
| `Unique` | UNIQUE 约束 |
| `MaxLength(n)` | VARCHAR(n) |
| `Default(v)` | DEFAULT v |
| `AutoTimestamp` | 自动维护 created_at/updated_at |
| `SoftDelete` | 软删除 (deleted_at) |
| `OptimisticLock` | 乐观锁 (version 字段) |

### 9.4 Repository 模式

```cpp
#include "zero/db/db_repository.h"

// 自动 CRUD
auto repo = zero::db::DbRepository<User>::create(session);

// 增
User user;
user.name = "Alice";
user.email = "alice@example.com";
repo->save(user);                          // INSERT
int64_t newId = user.id;                   // 自增 ID 已回填

// 查
auto user = repo->findById("42");
auto results = repo->findAll();            // SELECT * FROM users
auto users = repo->findBy("age", "25");    // WHERE age = 25

// 查询构建器
auto q = repo->query();
q->where("age", ">", "18")
  ->where("name", "LIKE", "%Alice%")
  ->orderBy("created_at", "DESC")
  ->limit(10)
  ->offset(0);
auto page = repo->paginate(q, 1, 20);      // 分页查询

// 改
user.age = 26;
repo->save(user);                          // UPDATE (模型已知 id)

// 删
repo->removeById("42");
repo->removeWhere(q);                      // DELETE WHERE ...
```

### 9.5 查询构建器 (DbQuery)

```cpp
class DbQuery {
public:
    DbQuery::ptr where(const std::string& column, const std::string& op,
                       const std::string& value);
    DbQuery::ptr whereIn(const std::string& column,
                         const std::vector<std::string>& values);
    DbQuery::ptr orWhere(const std::string& column, ...);

    DbQuery::ptr orderBy(const std::string& column, const std::string& dir);
    DbQuery::ptr groupBy(const std::string& column);
    DbQuery::ptr having(const std::string& condition);

    DbQuery::ptr limit(int n);
    DbQuery::ptr offset(int n);

    DbQuery::ptr join(const std::string& table, const std::string& on);

    // 执行
    uint64_t count();                       // SELECT COUNT(*)
    bool exists();                          // SELECT 1 ... LIMIT 1
    std::vector<DbRow> get();               // 获取全部结果
    DbRow first();                          // 获取第一条
};
```

### 9.6 事务

```cpp
auto txn = session->beginTransaction();

try {
    repo->save(user1);
    repo->save(user2);
    txn->commit();
} catch (...) {
    txn->rollback();
    throw;
}
```

### 9.7 读写分离

```cpp
auto mgr = zero::db::DbManager::instance();

// 注册写库
mgr->registerMaster("main", masterSession);

// 注册多个读库（轮询负载均衡）
mgr->registerSlave("main", slaveSession1);
mgr->registerSlave("main", slaveSession2);

// 自动路由
auto writeSession = mgr->sessionForWrite("main");   // → master
auto readSession = mgr->sessionForRead("main");      // → slave (轮询)
```

### 9.8 分库分表路由

```cpp
// 自定义路由策略
class UserRouter : public DbRouter {
    std::string route(const std::string& table,
                      const DbValue& key) override {
        int64_t userId = key.asInt64();
        int shard = userId % 4;
        return "shard_" + std::to_string(shard);
    }
};

mgr->registerRouter("users", std::make_shared<UserRouter>());
auto session = mgr->routeForTable("users", DbValue(userId), true);
```

### 9.9 数据迁移

```cpp
#include "zero/db/db_migration.h"

class CreateUsersTable : public DbMigration {
    std::string version() override { return "20260705001"; }
    std::string description() override { return "Create users table"; }

    void up() override {
        schema()->createTable("users", [](auto& t) {
            t.bigIncrements("id");
            t.string("name", 100);
            t.string("email", 255)->unique();
            t.integer("age")->defaultValue(0);
            t.timestamps();
        });
    }

    void down() override {
        schema()->dropTable("users");
    }
};
```

### 9.10 MySQL 连接池

```cpp
#include "zero/db/mysql_connection.h"

// 连接配置
MysqlConnection::ptr conn(new MysqlConnection());
conn->connect("127.0.0.1", 3306, "user", "password", "database");

// Worker 池（协程友好，非阻塞异步查询）
#include "zero/db/mysql_worker_pool.h"
auto pool = MysqlWorkerPool::create(10);  // 10 个连接
pool->start();
```

### 9.11 模型生命周期钩子

```cpp
class User : public DbModel {
    void beforeInsert() override { /* 插入前 */ }
    void afterInsert() override  { /* 插入后 */ }
    void beforeUpdate() override { /* 更新前 */ }
    void afterUpdate() override  { /* 更新后 */ }
    void beforeSave() override   { /* 保存前 */ }
    void afterSave() override    { /* 保存后 */ }
    void beforeDelete() override { /* 删除前 */ }
    void afterDelete() override  { /* 删除后 */ }
    void afterLoad() override    { /* 从数据库加载后 */ }
};
```

---

## 10. 安全模块

### 10.1 WAF (Web Application Firewall)

```cpp
#include "zero/security/waf/waf_middleware.h"

WafConfig cfg;
cfg.blockSqlInjection = true;      // SQL 注入检测
cfg.blockXss = true;              // XSS 检测
cfg.blockIpBlacklist = true;      // IP 黑名单

auto waf = WafMiddleware::create(cfg);

// 手动管理 IP 黑名单
waf->addIpBlacklist("10.0.0.1");
waf->removeIpBlacklist("10.0.0.1");
```

**检测能力：**
| 攻击类型 | 检测内容 | 示例 |
|----------|----------|------|
| SQL 注入 | `' OR 1=1 --`, `'; DROP TABLE`, `UNION SELECT` | `?id=1 OR 1=1` |
| XSS | `<script>`, `javascript:`, `onerror=` | `<script>alert(1)</script>` |

### 10.2 Auth (JWT + Static Token)

```cpp
#include "zero/security/auth/auth_middleware.h"

// JWT 鉴权
auto auth = AuthMiddleware::jwt("my_secret_key");

// 静态 Token 鉴权
auto auth = AuthMiddleware::staticTokens({"token1", "token2"});

// 自定义 Token 验证器
auto auth = AuthMiddleware::create([](const std::string& token) -> AuthContext {
    if (token == "valid") return AuthContext{true, "user_123", "admin"};
    return AuthContext{false};
});

// 中间件会自动注入 X-User-Id, X-User-Role 等头
```

### 10.3 RBAC / Permission

```cpp
#include "zero/security/auth/permission_middleware.h"

// 权限中间件：检查 X-Claim-permissions 头
auto perm = PermissionMiddleware::create({"user:write", "admin:read"});

// RBAC 中间件：检查 X-User-Role 头
auto rbac = RBACMiddleware::create(
    {"admin", "superadmin"},        // 允许的角色
    "/api/admin"                    // 保护路径前缀
);
```

### 10.4 TLS / mTLS

```cpp
#include "zero/security/tls/mtls_config.h"
#include "zero/security/tls/cert_manager.h"

// mTLS 配置
tls::MtlsConfig cfg;
cfg.certFile = "/etc/ssl/certs/server.crt";
cfg.keyFile = "/etc/ssl/private/server.key";
cfg.caFile = "/etc/ssl/certs/ca.crt";
cfg.verifyPeer = true;             // 双向验证
cfg.verifyDepth = 9;

auto serverCtx = tls::MtlsContext::createServer(cfg);
auto clientCtx = tls::MtlsContext::createClient(cfg);

// 证书管理器
auto cm = CertificateManager::create();
cm->loadFromFile("example.com", "/path/to/cert.pem", "/path/to/key.pem");

Certificate cert;
if (cm->getCertificate("example.com", cert)) {
    // 证书有效
}
if (cm->isExpiringSoon("example.com", 30)) {
    // 30 天内过期告警
}
```

### 10.5 JWT 工具

```cpp
#include "zero/util/jwt_util.h"

// 生成 JWT
JWTPayload payload;
payload.sub = "user_123";                // subject
payload.role = "admin";                  // 角色
payload.claims["permissions"] = "read,write";
payload.exp = std::chrono::system_clock::now() + std::chrono::hours(1);

std::string token = JWTUtil::generate("secret", payload);

// 验证 JWT
auto decoded = JWTUtil::verify("secret", token);
if (decoded) {
    std::string userId = decoded->sub;
    std::string role = decoded->claims["role"];
}

// 无验证解码
auto payload = JWTUtil::decode(token);
```

---

## 11. 配置中心

### 11.1 YAML 配置

```yaml
# application.yml
app:
  name: zero-server
  port: 8080
  workers: 4

logs:
  - name: root
    level: INFO
    appenders:
      - type: stdout

kv:
  port: 6379
  maxmemory: 1073741824        # 1GB
  maxmemory-policy: allkeys-lru
  appendonly: true
  appendfsync: everysec
  save: 60
```

### 11.2 Config API

```cpp
#include "zero/core/config/config.h"

// 声明配置变量
static zero::ConfigVar<int>::ptr g_port =
    zero::Config::Lookup("app.port", 8080, "server listen port");

// 读取
int port = g_port->getValue();

// 热更新回调
g_port->addListener([](const int& oldVal, const int& newVal) {
    ZERO_LOG_INFO(g_logger) << "Port changed from " << oldVal
                            << " to " << newVal;
});
```

### 11.3 配置中心集成

```cpp
#include "zero/config/config_center.h"

// etcd 配置中心
auto center = std::make_shared<EtcdConfigCenter>("http://127.0.0.1:2379");
center->start();

// 热加载
center->startHotReload("/tmp/config.yaml", 500); // 500ms 轮询

// 变更通知
center->addListener("app.timeout", [](const ConfigChangeEvent& e) {
    ZERO_LOG_INFO(g_logger) << "Config changed: " << e.key
                            << " = " << e.newValue;
});
```

---

## 12. 服务注册发现

### 12.1 服务实例模型

```cpp
struct ServiceInstance {
    std::string id;
    std::string serviceName;
    std::string host;
    int port = 0;
    std::string version;
    std::map<std::string, std::string> metadata;  // 自定义元数据
    bool healthy = true;
};
```

### 12.2 etcd 注册中心

```cpp
#include "zero/registry/etcd_registry.h"

// 服务注册
auto registry = std::make_shared<EtcdRegistry>(
    "http://127.0.0.1:2379", "/zero/services");

ServiceInstance inst;
inst.id = "kv-node-1";
inst.serviceName = "kv-service";
inst.host = "127.0.0.1";
inst.port = 6379;
inst.version = "1.0.0";

registry->registerService(inst);
registry->heartbeat(inst.id);            // 定期心跳
registry->deregister(inst.id);

// 服务发现
#include "zero/registry/service_discovery.h"

auto discovery = std::make_shared<ServiceDiscovery>(registry);

// 健康检查配置
HealthCheckConfig hc;
hc.enabled = true;
hc.intervalMs = 5000;                    // 5 秒探测间隔

discovery->setHealthCheck(hc);

// 获取服务实例
auto instances = discovery->getInstances("kv-service");

// 订阅变更
discovery->subscribe("kv-service", [](const std::vector<ServiceInstance>& insts) {
    ZERO_LOG_INFO(g_logger) << "Service instances changed: " << insts.size();
});

// 元数据过滤
auto filtered = discovery->getInstances("kv-service",
    {{"version", "1.0.0"}, {"region", "us-east"}});
```

---

## 13. 工具库

### 13.1 加密工具

```cpp
#include "zero/util/crypto_util.h"

// MD5 / SHA1 / SHA256 / SHA512
std::string md5 = CryptoUtil::MD5("hello");
std::string sha256 = CryptoUtil::SHA256("hello");

// Base64
std::string encoded = CryptoUtil::base64Encode(data);
std::string decoded = CryptoUtil::base64Decode(encoded);

// AES 加解密
std::string encrypted = CryptoUtil::AESEncrypt(plaintext, key);
std::string decrypted = CryptoUtil::AESDecrypt(encrypted, key);

// HMAC
std::string hmac = CryptoUtil::HMAC_SHA256(data, secret);

// 随机数
std::string randomBytes = CryptoUtil::randomBytes(32);
int64_t randomInt = CryptoUtil::randomInt64();
```

### 13.2 哈希工具

```cpp
#include "zero/util/hash_util.h"

uint64_t h = HashUtil::murmurHash64(key);
uint32_t h = HashUtil::fnv1a(data, len);
```

### 13.3 JSON 工具

```cpp
#include "zero/util/json_util.h"

// 解析
Json::Value root = JsonUtil::parse("{\"name\": \"zero\"}");

// 序列化
std::string json = JsonUtil::stringify(root);

// 安全取值
std::string name = JsonUtil::getString(root, "name", "default");
int port = JsonUtil::getInt(root, "port", 8080);
```

### 13.4 其他工具

```cpp
#include "zero/core/util/util.h"

// 获取线程/协程 ID
uint64_t tid = zero::GetThreadId();
uint64_t fid = zero::GetFiberId();

// 获取主机名
std::string host = zero::GetHostName();

// 高精度时间
uint64_t ms = zero::GetCurrentMS();
uint64_t us = zero::GetCurrentUS();

// CPU 核心数
int cores = zero::GetCpuCores();

// 字符串工具
zero::StringUtil::Split(str, ',');
zero::StringUtil::Trim(str);
zero::StringUtil::ToUpper(str);
zero::StringUtil::ToLower(str);
zero::StringUtil::StartsWith(str, prefix);
zero::StringUtil::EndsWith(str, suffix);
```

---

## 测试

### 测试概览（48 套 — 全部通过，零错误 ✅）

```bash
# 运行全部测试
cmake --build build -j$(nproc)
ctest --test-dir build -j$(nproc)

# 运行指定分类
ctest --test-dir build -R test_kv        # KV 测试
ctest --test-dir build -R test_rpc       # RPC 测试

# 重新运行失败的测试
ctest --test-dir build --rerun-failed --output-on-failure
```

### 测试清单

#### Core (核心) — 12 套
| 测试 | 覆盖范围 |
|------|----------|
| `test_util_sample` | 基础工具函数 |
| `test_bytearray` | ByteArray 编解码 |
| `test_address` | 网络地址解析与 CIDR |
| `test_fd_manager` | 文件描述符管理 |
| `test_iomanager` | epoll 事件循环 + 定时器 |
| `test_socket` | TCP/UDP Socket 操作 |
| `test_fiber` | 协程创建/切换/栈管理 |
| `test_timer` | 定时器添加/取消/重置 |
| `test_mutex` | 互斥锁/读写锁/自旋锁/协程信号量 |
| `test_thread` | 线程创建与管理 |
| `test_log` | 日志基础功能 |
| `test_config_var` | 配置变量读写 + 热更新回调 |

#### Net/HTTP — 6 套
| 测试 | 覆盖范围 |
|------|----------|
| `test_socket_stream` | Socket 流式读写 |
| `test_zlib_stream` | 压缩/解压流 |
| `test_http_parser` | Ragel HTTP/1.1 解析器 |
| `test_http2` | HTTP/2 帧 + HPACK 编解码 |
| `test_http_server` | HTTP 服务端集成测试 |
| `test_fiber_stack` | 协程栈 mmap/guard page/freelist |

#### KV — 12 套
| 测试 | 覆盖范围 |
|------|----------|
| `test_kv_info` | INFO 命令输出验证 |
| `test_kv_resp` | RESP 协议编解码、pipeline 帧解析 |
| `test_kv_rdb` | RDB v2 快照往返（SAVE + 加载） |
| `test_kv_pubsub` | 发布订阅（channel + pattern） |
| `test_kv_aof` | AOF 日志重放与 rewrite |
| `test_kv_repl` | PSYNC 协议、master/slave role |
| `test_kv_p0p1` | P0/P1 扩展命令全量验收 |
| `test_kv_phase10` | RDB v2、消费组、XREADGROUP |
| `test_kv_single` | P0 单体命令、多 key、TTL |
| `test_kv_commands` | 基础命令（String/Hash/List/Set/ZSet）、事务 |
| `test_kv_blocking` | BLPOP/BRPOP/BRPOPLPUSH 阻塞操作 |
| `test_kv_extended` | AUTH、淘汰策略、Stream 命令 |
| `test_kv_repl_e2e` | 主从端到端数据一致性 |

#### RPC — 9 套
| 测试 | 覆盖范围 |
|------|----------|
| `test_rpc_mini` | RPC 最小框架连接/通信 |
| `test_rpc_client` | RpcChannel 客户端调用 |
| `test_rpc_server` | RpcServer 服务端处理 |
| `test_rpc_v2` | RPC 高级功能 |
| `test_rpc` | RPC 功能集成 |
| `test_rpc_load_balancer` | 负载均衡策略 |
| `test_rpc_etcd_integration` | RPC + etcd 服务发现集成 |
| `test_rpc_qps` | RPC QPS 压力测试 |
| `test_service_governance` | 服务治理（注册/发现/健康检查） |

#### Security/Util/Other — 7 套
| 测试 | 覆盖范围 |
|------|----------|
| `test_jwt` | JWT 生成/验证/过期 |
| `test_security` | WAF/JWT/RBAC/Permission/mTLS |
| `test_cert_manager` | 证书管理（加载/过期检测） |
| `test_config` | YAML 配置解析与全量热加载 |
| `test_config_center` | etcd 配置中心集成 |
| `test_framework_extensions` | 网关路由/配置热更新/TraceContext |
| `test_log_system` | 日志系统集成测试（多 Appender） |
| `test_production_features` | Signal/TcpServer/K8s Probe |

---

## bench2 QPS 压测工具

`bench2/` 目录提供独立的 QPS 并发测试工具，所有客户端使用 **raw socket I/O**（不依赖框架），模拟真实场景压测。

### 工具列表

| 工具 | 类型 | 说明 |
|------|------|------|
| `tcp_echo_server` | 服务器 | Zero 协程 TCP Echo 服务器 |
| `tcp_echo_client` | 客户端 | Raw socket TCP Echo QPS 测试 |
| `http_echo_server` | 服务器 | HTTP POST Body Echo 服务器 |
| `http_echo_client` | 客户端 | Raw socket HTTP/1.1 QPS 测试 |
| `ws_echo_server` | 服务器 | WebSocket Echo 服务器 |
| `ws_echo_client` | 客户端 | Raw socket WebSocket QPS 测试 |
| `kv_bench_client` | 客户端 | Raw socket RESP 协议 QPS 测试 |

### 通用参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-h` | 服务器地址 | 127.0.0.1 |
| `-p` | 端口 | 协议默认端口 |
| `-c` | 并发连接数 | 100 |
| `-d` | 测试时长 (秒) | 10 |
| `-s` | 负载大小 (字节) | 64 |
| `-w` | Worker 线程数 (服务器) | 4 |

### KV 客户端额外参数

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `-t` | 命令 (PING/GET/SET) | GET |
| `-P` | Pipeline 批量数 | 1 |
| `-k` | Key 空间大小 | 10000 |

### 使用示例

```bash
# TCP Echo 压测
./bin/tcp_echo_server -p 18020 -w 4 &
./bin/tcp_echo_client -p 18020 -c 500 -d 10 -s 64

# HTTP POST Echo 压测
./bin/http_echo_server -p 18022 -w 4 &
./bin/http_echo_client -p 18022 -c 500 -d 10 -s 256

# KV GET 压测（非管道）
./bin/kv_server -p 16379 -w 4 &
./bin/kv_bench_client -p 16379 -c 500 -d 10 -t GET

# KV GET Pipeline 压测
./bin/kv_bench_client -p 16379 -c 500 -d 10 -t GET -P 10
./bin/kv_bench_client -p 16379 -c 500 -d 10 -t GET -P 50

# 一键全量压测（含多 payload 复合测试）
bash bench2/run_all.sh
bash bench2/run_all.sh --quick    # 快速模式 2s×100conn
```

---

## main/ — Spring Boot 风格启动入口

`main/` 目录提供生产级 Spring Boot 风格的服务器启动入口，支持 YAML 配置加载、命令行覆盖、环境变量覆盖、Profile 激活、配置热重载、优雅关闭等完整功能。

### 启动入口

| 可执行文件 | 源码 | 说明 |
|------------|------|------|
| `bin/zero_app` | `main/zero_app.cc` | 统一应用，按配置同时启动 HTTP/KV/RPC/WS |
| `bin/http_server` | `main/http_server.cc` | 纯 HTTP 服务器 |
| `bin/kv_main` | `main/kv_server.cc` | 纯 KV (Redis) 服务器，支持 RDB/AOF/管理接口 |

### 功能特性

- **配置文件**: YAML 格式 (`conf/application.yml`)，自动加载配置目录下所有 `.yml` 文件
- **命令行覆盖**: `--key=value` 格式，如 `--app.workers=8 --servers.http.address=0.0.0.0:9090`
- **环境变量覆盖**: 自动读取环境变量（下划线 → 点号），如 `ZERO_APP_WORKERS=8`
- **Profile**: 支持 `-p dev/test/prod` 多环境配置切换
- **配置热重载**: inotify 监控 `conf/` 目录，修改后自动生效（300ms 防抖）
- **优雅关闭**: SIGINT/SIGTERM 安全关闭，KV 服务器自动触发 SAVE
- **守护进程**: `-d` 后台运行，可选 PID 文件
- **默认 6 worker 线程**

### 使用示例

```bash
# HTTP 服务器——默认 6 worker
./bin/http_server

# 覆盖端口和线程数
./bin/http_server --app.workers=8 --servers.http.address=0.0.0.0:9090

# KV 服务器——开启 AOF 持久化和 HTTP 管理接口
./bin/kv_main --servers.kv.aof_enabled=1 --servers.kv.http_admin_port=16379

# 统一应用——同时启动 HTTP + KV
./bin/zero_app --servers.kv.enabled=1

# 生产环境——后台运行
./bin/http_server -d --app.pid_file=/var/run/http-server.pid

# 指定 Profile
./bin/zero_app -p prod
```

### QPS 多线程缩放测试

```bash
# 以 HTTP 为例，用 bench2 工具测试不同 worker 线程数的 QPS
for w in 2 4 6 8; do
  ./bin/http_echo_server -p 8022 -w $w &
  sleep 1
  ./bin/http_echo_client -p 8022 -c 500 -d 15 -s 64
  kill %1; wait; sleep 1
done
```

完整测试结果见 [性能一览](#性能一览)。

---

## 目录结构

```
zero-framework/
├── zero/                         # 框架核心源码
│   ├── core/                     # 核心基础设施
│   │   ├── concurrency/          # fiber, scheduler, mutex, thread, timer
│   │   ├── io/                   # iomanager, socket, stream, address, hook
│   │   ├── log/                  # log, log_level, log_formatter,
│   │   │                        #   log_appender, log_filter, log_config,
│   │   │                        #   log_degrade, log_context, async_log
│   │   ├── config/               # config (YAML 配置变量)
│   │   └── util/                 # util, env, uri.rl
│   │
│   ├── net/                      # 网络层
│   │   ├── tcp/                  # tcp_server
│   │   └── streams/              # socket_stream, zlib_stream
│   │
│   ├── http/                     # HTTP / WebSocket 模块
│   │   ├── servlets/             # config_servlet, status_servlet,
│   │   │                        #   health_servlet, db_status_servlet
│   │   ├── middleware/           # access_log, circuit_breaker, cors,
│   │   │                        #   rate_limit, timeout
│   │   ├── gateway/              # gateway_servlet, http_proxy_backend,
│   │   │                        #   http_reverse_proxy
│   │   ├── http_server, http_session, http_connection
│   │   ├── ws_server, ws_session, ws_connection, ws_servlet
│   │   ├── http_parser, http11_parser.rl, httpclient_parser.rl
│   │   ├── http2, hpack, restful_router
│   │   └── servlet, session_data
│   │
│   ├── kv/                       # KV 存储引擎
│   │   ├── store/                # kv_store (内存存储 + Shard 分片)
│   │   ├── persistence/          # rdb (快照), aof (追加日志)
│   │   ├── replication/          # replication (主从复制 + PSYNC)
│   │   ├── pubsub/               # pubsub_hub (发布订阅)
│   │   ├── blocking/             # blocking_list_hub (阻塞队列)
│   │   ├── admin/                # kv_admin_servlet (HTTP 管理接口)
│   │   ├── resp, resp_reader, resp_encoder
│   │   ├── command_dispatch, command.h, commands_p0p1
│   │   ├── kv_server, kv_session, kv_config
│   │   ├── slowlog, info, lua_engine
│   │   └── cluster/              # (保留，待实现)
│   │
│   ├── rpc/                      # RPC 框架
│   │   ├── proto/                # Protobuf 定义
│   │   │   ├── common.proto
│   │   │   ├── kv_node.proto     # KvNodeService
│   │   │   ├── sentinel.proto    # SentinelService
│   │   │   └── rpc.proto
│   │   ├── rpc_server, rpc_channel, rpc_channel_manager
│   │   ├── rpc_config, rpc_circuit_breaker
│   │   ├── rpc_load_balancer, rpc_discovery_bridge
│   │   ├── kv_node_service, sentinel_service
│   │   └── sentinel_manager
│   │
│   ├── db/                       # MySQL ORM 框架
│   │   ├── mysql/                # mysql_driver, mysql_session
│   │   ├── db_model, db_value, db_validator
│   │   ├── db_query, db_session, db_transaction
│   │   ├── db_repository, db_manager
│   │   ├── db_migration, db_schema, db_relation
│   │   ├── db_cache, db_router, db_result, db_driver
│   │   ├── mysql_connection, mysql_worker_pool
│   │   └── db_session_hook
│   │
│   ├── registry/                 # 服务注册发现
│   │   ├── service_registry (接口)
│   │   ├── service_discovery, etcd_registry
│   │   ├── etcd_http_client
│   │   └── (转发头: etcd_config_center, zookeeper_config_center)
│   │
│   ├── security/                 # 安全模块
│   │   ├── auth/                 # auth_middleware, permission_middleware
│   │   ├── tls/                  # cert_manager, mtls_config
│   │   └── waf/                  # waf_middleware
│   │
│   ├── config/                   # 配置中心
│   │   └── config_center, etcd_config_center, zookeeper_config_center
│   │
│   ├── streams/                  # socket_stream, zlib_stream
│   └── util/                     # crypto_util, hash_util, json_util, jwt_util
│
├── main/                         # Spring Boot 风格生产服务器入口
│   ├── zero_app.cc              #   统一应用 (HTTP/KV/RPC/WS)
│   ├── http_server.cc           #   纯 HTTP 服务器
│   └── kv_server.cc             #   纯 KV 服务器
├── bench2/                       # QPS 压测工具 (TCP/HTTP/WS/KV)
│   ├── tcp_echo_server.cc        # TCP Echo 服务器 (Zero 协程)
│   ├── tcp_echo_client.cc        # TCP QPS 客户端 (raw socket)
│   ├── http_echo_server.cc       # HTTP Echo 服务器
│   ├── http_echo_client.cc       # HTTP QPS 客户端 (raw socket)
│   ├── ws_echo_server.cc         # WebSocket Echo 服务器
│   ├── ws_echo_client.cc         # WebSocket QPS 客户端 (raw socket)
│   ├── kv_bench_client.cc        # KV RESP 协议 QPS 客户端 (raw socket)
│   └── run_all.sh                # 一键全量压测脚本
│
├── examples/                     # 服务入口示例
│   ├── kv_server.cc              # KV 服务入口
│   ├── sentinel_server.cc        # Sentinel 服务入口
│   ├── echo_server.cc            # TCP Echo Server 入口
│   ├── bench_server.cc           # TCP Echo 压测服务器
│   ├── http_bench_server.cc      # HTTP Echo 压测服务器
│   ├── ws_bench_server.cc        # WebSocket Echo 压测服务器
│   ├── qps_client.cc             # TCP QPS 压测客户端
│   ├── http_qps_client.cc        # HTTP QPS 压测客户端
│   ├── ws_qps_client.cc          # WebSocket QPS 压测客户端
│   ├── log_demo.cc               # 日志功能演示
│   └── log_qps_matrix.cc         # 日志 QPS 矩阵工具
│
├── tests/                        # 单元测试 (48 套，全部通过)
│   ├── unit/
│   │   ├── core/                 # 核心测试 (14 套)
│   │   ├── net/                  # 网络测试 (2 套)
│   │   ├── http/                 # HTTP 测试 (3 套)
│   │   ├── kv/                   # KV 测试 (12 套)
│   │   ├── rpc/                  # RPC 测试 (9 套)
│   │   ├── db/                   # DB/ORM 测试 (5 套)
│   │   ├── config/               # 配置中心测试 (1 套)
│   │   ├── util/                 # 工具测试 (1 套)
│   │   └── security/             # 安全测试 (2 套)
│   ├── support/                  # 测试支持库 (mocks, helpers)
│   └── CMakeLists.txt
│
├── scripts/                      # 构建/测试/压测脚本
│   ├── build.sh                  # 构建脚本
│   ├── run_tests.sh              # 运行测试（按分类）
│   ├── run_server.sh             # 启动服务
│   ├── bench_*.sh                # 压测脚本 (tcp/http/kv/db/log/rpc/ws)
│   ├── bench_all.sh              # 全量压测编译
│   ├── bench_common.sh           # 压测公共函数
│   ├── docker.sh                 # Docker 构建
│   ├── format.sh                 # 代码格式化
│   └── setup_env.sh              # 环境初始化
│
├── bench/                        # 旧版 bench (保留兼容)
│   └── (tcp/http/kv/db/log/rpc/ws)/
│
├── cmake/                        # CMake 工具
│   └── utils.cmake               # force_redefine_file_macro, protobufmaker
│
├── conf/                         # 配置文件
│   ├── application.yml           # 默认配置
│   └── application-full.yml      # 全量配置参考
│
├── CMakeLists.txt                # 顶层构建脚本
├── .gitignore
└── README.md
```

---

## API 速查

### 快速启动一个 TCP 服务

```cpp
#include "zero/net/tcp/tcp_server.h"
#include "zero/core/io/iomanager.h"

class MyServer : public zero::TcpServer {
    void handleClient(zero::Socket::ptr client) override {
        char buf[4096];
        while (true) {
            int n = client->recv(buf, sizeof(buf));
            if (n <= 0) break;
            client->send(buf, n);   // echo
        }
    }
};

int main() {
    zero::set_hook_enable(true);
    zero::IOManager iom(4);
    iom.schedule([]() {
        auto server = std::make_shared<MyServer>();
        server->bind(zero::Address::LookupAny("0.0.0.0:8020"));
        server->start();
    });
    return 0;
}
```

### 日志 + MDC 追踪

```cpp
#include "zero/core/log/log.h"

static auto g_log = ZERO_LOG_NAME("myapp");

zero::LogContext::put("request_id", generateRequestId());
ZERO_LOG_INFO(g_log) << "Processing request";
```

### 配置热更新

```cpp
auto g_port = zero::Config::Lookup<int>("app.port", 8080, "listen port");
g_port->addListener([](const int& oldV, const int& newV) {
    // 配置变更，reload 相关组件
});
```

### 定时任务

```cpp
auto iom = zero::IOManager::GetThis();
iom->addTimer(5000, []() {
    ZERO_LOG_INFO(g_log) << "5-second timer fired!";
}, true);  // recurring = true
```

### RPC 客户端调用

```cpp
RpcChannel channel;
channel.connect("127.0.0.1", 50051);

KvNodeService_Stub stub(&channel);
HealthCheckRequest req;
HealthCheckResponse rsp;
stub.HealthCheck(nullptr, &req, &rsp, nullptr);
```

### DB 增删改查

```cpp
auto repo = DbRepository<User>::create(session);

// 增
User u; u.name = "Alice"; u.email = "alice@example.com";
repo->save(u);

// 查
auto user = repo->findById("1");
auto all = repo->findAll();
auto recent = repo->query()->where("created_at", ">", "2026-01-01")
                        ->orderBy("created_at", "DESC")->limit(10)->get();

// 改
user->name = "Alice2"; repo->save(user);

// 删
repo->removeById("1");
```

---

## 配置参考

### 完整 application.yml

```yaml
app:
  name: zero-server
  workers: 4

# 日志配置
logs:
  - name: root
    level: INFO
    formatter: "%d{%Y-%m-%d %H:%M:%S}%T%t%T%F%T[%p]%T[%c]%T%f:%l%T%m%n"
    appenders:
      - type: stdout
        color: auto
      - type: file
        file: logs/app.log
        rolling: true
        max_size: 104857600
      - type: async
        buffer_size: 65536
        flush_interval_ms: 100
        appender:
          type: file
          file: logs/app_async.log

# KV 配置
kv:
  port: 6379
  maxmemory: 1073741824
  maxmemory-policy: allkeys-lru
  appendonly: true
  appendfsync: everysec
  save: 60
  requirepass: ""
  slowlog-log-slower-than: 10000
  slowlog-max-len: 128
  databases: 16
  timeout: 300
  tcp-keepalive: 300
  lua-global-atomic: false

# RPC 配置
rpc:
  port: 50051
  node-id: node1
  node-host: 0.0.0.0
  sentinel-mode: false
  sentinel-peers: []
  monitored-nodes: []

# 注册中心
registry:
  etcd:
    endpoints: ["http://127.0.0.1:2379"]
    prefix: /zero/services
    heartbeat-interval: 5
    ttl: 15

# DB/ORM 配置
db:
  default: main
  connections:
    main:
      driver: mysql
      host: 127.0.0.1
      port: 3306
      user: root
      password: ""
      database: zero
      pool-size: 10
      charset: utf8mb4
    slave1:
      driver: mysql
      host: 127.0.0.1
      port: 3307
      user: root
      password: ""
      database: zero
      pool-size: 5
```

---

## 构建选项

```bash
# Release 编译（生产环境）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Debug 编译（开发调试）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# 无 Protobuf（无 RPC 模块）
# 自动检测，未安装则自动跳过

# 无 LuaJIT（无 Lua 脚本功能）
# 自动检测，未安装则自动跳过

# 无 MySQL（无 DB/ORM 集成测试）
# 自动跳过 DB 集成测试
```

---

## License

MIT License

---

**当前基线：** 130+ 条 KV 命令，48 套测试全部通过（0 错误），bench2 全协议 QPS 压测工具，Spring Boot 风格 `main/` 启动入口，TCP Echo 62 万 QPS（4 线程），HTTP Echo 44 万 QPS（8 线程），KV GET 38 万 rps（6 线程）。DB/ORM 已独立为 [`zero-orm`](/home/wangmaosen/project/kv/orm/) 模块。
