/**
 * @file rpc_server.cc
 * @brief RPC 模块 - rpc_server
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#include "rpc_server.h"

#include "zero/core/log/log.h"
#include "zero/core/io/socket.h"
#include "zero/streams/socket_stream.h"

#include <arpa/inet.h>

namespace zero {
namespace rpc {

static zero::Logger::ptr g_logger = ZERO_LOG_NAME("rpc");

namespace {

uint32_t decodeBe32(const uint8_t* in) {
    return ((uint32_t)in[0] << 24) |
           ((uint32_t)in[1] << 16) |
           ((uint32_t)in[2] << 8) |
           (uint32_t)in[3];
}

void encodeBe32(uint32_t val, uint8_t* out) {
    out[0] = (val >> 24) & 0xFF;
    out[1] = (val >> 16) & 0xFF;
    out[2] = (val >> 8) & 0xFF;
    out[3] = val & 0xFF;
}

} // namespace

RpcServer::RpcServer(RpcHandler dispatch,
                     IOManager* worker, IOManager* io_worker,
                     IOManager* accept_worker)
    : TcpServer(worker, io_worker, accept_worker)
    , m_dispatch(std::move(dispatch)) {
    m_type = "rpc";
    setName("zero-rpc");
}

void RpcServer::handleClient(Socket::ptr client) {
    ZERO_LOG_DEBUG(g_logger) << "rpc handleClient " << *client;
    // SocketStream uses hooked I/O — safe in fiber context
    SocketStream::ptr stream(new SocketStream(client, false));

    uint8_t lenBuf[4];
    std::string wireBuf;
    wireBuf.reserve(65536);

    while (client && client->isConnected()) {
        // Read 4-byte BE length prefix
        if (stream->readFixSize(lenBuf, 4) <= 0) break;

        uint32_t msgLen = decodeBe32(lenBuf);
        if (msgLen == 0 || msgLen > 64 * 1024 * 1024) {
            ZERO_LOG_WARN(g_logger) << "rpc: invalid message length " << msgLen;
            break;
        }

        wireBuf.resize(msgLen);
        if (stream->readFixSize(&wireBuf[0], msgLen) <= 0) break;

        RpcEnvelope request;
        if (!request.ParseFromString(wireBuf)) {
            ZERO_LOG_WARN(g_logger) << "rpc: failed to parse request";
            break;
        }

        RpcEnvelope response;
        response.set_request_id(request.request_id());
        m_dispatch(request, response);

        std::string respWire;
        if (!response.SerializeToString(&respWire)) {
            ZERO_LOG_WARN(g_logger) << "rpc: failed to serialize response";
            break;
        }

        uint8_t respLenBuf[4];
        encodeBe32(static_cast<uint32_t>(respWire.size()), respLenBuf);
        if (stream->writeFixSize(respLenBuf, 4) <= 0) break;
        if (stream->writeFixSize(respWire.data(), respWire.size()) <= 0) break;
    }

    ZERO_LOG_DEBUG(g_logger) << "rpc: client disconnected";
}

} // namespace rpc
} // namespace zero
