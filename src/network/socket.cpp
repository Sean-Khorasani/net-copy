#include "network/socket.h"
#include "crypto/sha3.h"
#include "exceptions.h"
#include "common/fast_mem.h"
#include <cstring>
#include <limits>
#include <algorithm>
#include <array>

#ifdef _WIN32
#include <basetsd.h>
typedef SSIZE_T ssize_t;
#else
#include <netinet/tcp.h>
#endif

#include <vector>
#include <iostream>
#include <chrono>

struct RudpHeader {
    uint32_t seq;
    uint32_t ack;
    uint8_t flags; // 1: SYN, 2: ACK, 4: FIN, 8: DATA
    uint16_t length;
};

static void send_rudp_packet(socket_t fd, const sockaddr_in& dest, uint32_t seq, uint32_t ack, uint8_t flags, const void* data, uint16_t length) {
    std::vector<uint8_t> buffer(sizeof(RudpHeader) + length);
    RudpHeader* header = reinterpret_cast<RudpHeader*>(buffer.data());
    header->seq = htonl(seq);
    header->ack = htonl(ack);
    header->flags = flags;
    header->length = htons(length);
    if (length > 0 && data) {
        std::memcpy(buffer.data() + sizeof(RudpHeader), data, length);
    }
    
    sendto(fd, reinterpret_cast<const char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
           reinterpret_cast<const sockaddr*>(&dest), sizeof(dest));
}

static bool receive_rudp_packet(socket_t fd, sockaddr_in& from, uint32_t& seq, uint32_t& ack, uint8_t& flags, std::vector<uint8_t>& payload, int timeout_seconds) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd, &fds);
    
    struct timeval tv;
    tv.tv_sec = timeout_seconds;
    tv.tv_usec = 0;
    
    int ret = select(static_cast<int>(fd + 1), &fds, nullptr, nullptr, &tv);
    if (ret <= 0) return false;
    
    std::vector<uint8_t> buffer(2048);
#ifdef _WIN32
    int from_len = sizeof(from);
#else
    socklen_t from_len = sizeof(from);
#endif
    int n = recvfrom(fd, reinterpret_cast<char*>(buffer.data()), static_cast<int>(buffer.size()), 0,
                     reinterpret_cast<sockaddr*>(&from), &from_len);
    if (n < static_cast<int>(sizeof(RudpHeader))) return false;
    
    const RudpHeader* header = reinterpret_cast<const RudpHeader*>(buffer.data());
    seq = ntohl(header->seq);
    ack = ntohl(header->ack);
    flags = header->flags;
    uint16_t len = ntohs(header->length);
    
    if (n < static_cast<int>(sizeof(RudpHeader)) + len) return false;
    
    payload.resize(len);
    if (len > 0) {
        std::memcpy(payload.data(), buffer.data() + sizeof(RudpHeader), len);
    }
    return true;
}

