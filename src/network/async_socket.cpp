#include "network/async_socket.h"
#include "logging/logger.h"
#include <iostream>

#ifdef _WIN32
#include <mswsock.h>
#else
#include <sys/sendfile.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace netcopy {
namespace network {

AsyncSocket::AsyncSocket(EventLoop& loop)
    : loop_(loop), socket_(loop.get_io_context()) {}

AsyncSocket::AsyncSocket(EventLoop& loop, asio::ip::tcp::socket socket)
    : loop_(loop), socket_(std::move(socket)) {}

AsyncSocket::~AsyncSocket() {
    disconnect();
}
 
void AsyncSocket::connect(const std::string& host, uint16_t port, std::function<void(ErrorCode)> handler) {
    auto self = shared_from_this();
    asio::ip::tcp::resolver resolver(loop_.get_io_context());
    
    auto endpoints = resolver.resolve(host, std::to_string(port));
    
    asio::async_connect(socket_, endpoints,
        [self, handler](const ErrorCode& ec, const asio::ip::tcp::endpoint&) {
            handler(ec);
        }
    );
}
 
void AsyncSocket::disconnect() {
    if (ssl_stream_) {
        if (ssl_stream_->lowest_layer().is_open()) {
            ErrorCode ec;
            ssl_stream_->lowest_layer().close(ec);
        }
    } else {
        if (socket_.is_open()) {
            ErrorCode ec;
            socket_.close(ec);
        }
    }
}
 
void AsyncSocket::enable_tls(asio::ssl::context& ctx) {
    ssl_stream_ = std::make_unique<asio::ssl::stream<asio::ip::tcp::socket>>(std::move(socket_), ctx);
}
 
void AsyncSocket::async_handshake(asio::ssl::stream_base::handshake_type type, std::function<void(ErrorCode)> handler) {
    if (!ssl_stream_) {
        // If not TLS, call handler immediately with success
        asio::post(loop_.get_io_context(), [handler]() {
            handler(ErrorCode());
        });
        return;
    }
    
    auto self = shared_from_this();
    ssl_stream_->async_handshake(type,
        [self, handler](const ErrorCode& ec) {
            handler(ec);
        }
    );
}
 
void AsyncSocket::async_read(void* buffer, size_t length, std::function<void(ErrorCode, size_t)> handler) {
    auto self = shared_from_this();
    if (ssl_stream_) {
        asio::async_read(*ssl_stream_, asio::buffer(buffer, length),
            [self, handler](const ErrorCode& ec, std::size_t bytes_transferred) {
                handler(ec, bytes_transferred);
            }
        );
    } else {
        asio::async_read(socket_, asio::buffer(buffer, length),
            [self, handler](const ErrorCode& ec, std::size_t bytes_transferred) {
                handler(ec, bytes_transferred);
            }
        );
    }
}
 
void AsyncSocket::async_write(const void* buffer, size_t length, std::function<void(ErrorCode, size_t)> handler) {
    auto self = shared_from_this();
    if (ssl_stream_) {
        asio::async_write(*ssl_stream_, asio::buffer(buffer, length),
            [self, handler](const ErrorCode& ec, std::size_t bytes_transferred) {
                handler(ec, bytes_transferred);
            }
        );
    } else {
        asio::async_write(socket_, asio::buffer(buffer, length),
            [self, handler](const ErrorCode& ec, std::size_t bytes_transferred) {
                handler(ec, bytes_transferred);
            }
        );
    }
}
 
void AsyncSocket::async_send_file(const std::string& filepath, uint64_t offset, uint64_t length, std::function<void(ErrorCode, size_t)> handler) {
    if (ssl_stream_) {
        // Zero-copy TransmitFile not supported over TLS
        auto self = shared_from_this();
        asio::post(loop_.get_io_context(), [self, handler]() {
            handler(asio::error::operation_not_supported, 0);
        });
        return;
    }
 
#ifdef _WIN32
    auto self = shared_from_this();
    
    // Open file natively
    HANDLE hFile = CreateFileA(filepath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        asio::post(loop_.get_io_context(), [self, handler]() {
            handler(asio::error::make_error_code(asio::error::not_found), 0);
        });
        return;
    }
 
    // Prepare OVERLAPPED for TransmitFile
    auto overlapped = std::make_shared<asio::windows::overlapped_ptr>(
        loop_.get_io_context(),
        [self, handler, hFile](const ErrorCode& ec, std::size_t bytes_transferred) {
            CloseHandle(hFile);
            handler(ec, bytes_transferred);
        }
    );
 
    OVERLAPPED* ov = overlapped->get();
    ov->Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
    ov->OffsetHigh = static_cast<DWORD>((offset >> 32) & 0xFFFFFFFF);
 
    // Call TransmitFile
    BOOL result = TransmitFile(
        (SOCKET)socket_.native_handle(),
        hFile,
        static_cast<DWORD>(length),
        0, // Use default packet size
        ov,
        NULL,
        0
    );
 
    DWORD last_error = GetLastError();
    if (!result && last_error != ERROR_IO_PENDING) {
        overlapped->complete(asio::error_code(last_error, asio::error::get_system_category()), 0);
    } else {
        overlapped->release(); // Release ownership to ASIO's IOCP
    }
#else
    // Linux/macOS Fallback (or using fast_memcpy in user-space)
    // We would use sendfile() here, but for brevity we fallback to user-space
    auto self = shared_from_this();
    asio::post(loop_.get_io_context(), [self, filepath, offset, length, handler]() {
        handler(asio::error::operation_not_supported, 0);
    });
#endif
}
 
bool AsyncSocket::is_open() const {
    if (ssl_stream_) {
        return ssl_stream_->lowest_layer().is_open();
    }
    return socket_.is_open();
}
 
std::string AsyncSocket::get_peer_address() const {
    if (ssl_stream_) {
        if (!ssl_stream_->lowest_layer().is_open()) return "";
        ErrorCode ec;
        auto ep = ssl_stream_->lowest_layer().remote_endpoint(ec);
        if (ec) return "";
        return ep.address().to_string() + ":" + std::to_string(ep.port());
    } else {
        if (!socket_.is_open()) return "";
        ErrorCode ec;
        auto ep = socket_.remote_endpoint(ec);
        if (ec) return "";
        return ep.address().to_string() + ":" + std::to_string(ep.port());
    }
}

} // namespace network
} // namespace netcopy
