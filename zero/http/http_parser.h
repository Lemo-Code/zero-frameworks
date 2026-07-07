/**
 * @file http_parser.h
 * @brief HTTP协议解析封装
 * @author lemo
 * @email 2270338643@qq.com
 * @date 2026-07-05
 * @copyright Copyright (c) 2026年 zero-framework All rights reserved
 */

#ifndef __ZERO_HTTP_PARSER_H__
#define __ZERO_HTTP_PARSER_H__

#include "http.h"
#include "http11_parser.h"
#include "httpclient_parser.h"

namespace zero {
namespace http {

/**
 * @brief HTTP请求解析类
 */
class HttpRequestParser {
public:
    /// HTTP解析类的智能指针
    typedef std::shared_ptr<HttpRequestParser> ptr;

    /**
     * @brief 构造函数
     */
    HttpRequestParser();

    /**
     * @brief 解析协议
     * @param[in] data 协议文本内存
     * @param[in] len 协议文本内存长度
     * @return 已消费的字节数
     */
    size_t execute(const char* data, size_t len);

    /**
     * @brief 是否解析完成
     * @return 是否解析完成
     */
    int isFinished();

    /**
     * @brief 是否有错误
     * @return 是否有错误
     */
    int hasError(); 

    /**
     * @brief 返回HttpRequest结构体
     */
    HttpRequest::ptr getData() const { return m_data;}

    /**
     * @brief 设置错误
     * @param[in] v 错误值
     */
    void setError(int v) { m_error = v;}

    /**
     * @brief 重置解析器，复用 HttpRequest 与内部状态
     */
    void reset();

    /**
     * @brief 仅保留路由/握手所需 header，跳过 Host/User-Agent 等
     */
    void setMinimalHeaders(bool v) { m_minimalHeaders = v; }
    bool isMinimalHeaders() const { return m_minimalHeaders; }

    void setContentLengthValue(uint64_t v) { m_contentLength = v; }

    /**
     * @brief 获取消息体长度
     */
    uint64_t getContentLength();

    /**
     * @brief 获取http_parser结构体
     */
    const http_parser& getParser() const { return m_parser;}

    /**
     * @brief 返回HttpRequest协议解析的缓存大小
     */
    static uint64_t GetHttpRequestBufferSize();

    /**
     * @brief 返回HttpRequest协议的最大消息体大小
     */
    static uint64_t GetHttpRequestMaxBodySize();
private:
    /// http_parser
    http_parser m_parser;
    /// HttpRequest结构
    HttpRequest::ptr m_data;
    /// 错误码
    /// 1000: invalid method
    /// 1001: invalid version
    /// 1002: invalid field
    int m_error;
    bool m_minimalHeaders = false;
    uint64_t m_contentLength = 0;
};

/**
 * @brief Http响应解析结构体
 */
class HttpResponseParser {
public:
    /// 智能指针类型
    typedef std::shared_ptr<HttpResponseParser> ptr;

    /**
     * @brief 构造函数
     */
    HttpResponseParser();

    /**
     * @brief 解析HTTP响应协议
     * @param[in, out] data 协议数据内存
     * @param[in] len 协议数据内存大小
     * @param[in] chunck 是否在解析chunck
     * @return 返回实际解析的长度,并且移除已解析的数据
     */
    size_t execute(char* data, size_t len, bool chunck);

    /**
     * @brief 是否解析完成
     */
    int isFinished();

    /**
     * @brief 是否有错误
     */
    int hasError(); 

    /**
     * @brief 返回HttpResponse
     */
    HttpResponse::ptr getData() const { return m_data;}

    /**
     * @brief 设置错误码
     * @param[in] v 错误码
     */
    void setError(int v) { m_error = v;}

    /**
     * @brief 获取消息体长度
     */
    uint64_t getContentLength();

    /**
     * @brief 返回httpclient_parser
     */
    const httpclient_parser& getParser() const { return m_parser;}
public:
    /**
     * @brief 返回HTTP响应解析缓存大小
     */
    static uint64_t GetHttpResponseBufferSize();

    /**
     * @brief 返回HTTP响应最大消息体大小
     */
    static uint64_t GetHttpResponseMaxBodySize();
private:
    /// httpclient_parser
    httpclient_parser m_parser;
    /// HttpResponse
    HttpResponse::ptr m_data;
    /// 错误码
    /// 1001: invalid version
    /// 1002: invalid field
    int m_error;
};

}
}

#endif
