add_subdirectory(support)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/support/include)
message(STATUS "LIBS available in tests: ${LIBS}")

# Helper to add a gtest-based test executable and register it with CTest.
function(zero_add_gtest targetname srcs)
    add_executable(${targetname} ${srcs})
    add_dependencies(${targetname} zero)
    force_redefine_file_macro_for_sources(${targetname})
    # LIBS is defined in the parent CMakeLists.txt and already contains zero,
    # pthread, yaml-cpp, jsoncpp, zlib, openssl, protobuf, mysql, etc.
    target_link_libraries(${targetname}
        ${LIBS}
        zero_test_support
        GTest::gtest_main
        GTest::gmock
    )
    set_target_properties(${targetname} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin
    )
    add_test(NAME ${targetname} COMMAND ${targetname})
endfunction()


# Phase A/B unit tests (core).
zero_add_gtest(test_util_sample unit/core/test_util.cc)
zero_add_gtest(test_bytearray unit/core/test_bytearray.cc)
zero_add_gtest(test_address unit/core/test_address.cc)
zero_add_gtest(test_fd_manager unit/core/test_fd_manager.cc)
zero_add_gtest(test_iomanager unit/core/test_iomanager.cc)
zero_add_gtest(test_socket unit/core/test_socket.cc)
zero_add_gtest(test_fiber unit/core/test_fiber.cc)
zero_add_gtest(test_timer unit/core/test_timer.cc)
zero_add_gtest(test_mutex unit/core/test_mutex.cc)
zero_add_gtest(test_thread unit/core/test_thread.cc)
zero_add_gtest(test_log unit/core/test_log.cc)
zero_add_gtest(test_config_var unit/core/test_config_var.cc)

# Phase C unit tests (net / http).
zero_add_gtest(test_socket_stream unit/net/test_socket_stream.cc)
zero_add_gtest(test_zlib_stream unit/net/test_zlib_stream.cc)
zero_add_gtest(test_http_parser unit/http/test_http_parser.cc)


# Phase E unit tests (util / cache / config / db / kv / rpc / security / http)
zero_add_gtest(test_jwt unit/util/test_jwt.cc)
zero_add_gtest(test_config unit/core/test_config.cc)
zero_add_gtest(test_config_center unit/config/test_config_center.cc)
zero_add_gtest(test_kv_info unit/kv/test_kv_info.cc)
zero_add_gtest(test_kv_resp unit/kv/test_kv_resp.cc)
zero_add_gtest(test_kv_rdb unit/kv/test_kv_rdb.cc)
zero_add_gtest(test_kv_pubsub unit/kv/test_kv_pubsub.cc)
zero_add_gtest(test_kv_aof unit/kv/test_kv_aof.cc)
zero_add_gtest(test_kv_repl unit/kv/test_kv_repl.cc)
zero_add_gtest(test_kv_p0p1 unit/kv/test_kv_p0p1.cc)
zero_add_gtest(test_kv_phase10 unit/kv/test_kv_phase10.cc)
zero_add_gtest(test_kv_single unit/kv/test_kv_single.cc)
zero_add_gtest(test_rpc_mini unit/rpc/test_rpc_mini.cc)
zero_add_gtest(test_rpc_client unit/rpc/test_rpc_client.cc)
zero_add_gtest(test_rpc_load_balancer unit/rpc/test_rpc_load_balancer.cc)
zero_add_gtest(test_rpc_server unit/rpc/test_rpc_server.cc)
zero_add_gtest(test_rpc_v2 unit/rpc/test_rpc_v2.cc)
zero_add_gtest(test_http2 unit/http/test_http2.cc)
zero_add_gtest(test_http_server unit/http/test_http_server.cc)
zero_add_gtest(test_cert_manager unit/security/test_cert_manager.cc)
zero_add_gtest(test_fiber_stack unit/core/test_fiber_stack.cc)

# Phase F unit tests (security)  -- db/orm tests migrated to /home/wangmaosen/project/kv/orm
# zero_add_gtest(test_middleware unit/http/test_middleware.cc)  # depends on non-existent MiddlewareServlet/RBACMiddleware
zero_add_gtest(test_security unit/security/test_security.cc)

# Phase G unit tests (core extensions / log system / production)
zero_add_gtest(test_framework_extensions unit/core/test_framework_extensions.cc)
zero_add_gtest(test_log_system unit/core/test_log_system.cc)
zero_add_gtest(test_production_features unit/core/test_production_features.cc)
# zero_add_gtest(test_remaining_features unit/core/test_remaining_features.cc)  # uses non-existent addRestServlet/addGlobalMiddleware

# Phase H unit tests (kv cluster / commands / blocking / extended / repl e2e)
zero_add_gtest(test_kv_commands unit/kv/test_kv_commands.cc)
# zero_add_gtest(test_kv_cluster unit/kv/test_kv_cluster.cc)  # depends on excluded cluster module
zero_add_gtest(test_kv_blocking unit/kv/test_kv_blocking.cc)
zero_add_gtest(test_kv_extended unit/kv/test_kv_extended.cc)
zero_add_gtest(test_kv_repl_e2e unit/kv/test_kv_repl_e2e.cc)

# Phase I unit tests (rpc / service governance / etcd integration / qps)
zero_add_gtest(test_rpc unit/rpc/test_rpc.cc)
zero_add_gtest(test_rpc_etcd_integration unit/rpc/test_rpc_etcd_integration.cc)
zero_add_gtest(test_rpc_qps unit/rpc/test_rpc_qps.cc)
# Phase J -- db/orm tests migrated to /home/wangmaosen/project/kv/orm
zero_add_gtest(test_service_governance unit/rpc/test_service_governance.cc)

