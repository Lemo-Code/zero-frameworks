/**
 * @file kv_admin_servlet.h
 * @brief Redis Admin HTTP Servlet（/redis/info、/redis/ping）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_KV_ADMIN_KV_ADMIN_SERVLET_H__
#define __ZERO_KV_ADMIN_KV_ADMIN_SERVLET_H__

#include "zero/http/http_server.h"
#include "zero/http/servlet.h"
#include "zero/kv/replication/replication.h"
#include "zero/kv/store/kv_store.h"

namespace zero {
namespace kv {

class KvInfoServlet : public http::Servlet {
public:
    typedef std::shared_ptr<KvInfoServlet> ptr;

    explicit KvInfoServlet(KvStore::ptr store);

    int32_t handle(http::HttpRequest::ptr request,
                   http::HttpResponse::ptr response,
                   http::HttpSession::ptr session) override;

private:
    KvStore::ptr m_store;
};

class RedisPingServlet : public http::Servlet {
public:
    typedef std::shared_ptr<RedisPingServlet> ptr;

    RedisPingServlet();

    int32_t handle(http::HttpRequest::ptr request,
                   http::HttpResponse::ptr response,
                   http::HttpSession::ptr session) override;
};

class RedisSaveServlet : public http::Servlet {
public:
    typedef std::shared_ptr<RedisSaveServlet> ptr;

    explicit RedisSaveServlet(KvStore::ptr store);

    int32_t handle(http::HttpRequest::ptr request,
                   http::HttpResponse::ptr response,
                   http::HttpSession::ptr session) override;

private:
    KvStore::ptr m_store;
};

class RedisRoleServlet : public http::Servlet {
public:
    typedef std::shared_ptr<RedisRoleServlet> ptr;

    RedisRoleServlet(KvStore::ptr store, ReplicationManager::ptr repl);

    int32_t handle(http::HttpRequest::ptr request,
                   http::HttpResponse::ptr response,
                   http::HttpSession::ptr session) override;

private:
    KvStore::ptr m_store;
    ReplicationManager::ptr m_repl;
};

class RedisStatsServlet : public http::Servlet {
public:
    typedef std::shared_ptr<RedisStatsServlet> ptr;

    explicit RedisStatsServlet(KvStore::ptr store);

    int32_t handle(http::HttpRequest::ptr request,
                   http::HttpResponse::ptr response,
                   http::HttpSession::ptr session) override;

private:
    KvStore::ptr m_store;
};

void registerKvAdminServlets(const http::HttpServer::ptr& http, KvStore::ptr store,
                                ReplicationManager::ptr repl = nullptr);

} // namespace kv
} // namespace zero

#endif
