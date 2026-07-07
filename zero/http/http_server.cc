/**
 * @file http_server.cc
 * @brief HTTP 服务器实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "http_server.h"
#include "zero/core/log/log.h"
#include "zero/http/servlets/config_servlet.h"
#include "zero/http/servlets/status_servlet.h"

namespace zero {
namespace http {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("system");

HttpServer::HttpServer(bool keepalive
               ,zero::IOManager* worker
               ,zero::IOManager* io_worker
               ,zero::IOManager* accept_worker)
    :TcpServer(worker, io_worker, accept_worker)
    ,m_isKeepalive(keepalive) {
    m_dispatch.reset(new ServletDispatch);

    m_type = "http";
    m_dispatch->addServlet("/_/status", Servlet::ptr(new StatusServlet));
    m_dispatch->addServlet("/_/config", Servlet::ptr(new ConfigServlet));
}

void HttpServer::setName(const std::string& v) {
    TcpServer::setName(v);
    m_dispatch->setDefault(std::make_shared<NotFoundServlet>(v));
}

void HttpServer::addFixedRoute(const std::string& path,
                               const char* keepalive, size_t keepaliveLen,
                               const char* closeWire, size_t closeLen) {
    FixedRouteWire wire;
    wire.keepalive = keepalive;
    wire.keepaliveLen = keepaliveLen;
    wire.close = closeWire;
    wire.closeLen = closeLen;
    m_fixedRoutes[path] = wire;
}

void HttpServer::handleClient(Socket::ptr client) {
    ZERO_LOG_DEBUG(g_logger) << "handleClient " << *client;
    HttpSession::ptr session(new HttpSession(client));
    if(m_minimalParse) {
        session->setMinimalParse(true);
    }

    if(m_minimalParse && m_fixedRoutes.size() == 1) {
        const auto& entry = *m_fixedRoutes.begin();
        const std::string& routePath = entry.first;
        const FixedRouteWire& wire = entry.second;
        do {
            bool clientClose = false;
            int rt = session->recvFixedRouteGet(routePath.data(), routePath.size(), &clientClose);
            if(rt < 0) {
                break;
            }
            if(rt == 0) {
                auto req = session->recvRequest();
                if(!req) {
                    break;
                }
                auto fit = m_fixedRoutes.find(req->getPath());
                if(fit != m_fixedRoutes.end()) {
                    const FixedRouteWire& rw = fit->second;
                    bool close = req->isClose() || !m_isKeepalive;
                    if(close) {
                        session->sendRaw(rw.close, rw.closeLen);
                    } else {
                        session->sendRaw(rw.keepalive, rw.keepaliveLen);
                    }
                    if(close) {
                        break;
                    }
                    continue;
                }
                HttpResponse::ptr rsp = session->getResponse(req->getVersion()
                                    ,req->isClose() || !m_isKeepalive);
                rsp->setHeader("Server", getName());
                m_dispatch->handle(req, rsp, session);
                if(rsp->hasFixedWire()) {
                    session->sendRaw(rsp->fixedWireData(), rsp->fixedWireLen());
                } else {
                    session->sendResponse(rsp);
                }
                if(!m_isKeepalive || req->isClose()) {
                    break;
                }
                continue;
            }

            bool close = clientClose || !m_isKeepalive;
            if(close) {
                session->sendRaw(wire.close, wire.closeLen);
            } else {
                session->sendRaw(wire.keepalive, wire.keepaliveLen);
            }
            if(close) {
                break;
            }
        } while(true);
        session->close();
        return;
    }

    do {
        auto req = session->recvRequest();
        if(!req) {
            ZERO_LOG_DEBUG(g_logger) << "recv http request fail, errno="
                << errno << " errstr=" << strerror(errno)
                << " cliet:" << *client << " keep_alive=" << m_isKeepalive;
            break;
        }

        auto fit = m_fixedRoutes.find(req->getPath());
        if(fit != m_fixedRoutes.end()) {
            const FixedRouteWire& wire = fit->second;
            bool close = req->isClose() || !m_isKeepalive;
            if(close) {
                session->sendRaw(wire.close, wire.closeLen);
            } else {
                session->sendRaw(wire.keepalive, wire.keepaliveLen);
            }
        } else {
            HttpResponse::ptr rsp = session->getResponse(req->getVersion()
                                ,req->isClose() || !m_isKeepalive);
            rsp->setHeader("Server", getName());
            m_dispatch->handle(req, rsp, session);
            if(rsp->hasFixedWire()) {
                session->sendRaw(rsp->fixedWireData(), rsp->fixedWireLen());
            } else {
                session->sendResponse(rsp);
            }
        }

        if(!m_isKeepalive || req->isClose()) {
            break;
        }
    } while(true);
    session->close();
}

}
}
