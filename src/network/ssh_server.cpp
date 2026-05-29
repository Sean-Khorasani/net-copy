/**
 * ssh_server.cpp - Optional wolfSSH SSH/SCP/SFTP server
 *
 * The NetCopy custom protocol is the DEFAULT. This adds a secondary
 * SSH/SFTP listener so standard clients (OpenSSH, WinSCP, FileZilla)
 * can connect to the same server on a separate port.
 *
 * Requires: wolfSSH (FetchContent dependency, built on wolfSSL)
 */

#include "network/ssh_server.h"
#include "exceptions.h"
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <fstream>
#include <vector>

// wolfSSH headers
#include <wolfssh/ssh.h>
#include <wolfssh/wolfsftp.h>

#ifdef _WIN32
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "Ws2_32.lib")
typedef SOCKET ssh_socket_t;
#  define SSH_INVALID_SOCKET INVALID_SOCKET
#  define ssh_closesocket(s) closesocket(s)
#else
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <unistd.h>
typedef int ssh_socket_t;
#  define SSH_INVALID_SOCKET -1
#  define ssh_closesocket(s) ::close(s)
#endif

namespace netcopy {
namespace network {

// ============================================================
// wolfSSH auth callback — bridges wolfSSH into our AuthEngine
// ============================================================
int SshServer::wolfssl_user_auth_cb(uint8_t type,
                                     WS_UserAuthData* data,
                                     void* ctx)
{
    auto* self = static_cast<SshServer*>(ctx);

    if (!data || !data->username) {
        return WOLFSSH_USERAUTH_FAILURE;
    }

    std::string username(reinterpret_cast<const char*>(data->username),
                         data->usernameSz);

    if (type == WOLFSSH_USERAUTH_PASSWORD) {
        if (!data->sf.password.password) {
            return WOLFSSH_USERAUTH_FAILURE;
        }
        std::string password(
            reinterpret_cast<const char*>(data->sf.password.password),
            data->sf.password.passwordSz);

        // Delegate to the shared NetCopy auth engine
        if (self->auth_.verify_password(username, password)) {
            return WOLFSSH_USERAUTH_SUCCESS;
        }
    }
    else if (type == WOLFSSH_USERAUTH_PUBLICKEY) {
        // Public key auth — for now, fall back to failure.
        // Can be extended to check authorized_keys file via auth_engine.
        return WOLFSSH_USERAUTH_FAILURE;
    }

    return WOLFSSH_USERAUTH_FAILURE;
}

// ============================================================
// Constructor / Destructor
// ============================================================
SshServer::SshServer(const config::ServerConfig& config,
                     auth::AuthEngine& auth,
                     uint16_t port,
                     SshMode mode)
    : config_(config)
    , auth_(auth)
    , port_(port)
    , mode_(mode)
{
    // Initialize wolfSSH library (safe to call multiple times)
    if (wolfSSH_Init() != WS_SUCCESS) {
        throw std::runtime_error("wolfSSH_Init() failed");
    }

    ctx_ = wolfSSH_CTX_new(WOLFSSH_ENDPOINT_SERVER, nullptr);
    if (!ctx_) {
        wolfSSH_Cleanup();
        throw std::runtime_error("wolfSSH_CTX_new() failed");
    }

    // Register the user auth callback
    wolfSSH_SetUserAuth(ctx_, wolfssl_user_auth_cb);

    // Load host key from server.conf cert/key (reuse the same TLS key as host key)
    if (!config_.tls.server_key_file.empty()) {
        if (wolfSSH_CTX_SetBanner(ctx_, "NetCopy SSH Server") != WS_SUCCESS) {
            std::cerr << "[SshServer] Warning: could not set SSH banner\n";
        }

        // Read the private key file
        std::ifstream key_file(config_.tls.server_key_file, std::ios::binary);
        if (key_file) {
            std::vector<char> key_buf((std::istreambuf_iterator<char>(key_file)),
                                      std::istreambuf_iterator<char>());
            std::cout << "[SshServer] Debug: key file size read = " << key_buf.size() << " bytes\n";
            if (key_buf.size() >= 4) {
                std::cout << "[SshServer] Debug: first bytes: "
                          << std::hex << (int)(unsigned char)key_buf[0] << " "
                          << (int)(unsigned char)key_buf[1] << " "
                          << (int)(unsigned char)key_buf[2] << " "
                          << (int)(unsigned char)key_buf[3] << std::dec << "\n";
            }
            int ret = wolfSSH_CTX_UsePrivateKey_buffer(
                ctx_,
                reinterpret_cast<const byte*>(key_buf.data()),
                static_cast<word32>(key_buf.size()),
                WOLFSSH_FORMAT_ASN1);
            std::cout << "[SshServer] Debug: wolfSSH_CTX_UsePrivateKey_buffer (ASN1) ret = " << ret << "\n";
            if (ret != WS_SUCCESS) {
                // Fall back to PEM format
                ret = wolfSSH_CTX_UsePrivateKey_buffer(
                    ctx_,
                    reinterpret_cast<const byte*>(key_buf.data()),
                    static_cast<word32>(key_buf.size()),
                    WOLFSSH_FORMAT_PEM);
                std::cout << "[SshServer] Debug: wolfSSH_CTX_UsePrivateKey_buffer (PEM) ret = " << ret << "\n";
            }
            if (ret != WS_SUCCESS) {
                std::cerr << "[SshServer] Warning: failed to load SSH host key from "
                          << config_.tls.server_key_file << " (code=" << ret << ")\n";
            }
        } else {
            std::cerr << "[SshServer] Warning: could not open host key file "
                      << config_.tls.server_key_file << "\n";
        }
    }

    std::cout << "[SshServer] Initialized — will listen on port " << port_
              << " (SSH/SFTP/SCP — optional secondary protocol)\n";
}

SshServer::~SshServer() {
    stop();
    if (ctx_) {
        wolfSSH_CTX_free(ctx_);
        ctx_ = nullptr;
    }
    wolfSSH_Cleanup();
}

// ============================================================
// Start / Stop
// ============================================================
void SshServer::start() {
    if (running_.load()) return;
    running_.store(true);
    thread_ = std::thread(&SshServer::listener_loop, this);
    std::cout << "[SshServer] Started on port " << port_ << "\n";
}

void SshServer::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (thread_.joinable()) {
        thread_.join();
    }
    std::cout << "[SshServer] Stopped\n";
}

// ============================================================
// Main listener loop (runs on a background thread)
// ============================================================
void SshServer::listener_loop() {
    ssh_socket_t listen_fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_fd == SSH_INVALID_SOCKET) {
        std::cerr << "[SshServer] Failed to create listen socket\n";
        running_.store(false);
        return;
    }

