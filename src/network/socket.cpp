#include "network/socket.h"
#include "exceptions.h"
#include <cstring>

namespace netcopy {
namespace network {

#ifdef _WIN32
bool Socket::winsock_initialized_ = false;

void Socket::initialize_winsock() {
    if (!winsock_initialized_) {
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            throw NetworkException("Failed to initialize Winsock: " + std::to_string(result));
        }
        winsock_initialized_ = true;
    }
}

void Socket::cleanup_winsock() {
    if (winsock_initialized_) {
        WSACleanup();
        winsock_initialized_ = false;
    }
}
#else
void Socket::initialize_winsock() {}
void Socket::cleanup_winsock() {}
bool Socket::winsock_initialized_ = true;
#endif

Socket::Socket() : socket_(INVALID_SOCKET_VALUE) {
    initialize_winsock();
    socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == INVALID_SOCKET_VALUE) {
        throw NetworkException("Failed to create socket");
    }
}

Socket::Socket(socket_t sock) : socket_(sock) {
    initialize_winsock();
}

Socket::~Socket() {
    close();
}

Socket::Socket(Socket&& other) noexcept : socket_(other.socket_) {
    other.socket_ = INVALID_SOCKET_VALUE;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        socket_ = other.socket_;
        other.socket_ = INVALID_SOCKET_VALUE;
    }
    return *this;
}

void Socket::bind(const std::string& address, uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (address == "0.0.0.0" || address.empty()) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
#ifdef _WIN32
        // Use inet_addr on Windows (universally available)
        addr.sin_addr.s_addr = inet_addr(address.c_str());
        if (addr.sin_addr.s_addr == INADDR_NONE) {
            throw NetworkException("Invalid address: " + address);
        }
#else
        if (inet_pton(AF_INET, address.c_str(), &addr.sin_addr) <= 0) {
            throw NetworkException("Invalid address: " + address);
        }
#endif
    }
    
    if (::bind(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR_VALUE) {
#ifdef _WIN32
        int error = WSAGetLastError();
        if (error == WSAEADDRINUSE) {
            throw NetworkException("Address already in use: " + address + ":" + std::to_string(port) + 
                                 ". Another process is already listening on this port. Try stopping the other process or use a different port.");
        }
        throw NetworkException("Failed to bind socket to " + address + ":" + std::to_string(port) + 
                             " (Windows error: " + std::to_string(error) + ")");
#else
        if (errno == EADDRINUSE) {
            throw NetworkException("Address already in use: " + address + ":" + std::to_string(port) + 
                                 ". Another process is already listening on this port. Try stopping the other process or use a different port.");
        }
        throw NetworkException("Failed to bind socket to " + address + ":" + std::to_string(port) + 
                             " (errno: " + std::to_string(errno) + ")");
#endif
    }
}

void Socket::listen(int backlog) {
    if (::listen(socket_, backlog) == SOCKET_ERROR_VALUE) {
        throw NetworkException("Failed to listen on socket");
    }
}

Socket Socket::accept() {
    sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);
    
    socket_t client_socket = ::accept(socket_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
    if (client_socket == INVALID_SOCKET_VALUE) {
        throw NetworkException("Failed to accept connection");
    }
    
    return Socket(client_socket);
}

void Socket::connect(const std::string& address, uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
#ifdef _WIN32
    // Use inet_addr on Windows (universally available)
    addr.sin_addr.s_addr = inet_addr(address.c_str());
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        throw NetworkException("Invalid address: " + address);
    }
#else
    if (inet_pton(AF_INET, address.c_str(), &addr.sin_addr) <= 0) {
        throw NetworkException("Invalid address: " + address);
    }
#endif
    
    if (::connect(socket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR_VALUE) {
        throw NetworkException("Failed to connect to " + address + ":" + std::to_string(port));
    }
}

size_t Socket::send(const void* data, size_t length) {
    ssize_t bytes_sent = ::send(socket_, static_cast<const char*>(data), length, 0);
    if (bytes_sent == SOCKET_ERROR_VALUE) {
        throw NetworkException("Failed to send data");
    }
    return static_cast<size_t>(bytes_sent);
}

size_t Socket::receive(void* buffer, size_t length) {
    ssize_t bytes_received = ::recv(socket_, static_cast<char*>(buffer), length, 0);
    if (bytes_received == SOCKET_ERROR_VALUE) {
        throw NetworkException("Failed to receive data");
    }
    if (bytes_received == 0) {
        throw NetworkException("Connection closed by peer");
    }
    return static_cast<size_t>(bytes_received);
}

void Socket::set_reuse_address(bool enable) {
#ifdef _WIN32
    // On Windows, use SO_EXCLUSIVEADDRUSE to prevent multiple binds to same port
    // This ensures proper "address already in use" errors
    if (enable) {
        int option = 1;
        if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, 
                       reinterpret_cast<const char*>(&option), sizeof(option)) == SOCKET_ERROR_VALUE) {
            throw NetworkException("Failed to set SO_REUSEADDR");
        }
    } else {
        int option = 1;
        if (setsockopt(socket_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, 
                       reinterpret_cast<const char*>(&option), sizeof(option)) == SOCKET_ERROR_VALUE) {
            throw NetworkException("Failed to set SO_EXCLUSIVEADDRUSE");
        }
    }
#else
    // Unix behavior - SO_REUSEADDR works as expected
    int option = enable ? 1 : 0;
    if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, 
                   reinterpret_cast<const char*>(&option), sizeof(option)) == SOCKET_ERROR_VALUE) {
        throw NetworkException("Failed to set SO_REUSEADDR");
    }
#endif
}

void Socket::set_non_blocking(bool enable) {
#ifdef _WIN32
    u_long mode = enable ? 1 : 0;
    if (ioctlsocket(socket_, FIONBIO, &mode) == SOCKET_ERROR_VALUE) {
        throw NetworkException("Failed to set non-blocking mode");
    }
#else
    int flags = fcntl(socket_, F_GETFL, 0);
    if (flags == -1) {
        throw NetworkException("Failed to get socket flags");
    }
    
    if (enable) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    
    if (fcntl(socket_, F_SETFL, flags) == -1) {
        throw NetworkException("Failed to set non-blocking mode");
    }
#endif
}

void Socket::set_timeout(int seconds) {
#ifdef _WIN32
    DWORD timeout = seconds * 1000;
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, 
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == SOCKET_ERROR_VALUE ||
        setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, 
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout)) == SOCKET_ERROR_VALUE) {
        throw NetworkException("Failed to set socket timeout");
    }
#else
    struct timeval timeout_val;
    timeout_val.tv_sec = seconds;
    timeout_val.tv_usec = 0;
    
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, &timeout_val, sizeof(timeout_val)) == -1 ||
        setsockopt(socket_, SOL_SOCKET, SO_SNDTIMEO, &timeout_val, sizeof(timeout_val)) == -1) {
        throw NetworkException("Failed to set socket timeout");
    }
#endif
}

bool Socket::is_valid() const {
    return socket_ != INVALID_SOCKET_VALUE;
}

void Socket::close() {
    if (is_valid()) {
#ifdef _WIN32
        closesocket(socket_);
#else
        ::close(socket_);
#endif
        socket_ = INVALID_SOCKET_VALUE;
    }
}

} // namespace network
} // namespace netcopy

