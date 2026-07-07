/**
 * @file kv_admin_servlet.cc
 * @brief KV 管理 Servlet 实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "kv_admin_servlet.h"
#include "zero/kv/info.h"
#include "zero/util/json_util.h"

#include <json/json.h>
#include <cstdlib>
#include <sstream>

namespace zero {
namespace kv {

namespace {

int parseDbFromQuery(http::HttpRequest::ptr request) {
    const std::string db_str = request->getParam("db", "0");
    char* end = nullptr;
    long n = std::strtol(db_str.c_str(), &end, 10);
    if(end == db_str.c_str() || *end != '\0' || n < 0 || n > 15) {
        return 0;
    }
    return (int)n;
}

bool isGet(http::HttpRequest::ptr request) {
    return request->getMethod() == http::HttpMethod::GET;
}

} // namespace

KvInfoServlet::KvInfoServlet(KvStore::ptr store)
    :http::Servlet("KvInfoServlet")
    ,m_store(std::move(store)) {
}

int32_t KvInfoServlet::handle(http::HttpRequest::ptr request,
                                 http::HttpResponse::ptr response,
                                 http::HttpSession::ptr) {
    if(!isGet(request)) {
        response->setStatus(http::HttpStatus::METHOD_NOT_ALLOWED);
        response->setBody("method not allowed");
        return 0;
    }
    if(!m_store) {
        response->setStatus(http::HttpStatus::INTERNAL_SERVER_ERROR);
        response->setBody("store unavailable");
        return 0;
    }

    const int db = parseDbFromQuery(request);
    const std::string format = request->getParam("format", "text");
    if(format == "json") {
        response->setHeader("Content-Type", "application/json; charset=utf-8");
        response->setBody(buildInfoJson(db, m_store));
    } else {
        response->setHeader("Content-Type", "text/plain; charset=utf-8");
        response->setBody(buildInfoText(db, m_store));
    }
    response->setStatus(http::HttpStatus::OK);
    return 0;
}

RedisPingServlet::RedisPingServlet()
    :http::Servlet("RedisPingServlet") {
}

int32_t RedisPingServlet::handle(http::HttpRequest::ptr request,
                                 http::HttpResponse::ptr response,
                                 http::HttpSession::ptr) {
    if(!isGet(request)) {
        response->setStatus(http::HttpStatus::METHOD_NOT_ALLOWED);
        response->setBody("method not allowed");
        return 0;
    }
    response->setHeader("Content-Type", "text/plain; charset=utf-8");
    response->setBody("PONG");
    response->setStatus(http::HttpStatus::OK);
    return 0;
}

RedisSaveServlet::RedisSaveServlet(KvStore::ptr store)
    :http::Servlet("RedisSaveServlet")
    ,m_store(std::move(store)) {
}

int32_t RedisSaveServlet::handle(http::HttpRequest::ptr request,
                                  http::HttpResponse::ptr response,
                                  http::HttpSession::ptr) {
    if(!isGet(request)) {
        response->setStatus(http::HttpStatus::METHOD_NOT_ALLOWED);
        response->setBody("method not allowed");
        return 0;
    }
    if(!m_store) {
        response->setStatus(http::HttpStatus::INTERNAL_SERVER_ERROR);
        response->setBody("store unavailable");
        return 0;
    }
    std::string err;
    if(!m_store->saveRdb(&err)) {
        response->setStatus(http::HttpStatus::INTERNAL_SERVER_ERROR);
        response->setHeader("Content-Type", "text/plain; charset=utf-8");
        response->setBody(err.empty() ? "save failed" : err);
        return 0;
    }
    response->setHeader("Content-Type", "text/plain; charset=utf-8");
    response->setBody("OK");
    response->setStatus(http::HttpStatus::OK);
    return 0;
}

RedisRoleServlet::RedisRoleServlet(KvStore::ptr store, ReplicationManager::ptr repl)
    :http::Servlet("RedisRoleServlet")
    ,m_store(std::move(store))
    ,m_repl(std::move(repl)) {
}

int32_t RedisRoleServlet::handle(http::HttpRequest::ptr request,
                                 http::HttpResponse::ptr response,
                                 http::HttpSession::ptr) {
    if(!isGet(request)) {
        response->setStatus(http::HttpStatus::METHOD_NOT_ALLOWED);
        response->setBody("method not allowed");
        return 0;
    }
    Json::Value root;
    if(m_repl && m_repl->role() == ReplicationManager::Role::Master) {
        root["role"] = "master";
        root["repl_id"] = m_repl->replId();
        root["repl_offset"] = (Json::Int64)m_repl->replOffset();
        root["connected_slaves"] = m_repl->connectedSlaves();
    } else if(m_repl) {
        root["role"] = "slave";
        root["master_host"] = m_repl->masterHost();
        root["master_port"] = m_repl->masterPort();
        root["master_link_status"] = (Json::Int64)m_repl->masterLinkStatus();
        root["master_repl_id"] = m_repl->masterReplId();
        root["master_repl_offset"] = (Json::Int64)m_repl->masterReplOffset();
    } else {
        root["role"] = "unknown";
    }
    response->setHeader("Content-Type", "application/json; charset=utf-8");
    response->setBody(JsonUtil::ToString(root));
    response->setStatus(http::HttpStatus::OK);
    return 0;
}

RedisStatsServlet::RedisStatsServlet(KvStore::ptr store)
    :http::Servlet("RedisStatsServlet")
    ,m_store(std::move(store)) {
}

int32_t RedisStatsServlet::handle(http::HttpRequest::ptr request,
                                  http::HttpResponse::ptr response,
                                  http::HttpSession::ptr) {
    if(!isGet(request)) {
        response->setStatus(http::HttpStatus::METHOD_NOT_ALLOWED);
        response->setBody("method not allowed");
        return 0;
    }
    if(!m_store) {
        response->setStatus(http::HttpStatus::INTERNAL_SERVER_ERROR);
        response->setBody("store unavailable");
        return 0;
    }
    const int db = parseDbFromQuery(request);
    Json::Value root;
    root["db"] = db;
    root["dbsize"] = (Json::Int64)m_store->dbsize(db);
    root["used_memory"] = (Json::Int64)m_store->usedMemoryApprox();
    root["maxmemory"] = (Json::Int64)m_store->maxMemory();
    root["maxmemory_policy"] = m_store->maxMemoryPolicy();
    response->setHeader("Content-Type", "application/json; charset=utf-8");
    response->setBody(JsonUtil::ToString(root));
    response->setStatus(http::HttpStatus::OK);
    return 0;
}

void registerKvAdminServlets(const http::HttpServer::ptr& http, KvStore::ptr store,
                                ReplicationManager::ptr repl) {
    if(!http || !store) {
        return;
    }
    http::ServletDispatch::ptr dispatch = http->getServletDispatch();
    dispatch->addServlet("/redis/info", std::make_shared<KvInfoServlet>(store));
    dispatch->addServlet("/redis/ping", std::make_shared<RedisPingServlet>());
    dispatch->addServlet("/redis/save", std::make_shared<RedisSaveServlet>(store));
    dispatch->addServlet("/redis/role", std::make_shared<RedisRoleServlet>(store, repl));
    dispatch->addServlet("/redis/stats", std::make_shared<RedisStatsServlet>(store));
}

} // namespace kv
} // namespace zero
