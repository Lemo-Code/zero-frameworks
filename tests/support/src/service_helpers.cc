/**
 * @file service_helpers.cc
 * @brief 测试支持 - service_helpers
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "zero_test/service_helpers.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>

namespace zero_test {

bool is_reachable(const std::string& host, int port, int timeout_ms) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    // Non-blocking connect with poll.
    int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        ::close(fd);
        return false;
    }

    int rc = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    if (rc == 0) {
        ::close(fd);
        return true;
    }
    if (errno != EINPROGRESS) {
        ::close(fd);
        return false;
    }

    pollfd pfd{fd, POLLOUT, 0};
    rc = ::poll(&pfd, 1, timeout_ms);
    bool ok = false;
    if (rc > 0) {
        int soerr = 0;
        socklen_t len = sizeof(soerr);
        if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &len) == 0 && soerr == 0) {
            ok = true;
        }
    }
    ::close(fd);
    return ok;
}

} // namespace zero_test
