/**
 * @file db_status_servlet.h
 * @brief DB 状态 HTTP Servlet
 */

#ifndef __ZERO_HTTP_DB_STATUS_SERVLET_H__
#define __ZERO_HTTP_DB_STATUS_SERVLET_H__

#include "zero/http/servlet.h"

namespace zero {
namespace http {

class DbStatusServlet : public Servlet {
public:
    typedef std::shared_ptr<DbStatusServlet> ptr;

    DbStatusServlet();
    virtual int32_t handle(HttpRequest::ptr request,
                           HttpResponse::ptr response,
                           HttpSession::ptr session) override;
};

} // namespace http
} // namespace zero

#endif // __ZERO_HTTP_DB_STATUS_SERVLET_H__
