/**
 * @file servlet.cc
 * @brief Servlet 基类实现
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "servlet.h"
#include <fnmatch.h>

namespace zero {
namespace http {

FunctionServlet::FunctionServlet(callback cb)
    :Servlet("FunctionServlet")
    ,m_cb(cb) {
}

int32_t FunctionServlet::handle(zero::http::HttpRequest::ptr request
               , zero::http::HttpResponse::ptr response
               , zero::http::HttpSession::ptr session) {
    return m_cb(request, response, session);
}



ServletDispatch::ServletDispatch()
    :Servlet("ServletDispatch") {
    m_default.reset(new NotFoundServlet("zero/1.0"));
    rebuildRouteTable();
}

void ServletDispatch::rebuildRouteTable() {
    auto table = std::make_shared<ServletRouteTable>();
    table->default_servlet = m_default;
    table->exact.reserve(m_datas.size());
    for(auto& kv : m_datas) {
        table->exact[kv.first] = kv.second->get();
    }
    table->globs.reserve(m_globs.size());
    for(auto& kv : m_globs) {
        table->globs.emplace_back(kv.first, kv.second->get());
    }
    std::atomic_store(&m_routeTable, table);
}

int32_t ServletDispatch::handle(zero::http::HttpRequest::ptr request
               , zero::http::HttpResponse::ptr response
               , zero::http::HttpSession::ptr session) {
    auto slt = getMatchedServlet(request->getPath());
    if(slt) {
        slt->handle(request, response, session);
    }
    return 0;
}

void ServletDispatch::addServlet(const std::string& uri, Servlet::ptr slt) {
    RWMutexType::WriteLock lock(m_mutex);
    m_datas[uri] = std::make_shared<HoldServletCreator>(slt);
    rebuildRouteTable();
}

void ServletDispatch::addServletCreator(const std::string& uri, IServletCreator::ptr creator) {
    RWMutexType::WriteLock lock(m_mutex);
    m_datas[uri] = creator;
    rebuildRouteTable();
}

void ServletDispatch::addGlobServletCreator(const std::string& uri, IServletCreator::ptr creator) {
    RWMutexType::WriteLock lock(m_mutex);
    for(auto it = m_globs.begin();
            it != m_globs.end(); ++it) {
        if(it->first == uri) {
            m_globs.erase(it);
            break;
        }
    }
    m_globs.push_back(std::make_pair(uri, creator));
    rebuildRouteTable();
}

void ServletDispatch::addServlet(const std::string& uri
                        ,FunctionServlet::callback cb) {
    RWMutexType::WriteLock lock(m_mutex);
    m_datas[uri] = std::make_shared<HoldServletCreator>(
                        std::make_shared<FunctionServlet>(cb));
    rebuildRouteTable();
}

void ServletDispatch::addGlobServlet(const std::string& uri
                                    ,Servlet::ptr slt) {
    RWMutexType::WriteLock lock(m_mutex);
    for(auto it = m_globs.begin();
            it != m_globs.end(); ++it) {
        if(it->first == uri) {
            m_globs.erase(it);
            break;
        }
    }
    m_globs.push_back(std::make_pair(uri
                , std::make_shared<HoldServletCreator>(slt)));
    rebuildRouteTable();
}

void ServletDispatch::addGlobServlet(const std::string& uri
                                ,FunctionServlet::callback cb) {
    return addGlobServlet(uri, std::make_shared<FunctionServlet>(cb));
}

void ServletDispatch::delServlet(const std::string& uri) {
    RWMutexType::WriteLock lock(m_mutex);
    m_datas.erase(uri);
    rebuildRouteTable();
}

void ServletDispatch::delGlobServlet(const std::string& uri) {
    RWMutexType::WriteLock lock(m_mutex);
    for(auto it = m_globs.begin();
            it != m_globs.end(); ++it) {
        if(it->first == uri) {
            m_globs.erase(it);
            break;
        }
    }
    rebuildRouteTable();
}

Servlet::ptr ServletDispatch::getServlet(const std::string& uri) {
    RWMutexType::ReadLock lock(m_mutex);
    auto it = m_datas.find(uri);
    return it == m_datas.end() ? nullptr : it->second->get();
}

Servlet::ptr ServletDispatch::getGlobServlet(const std::string& uri) {
    RWMutexType::ReadLock lock(m_mutex);
    for(auto it = m_globs.begin();
            it != m_globs.end(); ++it) {
        if(it->first == uri) {
            return it->second->get();
        }
    }
    return nullptr;
}

Servlet::ptr ServletDispatch::getMatchedServlet(const std::string& uri) {
    auto table = std::atomic_load(&m_routeTable);
    if(!table) {
        return m_default;
    }
    auto mit = table->exact.find(uri);
    if(mit != table->exact.end()) {
        return mit->second;
    }
    for(auto it = table->globs.begin();
            it != table->globs.end(); ++it) {
        if(!fnmatch(it->first.c_str(), uri.c_str(), 0)) {
            return it->second;
        }
    }
    return table->default_servlet;
}

void ServletDispatch::listAllServletCreator(std::map<std::string, IServletCreator::ptr>& infos) {
    RWMutexType::ReadLock lock(m_mutex);
    for(auto& i : m_datas) {
        infos[i.first] = i.second;
    }
}

void ServletDispatch::listAllGlobServletCreator(std::map<std::string, IServletCreator::ptr>& infos) {
    RWMutexType::ReadLock lock(m_mutex);
    for(auto& i : m_globs) {
        infos[i.first] = i.second;
    }
}

void ServletDispatch::setDefault(Servlet::ptr v) {
    RWMutexType::WriteLock lock(m_mutex);
    m_default = v;
    rebuildRouteTable();
}

NotFoundServlet::NotFoundServlet(const std::string& name)
    :Servlet("NotFoundServlet")
    ,m_name(name) {
    m_content = "<html><head><title>404 Not Found"
        "</title></head><body><center><h1>404 Not Found</h1></center>"
        "<hr><center>" + name + "</center></body></html>";

}

int32_t NotFoundServlet::handle(zero::http::HttpRequest::ptr request
                   , zero::http::HttpResponse::ptr response
                   , zero::http::HttpSession::ptr session) {
    response->setStatus(zero::http::HttpStatus::NOT_FOUND);
    response->setHeader("Server", "zero/1.0.0");
    response->setHeader("Content-Type", "text/html");
    response->setBody(m_content);
    return 0;
}


}
}
