#include "network/socket.h"
#include "exceptions.h"
#include <cstring>
#include <limits>
#include <algorithm>

#ifdef _WIN32
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#else
#include <netinet/tcp.h>
#endif

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

static int get_socket_family(socket_t sock) {
    sockaddr_storage addr{};
    socklen_t len = sizeof(addr);
    if (getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
        return addr.ss_family;
    }
    return AF_INET;
}

void Socket::bind(const std::string& address, uint16_t port) {
    std::string addr_str = address;
    if (addr_str.empty() || addr_str == "0.0.0.0") {
        addr_str = "::"; // Default to dual-stack IPv6 any address
    }
    
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC; // Accept both IPv4 and IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE; // For binding
    
    struct addrinfo* result = nullptr;
    std::string port_str = std::to_string(port);
    int status = getaddrinfo(addr_str.c_str(), port_str.c_str(), &hints, &result);
    if (status != 0) {
        throw NetworkException("getaddrinfo failed: " + std::string(gai_strerror(status)));
    }
    
    struct addrinfo* rp = nullptr;
    bool bound = false;
    std::string last_error_msg;
    
    for (rp = result; rp != nullptr; rp = rp->ai_next) {
        if (socket_ == INVALID_SOCKET_VALUE || rp->ai_family != get_socket_family(socket_)) {
            close();
            socket_ = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (socket_ == INVALID_SOCKET_VALUE) {
                continue;
            }
            
            if (rp->ai_family == AF_INET6) {
                int opt = 0;
#ifdef _WIN32
                setsockopt(socket_, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
                setsockopt(socket_, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt));
#endif
            }
        }
        
        set_reuse_address(reuse_address_);
        
        if (::bind(socket_, rp->ai_addr, static_cast<int>(rp->ai_addrlen)) != SOCKET_ERROR_VALUE) {
            bound = true;
            break;
        }
        
#ifdef _WIN32
        last_error_msg = "Windows error " + std::to_string(WSAGetLastError());
#else
        last_error_msg = std::strerror(errno);
#endif
    }
    
    freeaddrinfo(result);
    
    if (!bound) {
        throw NetworkException("Failed to bind socket to " + address + ":" + std::to_string(port) + " (" + last_error_msg + ")");
    }
}

void Socket::listen(int backlog) {
    if (::listen(socket_, backlog) == SOCKET_ERROR_VALUE) {
        throw NetworkException("Failed to listen on socket");
    }
}

