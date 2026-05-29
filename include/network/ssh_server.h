#pragma once
/**
 * ssh_server.h - Optional SSH/SCP/SFTP server using wolfSSH
 *
 * The NetCopy custom protocol remains the DEFAULT and primary transfer mechanism.
 * This module adds an OPTIONAL secondary listener so standard SSH/SCP/SFTP
 * clients (OpenSSH, WinSCP, FileZilla, etc.) can also connect to the same server.
 *
 * Architecture:
 *   - net_copy_server listens on its port (default 1245)  <- primary protocol
 *   - SshServer listens on a separate port (default 2222)  <- optional SSH/SFTP
 *
 * Both servers share the same:
 *   - User/auth database (auth_engine)
 *   - Allowed file access paths (config)
 *   - FileManager for file operations
 */

#include <string>
#include <cstdint>
#include <atomic>
#include <thread>
#include <functional>
#include "auth/auth_engine.h"
#include "config/config_parser.h"

// Include wolfSSH headers
#include <wolfssh/ssh.h>

namespace netcopy {
namespace network {

/**
 * SSH/SFTP/SCP mode supported by this server instance.
 */
enum class SshMode {
    SFTP,    ///< SFTP subsystem (file get/put/ls/rm, standard sftp clients)
    SCP,     ///< SCP protocol (standard scp clients)
    ALL      ///< Both SFTP and SCP (default)
};

/**
 * SshServer - Optional wolfSSH-backed server providing SSH/SCP/SFTP access.
 *
 * Usage from net_copy_server:
 *   SshServer ssh_srv(config, auth);
 *   ssh_srv.start();   // non-blocking, spawns listener thread
 *   ...
 *   ssh_srv.stop();
 */
class SshServer {
public:
    /**
     * @param config  Shared server configuration (access path, bind address, etc.)
     * @param auth    Shared auth engine for password + public key verification
     * @param port    SSH listen port (default 2222, separate from NetCopy port)
     * @param mode    Which SSH sub-protocols to support
     */
    explicit SshServer(const config::ServerConfig& config,
                       auth::AuthEngine& auth,
                       uint16_t port = 2222,
                       SshMode mode  = SshMode::ALL);
    ~SshServer();

    SshServer(const SshServer&) = delete;
    SshServer& operator=(const SshServer&) = delete;

    /**
     * Start the SSH server on a background thread.
     * Returns immediately; does not block.
     */
    void start();

    /**
     * Stop the SSH server gracefully. Waits for the listener thread to exit.
     */
    void stop();

    bool is_running() const { return running_.load(); }
    uint16_t port()   const { return port_; }

private:
    void listener_loop();
    void handle_client(WOLFSSH* ssh, const std::string& peer_addr);

    // Auth callbacks passed to wolfSSH
    static int wolfssl_user_auth_cb(uint8_t type, WS_UserAuthData* data, void* ctx);

    const config::ServerConfig& config_;
    auth::AuthEngine& auth_;
    uint16_t port_;
    SshMode  mode_;

    WOLFSSH_CTX*       ctx_     = nullptr;
    std::atomic<bool>  running_ = {false};
    std::thread        thread_;
};

} // namespace network
} // namespace netcopy