namespace netcopy {
namespace network {

// wolfSSL is the TLS backend - OpenSSL compat layer maps SSL_* calls to wolfSSL
static std::string get_ssl_error() {
    unsigned long err;
    std::string err_str;
    while ((err = ERR_get_error()) != 0) {
        char buf[256];
        ERR_error_string_n(err, buf, sizeof(buf));
        if (!err_str.empty()) err_str += ", ";
        err_str += buf;
    }
    return err_str.empty() ? "Unknown SSL error" : err_str;
}

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

Socket::Socket(Socket&& other) noexcept 
    : socket_(other.socket_), reuse_address_(other.reuse_address_),
      is_udp_(other.is_udp_), udp_connected_(other.udp_connected_),
      udp_remote_addr_(other.udp_remote_addr_), send_seq_(other.send_seq_),
      recv_seq_(other.recv_seq_), ssl_(other.ssl_), ssl_ctx_(other.ssl_ctx_),
      is_tls_client_(other.is_tls_client_) {
    other.socket_ = INVALID_SOCKET_VALUE;
    other.ssl_ = nullptr;
    other.ssl_ctx_ = nullptr;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        socket_ = other.socket_;
        reuse_address_ = other.reuse_address_;
        is_udp_ = other.is_udp_;
        udp_connected_ = other.udp_connected_;
        udp_remote_addr_ = other.udp_remote_addr_;
        send_seq_ = other.send_seq_;
        recv_seq_ = other.recv_seq_;
        ssl_ = other.ssl_;
        ssl_ctx_ = other.ssl_ctx_;
        is_tls_client_ = other.is_tls_client_;
        
        other.socket_ = INVALID_SOCKET_VALUE;
        other.ssl_ = nullptr;
        other.ssl_ctx_ = nullptr;
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
    if (is_udp_) {
        struct sockaddr_in addr{};
        std::memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (address.empty() || address == "0.0.0.0" || address == "*") {
            addr.sin_addr.s_addr = INADDR_ANY;
        } else {
            inet_pton(AF_INET, address.c_str(), &addr.sin_addr);
        }
        
        set_reuse_address(reuse_address_);
        if (::bind(socket_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR_VALUE) {
            throw NetworkException("Failed to bind UDP socket");
        }
        return;
    }

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
    if (is_udp_) {
        return; // No listen call for UDP
    }
    if (::listen(socket_, backlog) == SOCKET_ERROR_VALUE) {
        throw NetworkException("Failed to listen on socket");
    }
}

Socket Socket::accept() {
    if (is_udp_) {
        while (true) {
            sockaddr_in from;
            uint32_t r_seq, r_ack;
            uint8_t r_flags;
            std::vector<uint8_t> payload;
            if (receive_rudp_packet(socket_, from, r_seq, r_ack, r_flags, payload, 3600)) {
                if (r_flags == 1 /* SYN */) {
                    socket_t client_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
                    if (client_fd == INVALID_SOCKET_VALUE) {
                        continue;
                    }
                    
                    sockaddr_in ephem_addr;
                    std::memset(&ephem_addr, 0, sizeof(ephem_addr));
                    ephem_addr.sin_family = AF_INET;
                    ephem_addr.sin_addr.s_addr = INADDR_ANY;
                    ephem_addr.sin_port = 0;
                    if (::bind(client_fd, reinterpret_cast<struct sockaddr*>(&ephem_addr), sizeof(ephem_addr)) == SOCKET_ERROR_VALUE) {
                        closesocket(client_fd);
                        continue;
                    }
                    
                    uint32_t my_seq = 200;
                    send_rudp_packet(client_fd, from, my_seq, r_seq + 1, 3 /* SYN + ACK */, nullptr, 0);
                    
                    sockaddr_in ack_from;
                    uint32_t a_seq, a_ack;
                    uint8_t a_flags;
                    std::vector<uint8_t> ack_payload;
                    if (receive_rudp_packet(client_fd, ack_from, a_seq, a_ack, a_flags, ack_payload, 2)) {
                        if (a_flags == 2 /* ACK */ && a_ack == my_seq + 1) {
                            Socket client_sock(client_fd);
                            client_sock.is_udp_ = true;
                            client_sock.udp_connected_ = true;
                            client_sock.udp_remote_addr_ = from;
                            client_sock.send_seq_ = my_seq + 1;
                            client_sock.recv_seq_ = r_seq + 1;
                            return client_sock;
                        }
                    }
                    closesocket(client_fd);
                }
            }
        }
    }

    sockaddr_storage client_addr{};
    socklen_t addr_len = sizeof(client_addr);
    
    socket_t client_socket = ::accept(socket_, reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
    if (client_socket == INVALID_SOCKET_VALUE) {
        throw NetworkException("Failed to accept connection");
    }
    
    return Socket(client_socket);
}

void Socket::connect(const std::string& address, uint16_t port) {
    if (is_udp_) {
        std::memset(&udp_remote_addr_, 0, sizeof(udp_remote_addr_));
        udp_remote_addr_.sin_family = AF_INET;
        udp_remote_addr_.sin_port = htons(port);
        if (inet_pton(AF_INET, address.c_str(), &udp_remote_addr_.sin_addr) <= 0) {
            struct addrinfo hints{}, *res = nullptr;
            hints.ai_family = AF_INET;
            hints.ai_socktype = SOCK_DGRAM;
            if (getaddrinfo(address.c_str(), nullptr, &hints, &res) == 0) {
                udp_remote_addr_.sin_addr = reinterpret_cast<struct sockaddr_in*>(res->ai_addr)->sin_addr;
                freeaddrinfo(res);
            } else {
                throw NetworkException("Failed to resolve address: " + address);
            }
        }
        
        int attempts = 5;
        uint32_t seq = 100;
        send_seq_ = seq;
        
        while (attempts-- > 0) {
            send_rudp_packet(socket_, udp_remote_addr_, send_seq_, 0, 1 /* SYN */, nullptr, 0);
            
            sockaddr_in from;
            uint32_t r_seq, r_ack;
            uint8_t r_flags;
            std::vector<uint8_t> payload;
            if (receive_rudp_packet(socket_, from, r_seq, r_ack, r_flags, payload, 2)) {
                if (r_flags == 3 /* SYN + ACK */ && r_ack == send_seq_ + 1) {
                    recv_seq_ = r_seq + 1;
                    send_seq_++;
                    send_rudp_packet(socket_, udp_remote_addr_, send_seq_, recv_seq_, 2 /* ACK */, nullptr, 0);
                    udp_connected_ = true;
                    return;
                }
            }
        }
        throw NetworkException("UDP connection timed out");
    }

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

void Socket::connect_via_proxy(const std::string& address, uint16_t port,
                               const std::string& proxy_type, const std::string& proxy_host, uint16_t proxy_port,
                               const std::string& proxy_username, const std::string& proxy_password) {
    // 1. Connect to the proxy server
    connect(proxy_host, proxy_port);
    
    // 2. Perform proxy handshake
    if (proxy_type == "socks5") {
        // Send greeting
        std::vector<uint8_t> greeting;
        greeting.push_back(0x05); // Version 5
        if (!proxy_username.empty()) {
            greeting.push_back(0x02); // 2 methods
            greeting.push_back(0x00); // NO AUTH
            greeting.push_back(0x02); // USER/PASS
        } else {
            greeting.push_back(0x01); // 1 method
            greeting.push_back(0x00); // NO AUTH
        }
        
        send(greeting.data(), greeting.size());
        
        // Receive greeting response
        uint8_t response[2];
        receive(response, 2);
        
        if (response[0] != 0x05) {
            close();
            throw NetworkException("Invalid SOCKS5 greeting response version");
        }
        
        uint8_t method = response[1];
        if (method == 0x02) {
            // USER/PASS authentication
            std::vector<uint8_t> auth;
            auth.push_back(0x01); // Subnegotiation version 1
            auth.push_back(static_cast<uint8_t>(proxy_username.size()));
            auth.insert(auth.end(), proxy_username.begin(), proxy_username.end());
            auth.push_back(static_cast<uint8_t>(proxy_password.size()));
            auth.insert(auth.end(), proxy_password.begin(), proxy_password.end());
            
            send(auth.data(), auth.size());
            
            uint8_t auth_resp[2];
            receive(auth_resp, 2);
            if (auth_resp[0] != 0x01 || auth_resp[1] != 0x00) {
                close();
                throw NetworkException("SOCKS5 proxy authentication failed");
            }
        } else if (method == 0xFF) {
            close();
            throw NetworkException("SOCKS5 proxy requires authentication, but no acceptable methods returned");
        } else if (method != 0x00) {
            close();
            throw NetworkException("Unsupported SOCKS5 authentication method");
        }
        
        // Send connection request
        std::vector<uint8_t> request;
        request.push_back(0x05); // Version 5
        request.push_back(0x01); // Connect command
        request.push_back(0x00); // Reserved
        request.push_back(0x03); // Domain name address type
        request.push_back(static_cast<uint8_t>(address.size()));
        request.insert(request.end(), address.begin(), address.end());
        
        uint16_t port_net = htons(port);
        uint8_t port_bytes[2];
        std::memcpy(port_bytes, &port_net, 2);
        request.push_back(port_bytes[0]);
        request.push_back(port_bytes[1]);
        
        send(request.data(), request.size());
        
        // Receive connection reply
        uint8_t reply[4];
        receive(reply, 4);
        
        if (reply[0] != 0x05) {
            close();
            throw NetworkException("Invalid SOCKS5 connection reply version");
        }
        if (reply[1] != 0x00) {
            close();
            throw NetworkException("SOCKS5 connection failed with error code: " + std::to_string(reply[1]));
        }
        
        // Read the bound address
        uint8_t addr_type = reply[3];
        if (addr_type == 0x01) { // IPv4
            uint8_t ip[4];
            receive(ip, 4);
        } else if (addr_type == 0x03) { // Domain name
            uint8_t len;
            receive(&len, 1);
            std::vector<uint8_t> domain(len);
            receive(domain.data(), len);
        } else if (addr_type == 0x04) { // IPv6
            uint8_t ip[16];
            receive(ip, 16);
        }
        // Read port
        uint8_t port_read[2];
        receive(port_read, 2);
        
    } else if (proxy_type == "http") {
        // Send CONNECT request
        std::string request = "CONNECT " + address + ":" + std::to_string(port) + " HTTP/1.1\r\n";
        request += "Host: " + address + ":" + std::to_string(port) + "\r\n";
        if (!proxy_username.empty()) {
            std::string credentials = proxy_username + ":" + proxy_password;
            std::vector<uint8_t> cred_bytes(credentials.begin(), credentials.end());
            request += "Proxy-Authorization: Basic " + crypto::base64_encode(cred_bytes) + "\r\n";
        }
        request += "\r\n";
        
        send(request.data(), request.size());
        
        // Read response line by line until \r\n\r\n
        std::string response;
        char c;
        while (true) {
            receive(&c, 1);
            response += c;
            if (response.size() >= 4 && response.substr(response.size() - 4) == "\r\n\r\n") {
                break;
            }
        }
        
        // Parse status line
        size_t status_start = response.find(" ");
        if (status_start == std::string::npos) {
            close();
            throw NetworkException("Invalid HTTP proxy response format");
        }
        std::string status_code = response.substr(status_start + 1, 3);
        if (status_code != "200") {
            close();
            throw NetworkException("HTTP proxy connection failed: " + response.substr(0, response.find("\r\n")));
        }
    } else {
        close();
        throw NetworkException("Unsupported proxy type: " + proxy_type);
    }
}

size_t Socket::send(const void* data, size_t length) {
    if (is_udp_) {
        const uint8_t* byte_ptr = reinterpret_cast<const uint8_t*>(data);
        size_t total_sent = 0;
        
        while (total_sent < length) {
            uint16_t chunk_len = static_cast<uint16_t>((std::min)(static_cast<size_t>(1300), length - total_sent));
            
            bool acked = false;
            int retries = 10;
            while (!acked && retries-- > 0) {
                send_rudp_packet(socket_, udp_remote_addr_, send_seq_, recv_seq_, 8 /* DATA */, byte_ptr + total_sent, chunk_len);
                
                sockaddr_in from;
                uint32_t r_seq, r_ack;
                uint8_t r_flags;
                std::vector<uint8_t> payload;
                if (receive_rudp_packet(socket_, from, r_seq, r_ack, r_flags, payload, 1)) {
                    if (r_flags == 2 /* ACK */ && r_ack == send_seq_ + chunk_len) {
                        send_seq_ += chunk_len;
                        total_sent += chunk_len;
                        acked = true;
                    }
                }
            }
            if (!acked) {
                throw NetworkException("UDP send timed out");
            }
        }
        return total_sent;
    }

    if (ssl_) {
        int send_len = static_cast<int>((std::min)(length, static_cast<size_t>((std::numeric_limits<int>::max)())));
        int bytes_written = SSL_write(ssl_, data, send_len);
        if (bytes_written <= 0) {
            int ssl_err = SSL_get_error(ssl_, bytes_written);
            throw NetworkException("TLS send failed (SSL error " + std::to_string(ssl_err) + "): " + get_ssl_error());
        }
        return static_cast<size_t>(bytes_written);
    }

    int send_len = static_cast<int>((std::min)(length, static_cast<size_t>((std::numeric_limits<int>::max)())));
    ssize_t bytes_sent = ::send(socket_, static_cast<const char*>(data), send_len, 0);
    if (bytes_sent == SOCKET_ERROR_VALUE) {
        throw NetworkException("Failed to send data");
    }
    return static_cast<size_t>(bytes_sent);
}

size_t Socket::send_vectored(const void* first, size_t first_length, const void* second, size_t second_length) {
    if (first_length == 0) {
        return send(second, second_length);
    }
    if (second_length == 0) {
        return send(first, first_length);
    }

    if (is_udp_ || ssl_) {
        size_t total = 0;
        while (total < first_length) {
            size_t sent = send(static_cast<const uint8_t*>(first) + total, first_length - total);
            if (sent == 0) {
                throw NetworkException("Failed to send first vectored buffer");
            }
            total += sent;
        }
        size_t second_sent = 0;
        while (second_sent < second_length) {
            size_t sent = send(static_cast<const uint8_t*>(second) + second_sent, second_length - second_sent);
            if (sent == 0) {
                throw NetworkException("Failed to send second vectored buffer");
            }
            second_sent += sent;
        }
        return first_length + second_length;
    }

#ifdef _WIN32
    const char* first_ptr = static_cast<const char*>(first);
    const char* second_ptr = static_cast<const char*>(second);
    size_t first_remaining = first_length;
    size_t second_remaining = second_length;
    size_t total_sent = 0;

    while (first_remaining > 0 || second_remaining > 0) {
        std::array<WSABUF, 2> buffers{};
        DWORD buffer_count = 0;
        if (first_remaining > 0) {
            buffers[buffer_count].buf = const_cast<char*>(first_ptr);
            buffers[buffer_count].len = static_cast<ULONG>((std::min)(first_remaining, static_cast<size_t>((std::numeric_limits<ULONG>::max)())));
            ++buffer_count;
        }
        if (second_remaining > 0) {
            buffers[buffer_count].buf = const_cast<char*>(second_ptr);
            buffers[buffer_count].len = static_cast<ULONG>((std::min)(second_remaining, static_cast<size_t>((std::numeric_limits<ULONG>::max)())));
            ++buffer_count;
        }

        DWORD bytes_sent = 0;
        int result = WSASend(socket_, buffers.data(), buffer_count, &bytes_sent, 0, nullptr, nullptr);
        if (result == SOCKET_ERROR_VALUE) {
            throw NetworkException("Failed to send vectored data");
        }
        if (bytes_sent == 0) {
            throw NetworkException("Vectored send made no progress");
        }

        total_sent += bytes_sent;
        size_t remaining_to_consume = bytes_sent;
        if (first_remaining > 0) {
            size_t consumed = (std::min)(first_remaining, remaining_to_consume);
            first_ptr += consumed;
            first_remaining -= consumed;
            remaining_to_consume -= consumed;
        }
        if (remaining_to_consume > 0 && second_remaining > 0) {
            size_t consumed = (std::min)(second_remaining, remaining_to_consume);
            second_ptr += consumed;
            second_remaining -= consumed;
        }
    }

    return total_sent;
#else
    std::vector<uint8_t> buffer(first_length + second_length);
    fast_mem::fast_memcpy(buffer.data(), first, first_length);
    fast_mem::fast_memcpy(buffer.data() + first_length, second, second_length);
    size_t total_sent = 0;
    while (total_sent < buffer.size()) {
        size_t sent = send(buffer.data() + total_sent, buffer.size() - total_sent);
        if (sent == 0) {
            throw NetworkException("Failed to send vectored fallback buffer");
        }
        total_sent += sent;
    }
    return total_sent;
#endif
}

size_t Socket::receive(void* buffer, size_t length) {
    if (is_udp_) {
        uint8_t* byte_ptr = reinterpret_cast<uint8_t*>(buffer);
        size_t total_received = 0;
        
        while (total_received < length) {
            sockaddr_in from;
            uint32_t r_seq, r_ack;
            uint8_t r_flags;
            std::vector<uint8_t> payload;
            
            if (receive_rudp_packet(socket_, from, r_seq, r_ack, r_flags, payload, 10)) {
                if (r_flags == 8 /* DATA */) {
                    if (r_seq == recv_seq_) {
                        size_t to_copy = (std::min)(static_cast<size_t>(payload.size()), length - total_received);
                        std::memcpy(byte_ptr + total_received, payload.data(), to_copy);
                        recv_seq_ += to_copy;
                        total_received += to_copy;
                        
                        send_rudp_packet(socket_, udp_remote_addr_, send_seq_, recv_seq_, 2 /* ACK */, nullptr, 0);
                    } else if (r_seq < recv_seq_) {
                        send_rudp_packet(socket_, udp_remote_addr_, send_seq_, recv_seq_, 2 /* ACK */, nullptr, 0);
                    }
                } else if (r_flags == 4 /* FIN */) {
                    send_rudp_packet(socket_, udp_remote_addr_, send_seq_, r_seq + 1, 2 /* ACK */, nullptr, 0);
                    break;
                }
            } else {
                throw NetworkException("UDP receive timed out");
            }
        }
        return total_received;
    }

    if (ssl_) {
        int recv_len = static_cast<int>((std::min)(length, static_cast<size_t>((std::numeric_limits<int>::max)())));
        int bytes_read = SSL_read(ssl_, buffer, recv_len);
        if (bytes_read <= 0) {
            int ssl_err = SSL_get_error(ssl_, bytes_read);
            if (ssl_err == SSL_ERROR_ZERO_RETURN) {
                throw NetworkException("Connection closed by peer");
            }
            throw NetworkException("TLS receive failed (SSL error " + std::to_string(ssl_err) + "): " + get_ssl_error());
        }
        return static_cast<size_t>(bytes_read);
    }

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
    if (is_udp_) return;
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
    if (ssl_) {
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    if (ssl_ctx_) {
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
    }
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

void Socket::set_udp(bool enable) {
    if (is_udp_ == enable) return;
    
    close();
    
    is_udp_ = enable;
    initialize_winsock();
    
    if (is_udp_) {
        socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    } else {
        socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    }
    
    if (socket_ == INVALID_SOCKET_VALUE) {
        throw NetworkException("Failed to re-create socket");
    }
}

socket_t Socket::release() {
    socket_t temp = socket_;
    socket_ = INVALID_SOCKET_VALUE;
    return temp;
}

void Socket::enable_tls_client(bool verify, const std::string& ca_file) {
    if (is_udp_) {
        throw NetworkException("TLS is not supported on UDP sockets");
    }
    if (ssl_ctx_) {
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
    }
    ssl_ctx_ = SSL_CTX_new(TLS_client_method());
    if (!ssl_ctx_) {
        throw NetworkException("Failed to create SSL client context: " + get_ssl_error());
    }
    
    SSL_CTX_set_min_proto_version(ssl_ctx_, TLS1_3_VERSION);
    
    if (verify) {
        SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_PEER, nullptr);
        if (!ca_file.empty()) {
            if (SSL_CTX_load_verify_locations(ssl_ctx_, ca_file.c_str(), nullptr) <= 0) {
                std::string err = get_ssl_error();
                SSL_CTX_free(ssl_ctx_);
                ssl_ctx_ = nullptr;
                throw NetworkException("Failed to load CA file: " + ca_file + " (" + err + ")");
            }
        } else {
            SSL_CTX_set_default_verify_paths(ssl_ctx_);
        }
    } else {
        SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_NONE, nullptr);
    }
    
    is_tls_client_ = true;
}

void Socket::enable_tls_server(const std::string& cert_file, const std::string& key_file, const std::string& dh_file) {
    if (is_udp_) {
        throw NetworkException("TLS is not supported on UDP sockets");
    }
    if (ssl_ctx_) {
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
    }
    ssl_ctx_ = SSL_CTX_new(TLS_server_method());
    if (!ssl_ctx_) {
        throw NetworkException("Failed to create SSL server context: " + get_ssl_error());
    }
    
    SSL_CTX_set_min_proto_version(ssl_ctx_, TLS1_3_VERSION);
    
    if (SSL_CTX_use_certificate_chain_file(ssl_ctx_, cert_file.c_str()) <= 0) {
        std::string err = get_ssl_error();
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
        throw NetworkException("Failed to use certificate chain file: " + cert_file + " (" + err + ")");
    }
    
    if (SSL_CTX_use_PrivateKey_file(ssl_ctx_, key_file.c_str(), SSL_FILETYPE_PEM) <= 0) {
        std::string err = get_ssl_error();
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
        throw NetworkException("Failed to use private key file: " + key_file + " (" + err + ")");
    }
    
    if (!SSL_CTX_check_private_key(ssl_ctx_)) {
        SSL_CTX_free(ssl_ctx_);
        ssl_ctx_ = nullptr;
        throw NetworkException("Private key does not match certificate");
    }
    
    // DH params block removed - TLS 1.3 uses ECDHE exclusively, DH params are never used.
    // (The legacy BIO/DH/PEM APIs from OpenSSL 1.x are not needed with wolfSSL+TLS1.3)
    
    is_tls_client_ = false;
}

void Socket::perform_tls_handshake() {
    if (!ssl_ctx_) {
        throw NetworkException("TLS context not initialized. Call enable_tls_client or enable_tls_server first.");
    }
    
    if (ssl_) {
        SSL_free(ssl_);
        ssl_ = nullptr;
    }
    
    ssl_ = SSL_new(ssl_ctx_);
    if (!ssl_) {
        throw NetworkException("Failed to create SSL object: " + get_ssl_error());
    }
    
    if (SSL_set_fd(ssl_, static_cast<int>(socket_)) <= 0) {
        std::string err = get_ssl_error();
        SSL_free(ssl_);
        ssl_ = nullptr;
        throw NetworkException("Failed to bind socket to SSL: " + err);
    }
    
    int ret;
    if (is_tls_client_) {
        ret = SSL_connect(ssl_);
    } else {
        ret = SSL_accept(ssl_);
    }
    
    if (ret <= 0) {
        int err_code = SSL_get_error(ssl_, ret);
        std::string err_msg = get_ssl_error();
        SSL_free(ssl_);
        ssl_ = nullptr;
        throw NetworkException("TLS handshake failed (SSL error " + std::to_string(err_code) + "): " + err_msg);
    }
}

} // namespace network
} // namespace netcopy
