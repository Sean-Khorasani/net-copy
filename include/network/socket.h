#pragma once

// Pre-include wolfSSL options and force compatibility macros
// before including any wolfSSL/OpenSSL headers, because options.h will otherwise undefine them.
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

#include <string>
#include <cstdint>
#include <wolfssl/openssl/ssl.h>
#include <wolfssl/openssl/err.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef SOCKET socket_t;
    #define INVALID_SOCKET_VALUE INVALID_SOCKET
    #define SOCKET_ERROR_VALUE SOCKET_ERROR
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <fcntl.h>
    typedef int socket_t;
    #define INVALID_SOCKET_VALUE -1
    #define SOCKET_ERROR_VALUE -1
    #define closesocket close
#endif

namespace netcopy {
namespace network {

class Socket {
public:
    Socket();
    explicit Socket(socket_t sock);
    ~Socket();

    // Non-copyable
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    // Movable
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    // Server operations
    void bind(const std::string& address, uint16_t port);
    void listen(int backlog = 5);
    Socket accept();

    // Client operations
    void connect(const std::string& address, uint16_t port);
    void connect_via_proxy(const std::string& address, uint16_t port,
                           const std::string& proxy_type, const std::string& proxy_host, uint16_t proxy_port,
                           const std::string& proxy_username = "", const std::string& proxy_password = "");

    // TLS support (wolfSSL-backed, OpenSSL compat API)
    void enable_tls_client(bool verify = true, const std::string& ca_file = "");
    void enable_tls_server(const std::string& cert_file, const std::string& key_file, const std::string& dh_file = "");
    void perform_tls_handshake();
    bool is_tls() const { return ssl_ != nullptr; }

    // Data operations
    size_t send(const void* data, size_t length);
    size_t receive(void* buffer, size_t length);

    // Socket options
    void set_reuse_address(bool enable);
    void set_non_blocking(bool enable);
    void set_timeout(int seconds);
    void set_tcp_nodelay(bool enable);
    void set_buffer_sizes(size_t send_size, size_t recv_size);

    // Status
    bool is_valid() const;
    void close();

    // Get remote peer address as "ip:port" string (only valid for accepted sockets)
    std::string get_peer_address() const;

    // Get native handle
    socket_t native_handle() const { return socket_; }
    socket_t release();

    // UDP/R-UDP support
    void set_udp(bool enable);
    bool is_udp() const { return is_udp_; }

private:
    socket_t socket_;
    bool reuse_address_ = true;
    bool is_udp_ = false;
    bool udp_connected_ = false;
    struct sockaddr_in udp_remote_addr_;
    uint32_t send_seq_ = 0;
    uint32_t recv_seq_ = 0;
    
    // TLS members — wolfSSL types are typedef'd to SSL/SSL_CTX via compat layer
    SSL*     ssl_     = nullptr;
    SSL_CTX* ssl_ctx_ = nullptr;
    bool is_tls_client_ = false;
    
    static void initialize_winsock();
    static void cleanup_winsock();
    static bool winsock_initialized_;
};

} // namespace network
} // namespace netcopy