Socket Socket::accept() {
    sockaddr_storage client_addr{};
    socklen_t addr_len = sizeof(client_addr);
    
    socket_t client_socket = ::accept(socket_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
    if (client_socket == INVALID_SOCKET_VALUE) {
        throw NetworkException("Failed to accept connection");
    }
    
    return Socket(client_socket);
}

void Socket::connect(const std::string& address, uint16_t port) {
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    
    struct addrinfo* result = nullptr;
    std::string port_str = std::to_string(port);
    int status = getaddrinfo(address.c_str(), port_str.c_str(), &hints, &result);
    if (status != 0) {
        throw NetworkException("getaddrinfo failed for " + address + ": " + std::string(gai_strerror(status)));
    }
    
    struct addrinfo* rp = nullptr;
    bool connected = false;
    std::string last_error_msg;
    
    for (rp = result; rp != nullptr; rp = rp->ai_next) {
        if (socket_ == INVALID_SOCKET_VALUE || rp->ai_family != get_socket_family(socket_)) {
            close();
            socket_ = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (socket_ == INVALID_SOCKET_VALUE) {
                continue;
            }
        }
        
        if (::connect(socket_, rp->ai_addr, static_cast<int>(rp->ai_addrlen)) != SOCKET_ERROR_VALUE) {
            connected = true;
            break;
        }
        
#ifdef _WIN32
        last_error_msg = "Windows error " + std::to_string(WSAGetLastError());
#else
        last_error_msg = std::strerror(errno);
#endif
    }
    
    freeaddrinfo(result);
    
    if (!connected) {
        throw NetworkException("Failed to connect to " + address + ":" + std::to_string(port) + " (" + last_error_msg + ")");
    }
}

size_t Socket::send(const void* data, size_t length) {
    int send_len = static_cast<int>((std::min)(length, static_cast<size_t>((std::numeric_limits<int>::max)())));
    ssize_t bytes_sent = ::send(socket_, static_cast<const char*>(data), send_len, 0);
    if (bytes_sent == SOCKET_ERROR_VALUE) {
        throw NetworkException("Failed to send data");
    }
    return static_cast<size_t>(bytes_sent);
}

size_t Socket::receive(void* buffer, size_t length) {
    int recv_len = static_cast<int>((std::min)(length, static_cast<size_t>((std::numeric_limits<int>::max)())));
    ssize_t bytes_received = ::recv(socket_, static_cast<char*>(buffer), recv_len, 0);
    if (bytes_received == SOCKET_ERROR_VALUE) {
        throw NetworkException("Failed to receive data");
    }
    if (bytes_received == 0) {
        throw NetworkException("Connection closed by peer");
    }
    return static_cast<size_t>(bytes_received);
}

void Socket::set_reuse_address(bool enable) {
    reuse_address_ = enable;
    if (socket_ == INVALID_SOCKET_VALUE) {
        return;
    }
#ifdef _WIN32
    // On Windows, use SO_EXCLUSIVEADDRUSE to prevent multiple binds to same port
    // This ensures proper "address already in use" errors
    if (enable) {
        // Disable exclusive address use first to prevent mutual exclusivity failures
        int opt_val = 0;
        setsockopt(socket_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&opt_val), sizeof(opt_val));
        
        int option = 1;
        if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, 
                       reinterpret_cast<const char*>(&option), sizeof(option)) == SOCKET_ERROR_VALUE) {
            throw NetworkException("Failed to set SO_REUSEADDR");
        }
    } else {
        // Disable reuse address first
        int opt_val = 0;
        setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt_val), sizeof(opt_val));
        
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

void Socket::set_tcp_nodelay(bool enable) {
    int opt_val = enable ? 1 : 0;
    if (setsockopt(socket_, IPPROTO_TCP, TCP_NODELAY,
                   reinterpret_cast<const char*>(&opt_val), sizeof(opt_val)) == SOCKET_ERROR_VALUE) {
        throw NetworkException("Failed to set TCP_NODELAY");
    }
}

void Socket::set_buffer_sizes(size_t send_size, size_t recv_size) {
    int snd_val = static_cast<int>(send_size);
    int rcv_val = static_cast<int>(recv_size);
    if (setsockopt(socket_, SOL_SOCKET, SO_SNDBUF,
                   reinterpret_cast<const char*>(&snd_val), sizeof(snd_val)) == SOCKET_ERROR_VALUE) {
        throw NetworkException("Failed to set SO_SNDBUF");
    }
    if (setsockopt(socket_, SOL_SOCKET, SO_RCVBUF,
                   reinterpret_cast<const char*>(&rcv_val), sizeof(rcv_val)) == SOCKET_ERROR_VALUE) {
        throw NetworkException("Failed to set SO_RCVBUF");
    }
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

std::string Socket::get_peer_address() const {
    sockaddr_storage addr{};
    socklen_t addr_len = sizeof(addr);
    if (::getpeername(socket_, reinterpret_cast<sockaddr*>(&addr), &addr_len) == 0) {
        char host[NI_MAXHOST] = {};
        char serv[NI_MAXSERV] = {};
        int res = getnameinfo(reinterpret_cast<const sockaddr*>(&addr), addr_len,
                              host, sizeof(host),
                              serv, sizeof(serv),
                              NI_NUMERICHOST | NI_NUMERICSERV);
        if (res == 0) {
            std::string host_str(host);
            if (addr.ss_family == AF_INET6) {
                return "[" + host_str + "]:" + std::string(serv);
            }
            return host_str + ":" + std::string(serv);
        }
    }
    return "unknown";
}

} // namespace network
} // namespace netcopy
