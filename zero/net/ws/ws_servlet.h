/**
 * @file ws_servlet.h
 * @brief WebSocket Servlet 定义
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __HTTP_WS_SERVLET_H__
#define __HTTP_WS_SERVLET_H__

#include "ws_session.h"
#include "zero/core/concurrency/thread.h"
#include "zero/http/servlet.h"

namespace zero {
namespace http {

class WSServlet : public Servlet {
public:
    typedef std::shared_ptr<WSServlet> ptr;
    WSServlet(const std::string& name)
        :Servlet(name) {
    }
    virtual ~WSServlet() {}

    virtual int32_t handle(zero::http::HttpRequest::ptr request
                   , zero::http::HttpResponse::ptr response
                   , zero::http::HttpSession::ptr session) override {
        return 0;
    }

    virtual int32_t onConnect(zero::http::HttpRequest::ptr header
                              ,zero::http::WSSession::ptr session) = 0;
    virtual int32_t onClose(zero::http::HttpRequest::ptr header
                             ,zero::http::WSSession::ptr session) = 0;
    virtual int32_t handle(zero::http::HttpRequest::ptr header
                           ,zero::http::WSFrameMessage::ptr msg
                           ,zero::http::WSSession::ptr session) = 0;
    const std::string& getName() const { return m_name;}
protected:
    std::string m_name;
};

class FunctionWSServlet : public WSServlet {
public:
    typedef std::shared_ptr<FunctionWSServlet> ptr;
    typedef std::function<int32_t (zero::http::HttpRequest::ptr header
                              ,zero::http::WSSession::ptr session)> on_connect_cb;
    typedef std::function<int32_t (zero::http::HttpRequest::ptr header
                             ,zero::http::WSSession::ptr session)> on_close_cb; 
    typedef std::function<int32_t (zero::http::HttpRequest::ptr header
                           ,zero::http::WSFrameMessage::ptr msg
                           ,zero::http::WSSession::ptr session)> callback;

    FunctionWSServlet(callback cb
                      ,on_connect_cb connect_cb = nullptr
                      ,on_close_cb close_cb = nullptr);

    virtual int32_t onConnect(zero::http::HttpRequest::ptr header
                              ,zero::http::WSSession::ptr session) override;
    virtual int32_t onClose(zero::http::HttpRequest::ptr header
                             ,zero::http::WSSession::ptr session) override;
    virtual int32_t handle(zero::http::HttpRequest::ptr header
                           ,zero::http::WSFrameMessage::ptr msg
                           ,zero::http::WSSession::ptr session) override;
protected:
    callback m_callback;
    on_connect_cb m_onConnect;
    on_close_cb m_onClose;
};

class WSServletDispatch : public ServletDispatch {
public:
    typedef std::shared_ptr<WSServletDispatch> ptr;
    typedef RWMutex RWMutexType;

    WSServletDispatch();
    void addServlet(const std::string& uri
                    ,FunctionWSServlet::callback cb
                    ,FunctionWSServlet::on_connect_cb connect_cb = nullptr
                    ,FunctionWSServlet::on_close_cb close_cb = nullptr);
    void addGlobServlet(const std::string& uri
                    ,FunctionWSServlet::callback cb
                    ,FunctionWSServlet::on_connect_cb connect_cb = nullptr
                    ,FunctionWSServlet::on_close_cb close_cb = nullptr);
    WSServlet::ptr getWSServlet(const std::string& uri);
};

}
}

#endif
