cmake_minimum_required(VERSION 3.10)
project(zero-framework CXX C)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(cmake/utils.cmake)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -rdynamic -O2 -fPIC -ggdb -Wall -Wno-deprecated -Wno-unused-function -Wno-deprecated-declarations -Wno-builtin-macro-redefined -faligned-new")

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

find_package(Boost REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(GTest REQUIRED)
find_package(ZLIB REQUIRED)
find_package(Protobuf)

# LuaJIT (pkg-config: luajit)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(LUA luajit QUIET)
endif()
if(LUA_FOUND)
    include_directories(${LUA_INCLUDE_DIRS})
    add_definitions(-DHAS_LUA)
else()
    message(WARNING "LuaJIT not found — Lua scripting (EVAL/EVALSHA/SCRIPT) will be disabled")
endif()


# MySQL client (optional, for db tests/integration)
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(MYSQLCLIENT mysqlclient QUIET)
endif()
if(MYSQLCLIENT_FOUND)
    include_directories(${MYSQLCLIENT_INCLUDE_DIRS})
    link_directories(${MYSQLCLIENT_LIBRARY_DIRS})
    list(APPEND LIBS ${MYSQLCLIENT_LIBRARIES})
    message(STATUS "MySQL client found: ${MYSQLCLIENT_LIBRARIES}")
else()
    message(WARNING "MySQL client not found — DB integration tests will be disabled")
endif()

if(Boost_FOUND)
    include_directories(${Boost_INCLUDE_DIRS})
endif()
if(OPENSSL_FOUND)
    include_directories(${OPENSSL_INCLUDE_DIR})
endif()
if(ZLIB_FOUND)
    include_directories(${ZLIB_INCLUDE_DIR})
endif()
if(Protobuf_FOUND)
    include_directories(${Protobuf_INCLUDE_DIRS})
endif()

# Include generated proto files
include_directories(${CMAKE_CURRENT_BINARY_DIR})

if(EXISTS /usr/include/jsoncpp)
    include_directories(/usr/include/jsoncpp)
endif()

# Proto generation for RPC module
if(Protobuf_FOUND)
    set(RPC_PROTO_SRCS "")
    protobufmaker(zero/rpc/proto/common.proto RPC_PROTO_SRCS ${CMAKE_CURRENT_BINARY_DIR})
    protobufmaker(zero/rpc/proto/kv_node.proto RPC_PROTO_SRCS ${CMAKE_CURRENT_BINARY_DIR})
    protobufmaker(zero/rpc/proto/sentinel.proto RPC_PROTO_SRCS ${CMAKE_CURRENT_BINARY_DIR})
    protobufmaker(zero/rpc/proto/rpc.proto RPC_PROTO_SRCS ${CMAKE_CURRENT_BINARY_DIR})
    set(HAS_RPC ON)
else()
    set(HAS_RPC OFF)
endif()

set(LIB_SRC
    zero/core/io/address.cc
    zero/core/io/bytearray.cc
    zero/core/config/config.cc
    zero/core/util/env.cc
    zero/core/io/fd_manager.cc
    zero/core/concurrency/fiber.cc
    zero/core/concurrency/fiber_stack.cc
    zero/core/io/hook.cc
    zero/core/io/iomanager.cc
    zero/core/log/log.cc
    zero/core/log/log_level.cc
    zero/core/log/log_buffer.cc
    zero/core/log/log_event.cc
    zero/core/log/log_formatter.cc
    zero/core/log/log_appender.cc
    zero/core/log/log_color.cc
    zero/core/log/log_degrade.cc
    zero/core/log/log_filter.cc
    zero/core/log/log_context.cc
    zero/core/log/log_stats.cc
    zero/core/log/log_config.cc
    zero/core/log/async_log.cc
    zero/core/concurrency/mutex.cc
    zero/core/concurrency/scheduler.cc
    zero/core/io/socket.cc
    zero/core/io/stream.cc
    zero/net/streams/socket_stream.cc
    zero/net/tcp/tcp_server.cc
    zero/core/concurrency/thread.cc
    zero/core/concurrency/timer.cc
    zero/core/util/util.cc
    zero/util/crypto_util.cc
    zero/util/hash_util.cc
    zero/util/json_util.cc
    zero/util/jwt_util.cc
    zero/core/concurrency/worker.cc
    zero/net/streams/zlib_stream.cc
    zero/core/util/uri.rl.cc
    zero/http/http.cc
    zero/http/http_connection.cc
    zero/http/http_parser.cc
    zero/http/http_session.cc
    zero/http/http_server.cc
    zero/http/servlet.cc
    zero/http/servlets/config_servlet.cc
    zero/http/servlets/status_servlet.cc
    zero/http/servlets/health_servlet.cc
    zero/http/servlets/db_status_servlet.cc
    zero/http/session_data.cc
    zero/http/ws_connection.cc
    zero/http/ws_session.cc
    zero/http/ws_server.cc
    zero/http/ws_servlet.cc
    zero/http/http11_parser.rl.cc
    zero/http/httpclient_parser.rl.cc
    zero/http/hpack.cc
    zero/http/http2.cc
    zero/http/middleware.cc
    zero/http/middleware/access_log_middleware.cc
    zero/http/middleware/circuit_breaker_middleware.cc
    zero/http/middleware/cors_middleware.cc
    zero/http/middleware/rate_limit_middleware.cc
    zero/http/middleware/timeout_middleware.cc
    zero/http/restful_router.cc
    zero/http/gateway/gateway_servlet.cc
    zero/http/gateway/http_proxy_backend.cc
    zero/http/gateway/http_reverse_proxy.cc
    zero/kv/resp.cc
    zero/kv/resp_reader.cc
    zero/kv/store/kv_store.cc
    zero/kv/store/kv_store_extras.cc
    zero/kv/command_dispatch.cc
    zero/kv/commands_p0p1.cc
    zero/kv/slowlog.cc
    zero/kv/info.cc
    zero/kv/admin/kv_admin_servlet.cc
    zero/kv/kv_session.cc
    zero/kv/kv_server.cc
    zero/kv/kv_config.cc
    zero/kv/persistence/rdb.cc
    zero/kv/persistence/aof.cc
    zero/kv/pubsub/pubsub_hub.cc
    zero/kv/blocking/blocking_list_hub.cc
    zero/kv/replication/replication.cc
    zero/kv/lua_engine.cc
    # Note: zero/kv/cluster/* temporarily excluded due to interface mismatch
    # with current KvContext/KvStore. Re-enable after cluster module is fixed.
    # RPC module
    ${RPC_PROTO_SRCS}
    zero/rpc/rpc_config.cc
    zero/rpc/rpc_channel.cc
    zero/rpc/rpc_channel_manager.cc
    zero/rpc/rpc_circuit_breaker.cc
    zero/rpc/rpc_discovery_bridge.cc
    zero/rpc/rpc_load_balancer.cc
    zero/rpc/rpc_server.cc
    zero/rpc/kv_node_service.cc
    zero/rpc/sentinel_service.cc
    zero/rpc/sentinel_manager.cc
    zero/config/config_center.cc
    zero/config/etcd_config_center.cc
    zero/config/zookeeper_config_center.cc
    zero/db/db_migration.cc
    zero/db/db_query.cc
    zero/db/db_schema.cc
    zero/db/db_transaction.cc
    zero/db/mysql_connection.cc
    zero/db/mysql/mysql_driver.cc
    zero/db/mysql/mysql_session.cc
    zero/db/mysql_worker_pool.cc
    zero/security/auth/auth_middleware.cc
    zero/security/auth/permission_middleware.cc
    zero/security/tls/cert_manager.cc
    zero/security/tls/mtls_config.cc
    zero/security/waf/waf_middleware.cc
    zero/registry/etcd_http_client.cc
    zero/registry/etcd_registry.cc
    zero/registry/service_discovery.cc
    zero/core/concurrency/signal.cc
)

add_library(zero SHARED ${LIB_SRC})
force_redefine_file_macro_for_sources(zero)

# Dependencies used by libzero itself (without circular self-reference)
set(ZERO_LIBS
    dl
    pthread
    yaml-cpp
    jsoncpp
    ${ZLIB_LIBRARIES}
    ${OPENSSL_LIBRARIES}
)
if(Protobuf_FOUND)
    list(APPEND ZERO_LIBS ${PROTOBUF_LIBRARIES})
endif()
if(LUA_FOUND)
    list(APPEND ZERO_LIBS ${LUA_LIBRARIES})
endif()
if(MYSQLCLIENT_FOUND)
    list(APPEND ZERO_LIBS ${MYSQLCLIENT_LIBRARIES})
endif()

target_link_libraries(zero ${ZERO_LIBS})

# LIBS includes zero for executable/test targets
set(LIBS ${ZERO_LIBS} zero)

enable_testing()
add_subdirectory(tests)

zero_add_executable(echo_server "examples/echo_server.cc" zero "${LIBS}")
zero_add_executable(bench_server "examples/bench_server.cc" zero "${LIBS}")
zero_add_executable(http_bench_server "examples/http_bench_server.cc" zero "${LIBS}")
zero_add_executable(kv_server "examples/kv_server.cc" zero "${LIBS}")
zero_add_executable(ws_bench_server "examples/ws_bench_server.cc" zero "${LIBS}")

zero_add_executable(log_demo "examples/log_demo.cc" zero "${LIBS}")
zero_add_executable(log_qps_matrix "examples/log_qps_matrix.cc" zero "${LIBS}")

zero_add_executable(sentinel_server "examples/sentinel_server.cc" zero "${LIBS}")

# ---- bench2 QPS 测试工具 ----
# 服务器 (依赖 libzero)
zero_add_executable(tcp_echo_server "bench2/tcp_echo_server.cc" zero "${LIBS}")
zero_add_executable(http_echo_server "bench2/http_echo_server.cc" zero "${LIBS}")
zero_add_executable(ws_echo_server "bench2/ws_echo_server.cc" zero "${LIBS}")

# 客户端 (raw socket, 不依赖 libzero, 模拟真实场景)
add_executable(tcp_echo_client bench2/tcp_echo_client.cc)
set_target_properties(tcp_echo_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
add_executable(http_echo_client bench2/http_echo_client.cc)
set_target_properties(http_echo_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
add_executable(ws_echo_client bench2/ws_echo_client.cc)
set_target_properties(ws_echo_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
add_executable(kv_bench_client bench2/kv_bench_client.cc)
set_target_properties(kv_bench_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

add_executable(qps_client examples/qps_client.cc)
set_target_properties(qps_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
add_executable(http_qps_client examples/http_qps_client.cc)
set_target_properties(http_qps_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
add_executable(ws_qps_client examples/ws_qps_client.cc)
set_target_properties(ws_qps_client PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)

# ---- main/ - Spring Boot 风格最终版服务入口 ----
# zero_add_executable(zero_app "main/zero_app.cc" zero "${LIBS}")
# zero_add_executable(http_server "main/http_server.cc" zero "${LIBS}")
# zero_add_executable(kv_main "main/kv_server.cc" zero "${LIBS}")

SET(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
SET(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
