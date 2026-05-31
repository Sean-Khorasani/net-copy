#pragma once

// Pre-include wolfSSL options and force compatibility macros
// before including ASIO SSL, because options.h will otherwise undefine them.
#include <wolfssl/options.h>
#ifndef WOLFSSL_OPTIONS_H
#define WOLFSSL_OPTIONS_H
#endif
#ifndef ASIO_USE_WOLFSSL
#define ASIO_USE_WOLFSSL
#endif
#ifndef OPENSSL_ALL
#define OPENSSL_ALL
#endif
#ifndef WOLFSSL_ASIO
#define WOLFSSL_ASIO
#endif
#ifndef OPENSSL_EXTRA
#define OPENSSL_EXTRA
#endif
#ifndef OPENSSL_NO_SSL2
#define OPENSSL_NO_SSL2
#endif
#ifndef OPENSSL_NO_SSL3
#define OPENSSL_NO_SSL3
#endif

#include "network/event_loop.h"
#include <asio.hpp>
#include <asio/ssl.hpp>
#include <memory>
#include <string>
#include <functional>
#include <vector>

namespace netcopy {
namespace network {

class AsyncSocket : public std::enable_shared_from_this<AsyncSocket> {
public:
    using Ptr = std::shared_ptr<AsyncSocket>;
    using ErrorCode = asio::error_code;
    
    // Create an un-connected socket attached to an event loop
    explicit AsyncSocket(EventLoop& loop);
    ~AsyncSocket();

    // Move from an existing accepted native socket (used by server)
    AsyncSocket(EventLoop& loop, asio::ip::tcp::socket socket);

    // Disable copy
    AsyncSocket(const AsyncSocket&) = delete;
    AsyncSocket& operator=(const AsyncSocket&) = delete;

    void connect(const std::string& host, uint16_t port, std::function<void(ErrorCode)> handler);
    void disconnect();

    // TLS support
    void enable_tls(asio::ssl::context& ctx);
    void async_handshake(asio::ssl::stream_base::handshake_type type, std::function<void(ErrorCode)> handler);
    bool is_tls() const { return ssl_stream_ != nullptr; }

    // Async I/O operations
    void async_read(void* buffer, size_t length, std::function<void(ErrorCode, size_t)> handler);
    void async_write(const void* buffer, size_t length, std::function<void(ErrorCode, size_t)> handler);
    void async_write(const std::vector<asio::const_buffer>& buffers, std::function<void(ErrorCode, size_t)> handler);

    // Zero-copy file send using TransmitFile / sendfile natively
    // On failure or unsupported platforms, falls back to fast_mem user-space copying
    void async_send_file(const std::string& filepath, uint64_t offset, uint64_t length, std::function<void(ErrorCode, size_t)> handler);

    bool is_open() const;
    std::string get_peer_address() const;

    asio::ip::tcp::socket& native_socket() { return socket_; }

private:
    EventLoop& loop_;
    asio::ip::tcp::socket socket_;
    std::unique_ptr<asio::ssl::stream<asio::ip::tcp::socket>> ssl_stream_;
};

} // namespace network
} // namespace netcopy