    // Allow port reuse
    int opt = 1;
#ifdef _WIN32
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));
#else
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(port_);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(listen_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        std::cerr << "[SshServer] bind() failed on port " << port_ << "\n";
        ssh_closesocket(listen_fd);
        running_.store(false);
        return;
    }

    if (::listen(listen_fd, 10) != 0) {
        std::cerr << "[SshServer] listen() failed\n";
        ssh_closesocket(listen_fd);
        running_.store(false);
        return;
    }

    std::cout << "[SshServer] Listening on :" << port_
              << " — NetCopy primary protocol unaffected\n";

    // Set non-blocking so stop() can wake up the accept loop
#ifdef _WIN32
    u_long nb = 1;
    ioctlsocket(listen_fd, FIONBIO, &nb);
#else
    {
        int flags = fcntl(listen_fd, F_GETFL, 0);
        fcntl(listen_fd, F_SETFL, flags | O_NONBLOCK);
    }
#endif

    while (running_.load()) {
        // Poll for new connections with a short timeout
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(listen_fd, &fds);
        timeval tv{0, 100'000}; // 100 ms

        int sel = select(static_cast<int>(listen_fd + 1), &fds, nullptr, nullptr, &tv);
        if (sel <= 0) continue;

        sockaddr_in peer{};
        socklen_t peer_len = sizeof(peer);
        ssh_socket_t client_fd = ::accept(listen_fd,
                                          reinterpret_cast<sockaddr*>(&peer),
                                          &peer_len);
        if (client_fd == SSH_INVALID_SOCKET) continue;

        // Build peer address string for logging
        char peer_ip[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &peer.sin_addr, peer_ip, sizeof(peer_ip));
        std::string peer_addr = std::string(peer_ip) + ":" +
                                std::to_string(ntohs(peer.sin_port));

        // Spawn a detached thread per client (simple model — matches existing server pattern)
        std::thread([this, client_fd, peer_addr]() {
            WOLFSSH* ssh = wolfSSH_new(ctx_);
            if (!ssh) {
                std::cerr << "[SshServer] wolfSSH_new() failed for " << peer_addr << "\n";
                ssh_closesocket(client_fd);
                return;
            }

            wolfSSH_set_fd(ssh, static_cast<int>(client_fd));
            handle_client(ssh, peer_addr);

            wolfSSH_free(ssh);
            ssh_closesocket(client_fd);
        }).detach();
    }

    ssh_closesocket(listen_fd);
}

