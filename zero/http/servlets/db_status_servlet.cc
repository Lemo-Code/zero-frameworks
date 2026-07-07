/**
 * @file db_status_servlet.cc
 * @brief DB 状态 HTTP Servlet
 */

#include "db_status_servlet.h"
#include "zero/db/db_manager.h"

namespace zero {
namespace http {

DbStatusServlet::DbStatusServlet() : Servlet("DbStatusServlet") {}

int32_t DbStatusServlet::handle(HttpRequest::ptr request,
                                 HttpResponse::ptr response,
                                 HttpSession::ptr session) {
    (void)session;
    auto* mgr = db::DbManager::instance();
    std::stringstream ss;
    ss << "{\n";
    ss << "  \"status\": \"ok\",\n";
    auto def = mgr->session();
    ss << "  \"has_default_session\": " << (def != nullptr ? "true" : "false") << "\n";
    ss << "}\n";
    response->setStatus(HttpStatus::OK);
    response->setHeader("Content-Type", "application/json");
    response->setBody(ss.str());
    return 0;
}

} // namespace http
} // namespace zero
