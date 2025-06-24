#pragma once

#include <string>
#include <cstdint>

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

    // Data operations
    size_t send(const void* data, size_t length);
    size_t receive(void* buffer, size_t length);

    // Socket options
    void set_reuse_address(bool enable);
    void set_non_blocking(bool enable);
    void set_timeout(int seconds);

    // Status
    bool is_valid() const;
    void close();

    // Get native handle
    socket_t native_handle() const { return socket_; }

private:
    socket_t socket_;
    
    static void initialize_winsock();
    static void cleanup_winsock();
    static bool winsock_initialized_;
};

} // namespace network
} // namespace netcopy

