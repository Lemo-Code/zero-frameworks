/**
 * @file kv_session.h
 * @brief Redis 连接会话（类比 http::HttpSession）
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_KV_KV_SESSION_H__
#define __ZERO_KV_KV_SESSION_H__

#include "kv_context.h"
#include "resp.h"
#include "zero/streams/socket_stream.h"
#include <vector>

namespace zero {
namespace kv {

class KvSession : public SocketStream {
public:
    typedef std::shared_ptr<KvSession> ptr;

    KvSession(Socket::ptr sock, bool owner = true);

    /**
     * @brief 读一条完整 RESP 命令（数组）
     * @return nullptr 表示连接关闭或协议错误
     */
    bool recvCommand(RespValue& out);

    /** @brief 不读 socket，仅解析 buffer 中下一条命令 */
    bool tryRecvCommand(RespValue& out);

    void appendResponse(const RespValue& rsp);
    /** Fast-path: directly append pre-encoded RESP bytes without encodeInto() */
    void appendPreEncoded(const std::string& encoded);
    int flushResponses();
    int sendResponse(const RespValue& rsp);

    KvContext& context() { return m_ctx; }
    const KvContext& context() const { return m_ctx; }

    /** Pub/Sub 推送（立即 write） */
    int pushPubSubMessage(const RespValue& msg);

    /** 复制流推送（已 RESP 编码的字节） */
    int pushReplicationPayload(const std::string& encoded);

private:
    void compactIfNeeded();

    KvContext m_ctx;
    std::vector<char> m_buffer;
    size_t m_readPos = 0;
    size_t m_dataLen = 0;
    std::string m_writeBuf;
};

} // namespace kv
} // namespace zero

#endif