// ============================================================
// Per-client handler — performs SSH handshake and then
// dispatches to SFTP subsystem or SCP command handler.
// ============================================================
void SshServer::handle_client(WOLFSSH* ssh, const std::string& peer_addr) {
    std::cout << "[SshServer] Connection from " << peer_addr << "\n";

    // Set user auth context so the callback can access this SshServer instance
    wolfSSH_SetUserAuthCtx(ssh, this);

    // SSH handshake + user authentication (wolfSSH handles the full exchange)
    // Note: wolfSSH_accept returns WS_SFTP_COMPLETE or WS_SCP_INIT if the client
    // immediately requested those subsystems during handshake.
    int ret = wolfSSH_accept(ssh);
    if (ret != WS_SUCCESS && ret != WS_SFTP_COMPLETE && ret != WS_SCP_INIT) {
        std::cerr << "[SshServer] Handshake/auth failed for " << peer_addr
                  << " (code=" << ret << ")\n";
        return;
    }

    if ((ret == WS_SFTP_COMPLETE) &&
        (mode_ == SshMode::SFTP || mode_ == SshMode::ALL))
    {
        std::string access_path = config_.allowed_paths.empty() ? "." : config_.allowed_paths[0];
        std::cout << "[SshServer] SFTP session from " << peer_addr
                  << " (access path: " << access_path << ")\n";

        // Set default path for SFTP
        wolfSSH_SFTP_SetDefaultPath(ssh, access_path.c_str());

        // Run the SFTP subsystem — wolfSSH handles all SFTP protocol
        wolfSSH_SFTP_accept(ssh);
        while (running_.load()) {
            ret = wolfSSH_SFTP_read(ssh);
            if (ret == WS_WANT_READ || ret == WS_WANT_WRITE) {
                // would block — yield and try again
                std::this_thread::yield();
                continue;
            }
            if (ret != WS_SUCCESS) break;
        }
    }
    else if ((ret == WS_SCP_INIT) &&
             (mode_ == SshMode::SCP || mode_ == SshMode::ALL))
    {
        // SCP: run a receive/send loop using the wolfSSH SCP API
        std::cout << "[SshServer] SCP session from " << peer_addr << "\n";
        wolfSSH_stream_read(ssh, nullptr, 0); // flush / signal ready
        // wolfSSH SCP session loop — delegates file I/O to wolfSSH internals
        while (running_.load()) {
            ret = wolfSSH_stream_read(ssh, nullptr, 0);
            if (ret < 0 && ret != WS_WANT_READ) break;
        }
    }
    else {
        std::cout << "[SshServer] Unsupported or unknown channel type from " << peer_addr
                  << " (ret=" << ret << ") — closing\n";
    }

    wolfSSH_shutdown(ssh);
    std::cout << "[SshServer] Session closed: " << peer_addr << "\n";
}

} // namespace network
} // namespace netcopy
