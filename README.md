# NetCopy

**NetCopy** is a high-performance, encrypted, resumable file transfer tool — similar in spirit to `scp` or `rsync`, but built around a persistent server daemon, post-quantum-ready cryptography, and adaptive flow control.

---

## Table of Contents

- [Quick Start](#quick-start)
- [Building](#building)
- [Architecture Overview](#architecture-overview)
- [Server](#server)
  - [Running the Server](#running-the-server)
  - [server.conf Reference](#serverconf-reference)
  - [Windows Service](#windows-service)
- [Client](#client)
  - [Running the Client](#running-the-client)
  - [Destination Format](#destination-format)
  - [client.conf Reference](#clientconf-reference)
- [GUI Client](#gui-client)
- [User Management \& Authentication](#user-management--authentication)
  - [How Authentication Works](#how-authentication-works)
  - [net_copy_admin Tool](#net_copy_admin-tool)
  - [Password Authentication](#password-authentication)
  - [ML-KEM Key Authentication](#ml-kem-key-authentication)
- [Encryption Modes](#encryption-modes)
  - [Security Level Negotiation](#security-level-negotiation)
- [Examples \& Use Cases](#examples--use-cases)
- [users.csv Format](#userscsv-format)

---

## Quick Start

> These instructions demonstrate transferring files on a single Windows machine (server and client both on `127.0.0.1`).

**Step 1 — Start the server** (in one terminal):
```powershell
cd build_vs\Release
.\net_copy_server.exe -l 127.0.0.1:1245 -a "D:\Work\files"
```

**Step 2 — Transfer a file** (in another terminal):
```powershell
cd build_vs\Release
.\net_copy_client.exe C:\Users\Me\Documents\report.pdf 127.0.0.1:D:\Work\files\
```

**Step 3 — Transfer a folder recursively**:
```powershell
.\net_copy_client.exe -R C:\Users\Me\Projects\ 127.0.0.1:D:\Work\files\Projects\
```

**Step 4 — Resume an interrupted transfer**:
```powershell
.\net_copy_client.exe --resume C:\Users\Me\bigfile.zip 127.0.0.1:D:\Work\files\
```

**Step 5 — Start the GUI client**:
```powershell
.\net_copy_gui.exe
```
This launches a local web-view server and automatically opens the interface in your default web browser (defaulting to `http://localhost:1246/`).

---

## Building

### Windows (Recommended — Visual Studio Build Tools)
```powershell
.\build_vs_cli.bat
```
Outputs all compiled executables (`net_copy_server.exe`, `net_copy_client.exe`, `net_copy_admin.exe`, `net_copy_gui.exe`, and `net_copy_service.exe`) to `build_vs\Release\`.

### Windows (NVIDIA GPU + Visual Studio)
```powershell
.\build_auto_detect.bat
```

### Windows (MSYS2 / MinGW)
```powershell
rmdir /s /q build
mkdir build
cd build
cmake -G "MinGW Makefiles" ..
cmake --build . --config Release
```

### Linux
```bash
./build.sh
```
Outputs binaries to `build/`.

### Dependencies (managed automatically via vcpkg / FetchContent)
| Package | Purpose |
|---------|---------|
| `lz4` | Fast compression/decompression for transfer data |
| `liboqs` | CRYSTALS-Kyber / ML-KEM post-quantum key encapsulation |
| `wolfssl` | Robust TLS and cryptographic base primitives |
| `wolfssh` | Secure Shell (SSH) and SFTP support |

---

## Architecture Overview

```
┌─────────────────────┐       TCP/IP        ┌─────────────────────┐
│   net_copy_client   │ ──────────────────► │   net_copy_server   │
│         or          │                     │                     │
│    net_copy_gui     │   encrypted chunks  │  writes remote file │
│  reads local file   │ ◄────────────────── │  sends ACKs         │
│  flow-controlled    │    (AIMD algorithm) │  per-user ACL       │
└─────────────────────┘                     └─────────────────────┘
          │                                           │
    client.conf                                 server.conf
    (auth, crypto,                              (paths, users,
     bandwidth)                                  crypto, logging)
```

**Handshake and Communication Protocol Sequence:**
1. **Client → Server**: Handshake request containing client version, requested security level, username, chosen auth method, client-side nonce, and max chunk size.
2. **Server → Client**: Handshake response containing server version, server-side nonce, authentication requirement flag, accepted security level, negotiated max chunk size, parallel stream count, and path creation settings.
3. **Session Key Derivation**: Both sides derive a dynamic session key by combining the pre-shared key or user credentials with the handshaked client and server nonces.
4. **Client → Server (Authentication Phase)**: Sends auth proof (PBKDF2-SHA3-256 challenge response or ML-KEM decapsulation verification token).
5. **Server → Client**: Verification result. If successful, the connection proceeds; otherwise, the client is disconnected.
6. **Data Transfer**: Client streams encrypted `FILE_DATA` blocks; server processes, decrypts, writes to disk, and sends a `FILE_ACK` per chunk.
7. **Flow Control & Windowing**: Client regulates throughput using an Additive Increase Multiplicative Decrease (AIMD) flow control window, capping unacknowledged data in flight to at most 64MB to maximize pipelining efficiency while avoiding socket bottlenecks.

---

## Server

### Running the Server

```
net_copy_server [options]

Options:
  -l, --listen ADDRESS:PORT   Listen address and port  (default: 0.0.0.0:1245)
  -a, --access PATH           Directory allowed for file writes (can be specified multiple times)
  -c, --config FILE           Path to configuration file (default: server.conf)
  -d, --daemon                Run as a background daemon process (Linux/Unix)
  --auto-create               Create missing destination directories automatically (default)
  --no-auto-create            Reject writes to non-existent target directories
  -v, --verbose [LEVEL]       Force enable console output and set its logging level (defaults to DEBUG if LEVEL is omitted). Overrides the configuration file's console settings, but keeps file logging settings unchanged.
  -h, --help                  Show help message
```

#### Basic Command Line Examples

```powershell
# Listen on all local interfaces, port 1245, allowing writes inside D:\Shared
.\net_copy_server.exe -l 0.0.0.0:1245 -a "D:\Shared"

# Listen on localhost with a custom config file
.\net_copy_server.exe -l 127.0.0.1:1245 -c "C:\config\server.conf"

# Run with verbose logging to show granular handshake and ACK traces
.\net_copy_server.exe -v
```

> **Note**: CLI arguments override parameters defined in the configuration file. If `-c` is omitted, the server attempts to load `server.conf` from the executable's directory.

---

### server.conf Reference

The server configuration file (`server.conf`) controls network listeners, protocol constraints, logging, directories, and credential validations.

#### `[network]`
* **`listen_address`** (Default: `"127.0.0.1"`)
  * **Meaning**: Local interface IP address to bind. Use `0.0.0.0` for all IPv4 interfaces, `127.0.0.1` for local connections only, or `::` / `::1` for IPv6/dual-stack.
* **`listen_port`** (Default: `1245`)
  * **Meaning**: TCP port on which the server listens.
* **`max_connections`** (Default: `10`)
  * **Meaning**: Maximum concurrent active client connections.
* **`timeout`** (Default: `30`)
  * **Meaning**: Network inactivity timeout in seconds. Connections showing no activity for this duration are closed.
* **`udp`** (Default: `false`)
  * **Meaning**: Enables the custom Reliable-UDP (R-UDP) transport protocol instead of standard TCP.
* **`socket_buffer_size`** (Default: `0`)
  * **Meaning**: Sets custom TCP socket send and receive buffer sizes in bytes (`0` to use operating system defaults).

#### `[protocol]`
* **`default_protocol`** (Default: `"internal"`)
  * **Meaning**: Defines the default server protocol to accept on `listen_port`. TLS is enabled separately and wraps the internal protocol.
  * **Options**: `"internal"`, `"ssh"`, `"sftp"`.

#### `[protocol.internal]`
* **`enable`** (Default: `true`)
  * **Meaning**: Enables the custom NetCopy high-performance block protocol handler.
* **`secret_key`** (Default: `""`)
  * **Meaning**: Hex-encoded master key used for transport-layer security and dynamic key derivation. Must match the client's key.
* **`require_auth`** (Default: `true`)
  * **Meaning**: Enforce network-channel session key validation using the `secret_key`.
* **`auth_method`** (Default: `"password"`)
  * **Meaning**: The user authentication protocol to verify connecting users.
  * **Options**: `"password"`, `"mlkem"`.
* **`security_level`** (Default: `"auto"`)
  * **Meaning**: Enforced server-side encryption algorithm constraint.
  * **Options**: `"auto"` (uses client request), `"FAST"`, `"HIGH"`, `"AES"`, `"AES-GCM"`.
* **`users_file`** (Default: `"users.csv"`)
  * **Meaning**: Path to the user database CSV file containing credentials and access control lists.
* **`allow_anonymous`** (Default: `true`)
  * **Meaning**: Allow connections that do not specify a user credential, bypassing database validation (though still requiring the master `secret_key` if `require_auth` is enabled).
* **`max_chunk_size`** (Default: `"adaptive"`)
  * **Meaning**: The negotiated maximum chunk block size. Use `"adaptive"` (recommended) or specify a literal integer cap.
* **`inflight_window_bytes`** (Default: `67108864`)
  * **Meaning**: Internal protocol send window for unacknowledged transfer data.
* **`batch_bytes`** / **`batch_chunks`** (Default: `0` / `1`)
  * **Meaning**: Optional internal protocol chunk batching. Defaults keep the legacy one-message-per-chunk path.
* **`preallocate_files`**, **`cache_hints`**, **`streaming_verification`**, **`tcp_info_window`**
  * **Meaning**: Internal protocol Windows performance/verification experiments. Defaults are disabled while benchmarking.

#### `[protocol.tls]`
* **`enable`** (Default: `false`)
  * **Meaning**: Enables TLS transport for the NetCopy internal protocol. This is not the SSH/SFTP data path.
* **`tls_server_cert_file`** (Default: `"ssh_cert.pem"`)
  * **Meaning**: Path to the server's public certificate file.
* **`tls_server_key_file`** (Default: `"ssh_host_ec_key.der"`)
  * **Meaning**: Path to the server's private key file.
* **`tls_dh_file`** (Default: `""`)
  * **Meaning**: Path to custom Diffie-Hellman parameters.
* **`tls_client_cert_validation`** (Default: `false`)
  * **Meaning**: Enforces client-certificate Mutual Authentication (Two-Way TLS).
* **`tls_client_chain_validation`** (Default: `false`)
  * **Meaning**: Validates the client's certificate chain.
* **`tls_trusted_chain_file`** (Default: `""`)
  * **Meaning**: Trusted root CA bundle to verify client certificates.

#### `[performance]`
* **`max_bandwidth_percent`** (Default: `100`)
  * **Meaning**: Server bandwidth throttle limit as a percentage of the network adapter speed.
* **`max_file_size`** (Default: `0`)
  * **Meaning**: The maximum file size accepted for uploads in bytes (`0` for unlimited).

#### `[logging]`
* **`enable`** (Default: `true`)
  * **Meaning**: Enables logging outputs to a log file.
* **`log_level`** (Default: `"INFO"`)
  * **Meaning**: Logger verbosity (`TRACE`, `DEBUG`, `INFO`, `WARNING`, `ERROR`).
* **`log_file`** (Default: `"server.log"`)
  * **Meaning**: Path to write server logs.
* **`log_format`** (Default: `"text"`)
  * **Meaning**: Structure of log records (`"text"` or `"json"`).
* **`audit_file`** (Default: `""`)
  * **Meaning**: Path to a dedicated security audit log file recording transfer requests and access decisions.

#### `[paths]`
* **`allowed_paths`** (Default: `{"D:\src\net_copy\"}`)
  * **Meaning**: Directory access control list. The server rejects any reads/writes targeting folders outside this list. Can be specified multiple times.
* **`auto_create_directories`** (Default: `true`)
  * **Meaning**: Automatically create missing directories on receipt of files.

---

### Windows Service

To deploy NetCopy as a background Windows service, run the following commands in an Administrator terminal:

```powershell
# Install the Windows service
.\net_copy_service.exe install

# Start and Stop using standard commands
net start NetCopyServer
net stop NetCopyServer

# Alternatively, manage directly using the service utility
.\net_copy_service.exe start
.\net_copy_service.exe stop

# Remove the service
.\net_copy_service.exe uninstall
```

The service processes the `server.conf` file located in its installation folder.

---

## Client

### Running the Client

```
net_copy_client [options] <source> <destination>

Options:
  -c, --config FILE          Path to client configuration file (default: client.conf)
  -p, --port PORT            Override the remote server port
  -R, --recursive            Transfer directories recursively
  --resume                   Skip already-received bytes to resume interrupted transfers
  --auto-create              Instruct server to auto-create destination folders (default)
  --no-auto-create           Fail if destination directories do not exist on the server
  --no-empty-dirs            Do not replicate empty subdirectories in recursive transfers
  -s, --security LEVEL       Set encryption mode: high (default) | fast | aes | AES-256-GCM
  -g, --get, --download      Download mode (source is remote server, destination is local path)
  -v, --verbose [LEVEL]      Force enable console output and set its logging level (defaults to DEBUG if LEVEL is omitted). Overrides the configuration file's console settings, but keeps file logging settings unchanged.
  -h, --help                 Display this help message
```

---

### Destination Format

Remote paths conform to the following specifications:

```
<server>:<port>/<remote-path>   →   127.0.0.1:1245/D:/Work/Files/
<server>:<remote-path>          →   127.0.0.1:D:\Work\FILES\        (uses port from config/default)
<server>:/remote-path           →   192.168.1.5:/var/lib/net_copy/  (Unix target path)
<server>                        →   192.168.1.5                     (default port, root directory)
```

---

### client.conf Reference

The client configuration file (`client.conf`) defines security parameters, connection properties, throttling rules, and authentication credentials.

#### `[connection]`
* **`timeout`** (Default: `30`)
  * **Meaning**: Handshake timeout limit in seconds.
* **`keep_alive`** (Default: `true`)
  * **Meaning**: Sends TCP keep-alive probes on idle connections.
* **`udp`** (Default: `false`)
  * **Meaning**: Use R-UDP transport instead of TCP.
* **`socket_buffer_size`** (Default: `0`)
  * **Meaning**: Client socket buffer size in bytes (`0` to use OS defaults).

#### `[protocol]`
* **`default_protocol`** (Default: `"internal"`)
  * **Meaning**: Default protocol scheme when none is specified. TLS is enabled separately and wraps the internal protocol.

#### `[protocol.internal]`
* **`secret_key`** (Default: `""`)
  * **Meaning**: Hex-encoded master key used for transport encryption and session derivation. Must match the server.
* **`username`** (Default: `"admin"`)
  * **Meaning**: The username identifying the client connection.
* **`auth_method`** (Default: `"none"`)
  * **Meaning**: User authentication scheme to run.
  * **Options**: `"none"`, `"password"`, `"mlkem"`.
* **`password`** (Default: `""`)
  * **Meaning**: Plaintext user password.
* **`password_encrypted`** (Default: `""`)
  * **Meaning**: Base64 encrypted password blob (generated using `net_copy_admin encrypt-password`). 
* **`security_level`** (Default: `"HIGH"`)
  * **Meaning**: Client-side requested encryption algorithm.
  * **Options**: `"HIGH"`, `"FAST"`, `"AES"`, `"AES-256-GCM"`.
* **`max_chunk_size`** (Default: `"adaptive"`)
  * **Meaning**: Max chunk size limit.
* **`initial_chunk_size`** (Default: `262144` / 256 KB)
  * **Meaning**: Size of the first data block sent.
* **`min_chunk_size`** (Default: `8192` / 8 KB)
  * **Meaning**: Lower boundary of the adaptive congestion control block sizing.
* **`chunk_size_increase_factor`** (Default: `1.1`)
  * **Meaning**: AIMD growth multiplier on successful chunk ACK.
* **`chunk_size_decrease_factor`** (Default: `0.5`)
  * **Meaning**: AIMD shrinking multiplier upon encountering connection delays or packet drops.
* **`inflight_window_bytes`** (Default: `67108864`)
  * **Meaning**: Internal protocol send window for unacknowledged transfer data.
* **`batch_bytes`** / **`batch_chunks`** (Default: `0` / `1`)
  * **Meaning**: Optional internal protocol chunk batching. Defaults keep the legacy one-message-per-chunk path.
* **`preallocate_files`**, **`cache_hints`**, **`streaming_verification`**, **`tcp_info_window`**
  * **Meaning**: Internal protocol Windows performance/verification experiments. Defaults are disabled while benchmarking.

#### `[performance]`
* **`max_bandwidth_percent`** (Default: `100`)
  * **Meaning**: Upload bandwidth throttle percentage.
* **`retry_attempts`** (Default: `3`)
  * **Meaning**: Number of automatic reconnection/resume attempts on network loss.
* **`retry_delay`** (Default: `5`)
  * **Meaning**: Delay between reconnection attempts in seconds.

#### `[transfer]`
* **`auto_create_directories`** (Default: `true`)
  * **Meaning**: Instructs the server to create target directories if missing.
* **`create_empty_directories`** (Default: `true`)
  * **Meaning**: Copy empty subfolders during recursive directory transfers.

#### `[proxy]`
* **`type`** (Default: `"none"`)
  * **Meaning**: Routing proxy type (`"none"`, `"socks5"`, `"http"`).
* **`host`** / **`port`** / **`username`** / **`password`**: Credentials and server parameters of the proxy server.

#### `[gui]`
* **`port`** (Default: `1246`)
  * **Meaning**: Web server port the GUI utility binds to.
* **`open_browser_on_start`** (Default: `true`)
  * **Meaning**: Launch the system's default browser to load the GUI dashboard immediately on start.
* **`theme`** (Default: `"system"`)
  * **Meaning**: Visual appearance mode (`"system"`, `"light"`, `"dark"`).
* **`language`** (Default: `"en"`)
  * **Meaning**: Frontend interface language.

---

## GUI Client

The NetCopy GUI client (`net_copy_gui.exe`) provides an interactive interface for viewing connection statuses, queuing file transfers, browsing remote and local file directories, and managing configurations.

* To start the interface, launch `net_copy_gui.exe` inside your terminal or by double-clicking.
* Under Windows, it automatically launches your browser pointing to the correct address (e.g. `http://localhost:1246/`).
* Configuration variables such as binding port, theme, and language are read directly from the `[gui]` section of [client.conf](file:///D:/src/net_copy/build_vs/client.conf).

---

## User Management & Authentication

NetCopy features two user-level authentication mechanisms modeled on modern SSH standards:
1. **Password**: A secure challenge-response protocol powered by PBKDF2-SHA3-256 (the plaintext password is never sent over the network).
2. **ML-KEM Key**: A post-quantum-ready key encapsulation mechanism (CRYSTALS-Kyber, FIPS 203) verified via dynamic challenge token exchange.

### How Authentication Works

#### Password Authentication
```
Server generates:  challenge_nonce (32 random bytes) + user's salt (from database)
Server sends:      nonce + salt to Client
Client calculates: PK = PBKDF2(password, salt, iterations)
Client sends:      proof = SHA3-256( PK ‖ nonce )
Server compares:   SHA3-256( stored_hash ‖ nonce )  ← constant-time comparison
```
The password is **never transmitted** in cleartext.

#### ML-KEM Key Authentication
```
Server loads:      User public key from database
Server executes:   Encapsulate(public_key) → ciphertext + shared_secret
Server sends:      ciphertext + random challenge nonce to Client
Client executes:   Decapsulate(private_key, ciphertext) → shared_secret
Client sends:      proof = SHA3-256( shared_secret ‖ challenge nonce )
Server compares:   SHA3-256( server_shared_secret ‖ challenge nonce )
```
The private key **never leaves** the client machine.

---

### net_copy_admin Tool

`net_copy_admin` is the primary CLI tool used to manage the server-side user database, generate post-quantum ML-KEM keys, encrypt passwords, list remote directories, and verify authentication profiles.

```
net_copy_admin <command> [options]
```

#### `keygen` — Generate an ML-KEM key pair
```
net_copy_admin keygen --level 512|768|1024 --out STEM [--encrypt] [--passphrase PASS]
```
Creates two files:
* `STEM.pub` — public key (distribute to server administrators)
* `STEM.pem` — private key (must be kept secret on client machine)

| Parameter | Required | Description |
|-----------|----------|-------------|
| `--level` | No (default: 768) | NIST security level: `512`, `768`, or `1024` |
| `--out` | **Yes** | Output filename prefix (stem) |
| `--encrypt` | No | Encrypt the private key file using AES-256-CTR |
| `--passphrase` | No | Passphrase for encrypting the key (prompts if omitted) |

```powershell
# Generate standard ML-KEM-768 key pair
.\net_copy_admin.exe keygen --level 768 --out alice

# Generate and encrypt the private key file with a passphrase
.\net_copy_admin.exe keygen --level 768 --encrypt --out alice
```

#### `showpubkey` — View public key info from PEM file
```
net_copy_admin showpubkey KEY.pem [--passphrase PASS]
```
Prints the algorithm type, key sizes, and base64-encoded public key.

```powershell
.\net_copy_admin.exe showpubkey alice.pem --passphrase "KeyPass"
```

#### `encrypt-key` & `decrypt-key` — Passphrase operations
```powershell
# Passphrase-protect an existing key file
.\net_copy_admin.exe encrypt-key alice.pem --passphrase "KeyPass" --out alice_encrypted.pem

# Decrypt an encrypted key back to plain PEM format
.\net_copy_admin.exe decrypt-key alice_encrypted.pem --passphrase "KeyPass" --out alice_plain.pem
```

#### `encrypt-password` — Encrypt credentials for client.conf
```
net_copy_admin encrypt-password [--passphrase PASS] [--pass PASSWORD]
```
Returns a base64-encoded AES-CTR encrypted password blob that can be saved in `client.conf` as `password_encrypted` for secure startup configuration.

```powershell
.\net_copy_admin.exe encrypt-password --passphrase "MasterPass" --pass "MySecret"
```

#### `adduser` — Create a database user
```
net_copy_admin adduser --users CSV_FILE --name USERNAME [--pass PASS] [--pubkey PUBKEY.pub] [--paths PATHS] [--methods METHODS]
```
Adds a user to the CSV database.
* `--paths`: Comma-separated absolute directories allowed for access, or `*` for all.
* `--methods`: Permitted authentication modes (`password`, `mlkem`, or `password,mlkem`).

```powershell
# Add user Carol with password auth, restricted to one path
.\net_copy_admin.exe adduser --users users.csv --name carol --pass "MyPass123!" --paths "D:\Work\FILES"

# Add user Bob with ML-KEM key auth
.\net_copy_admin.exe adduser --users users.csv --name bob --pubkey bob.pub --paths "*" --methods mlkem
```

#### `passwd` — Change user password
```powershell
.\net_copy_admin.exe passwd --users users.csv --name carol
```
Generates a new random salt and hashes the updated credentials.

#### `setkey` — Update ML-KEM public key
```powershell
.\net_copy_admin.exe setkey --users users.csv --name bob --pubkey bob_new.pub
```

#### `deluser` — Remove a user
```powershell
.\net_copy_admin.exe deluser --users users.csv --name carol
```

#### `listusers` — Print database entries
```powershell
.\net_copy_admin.exe listusers --users users.csv
```

#### `verify` — Remote connection credentials validation
```
net_copy_admin verify --host HOST --name NAME [--pass PASS] [--key KEY.pem] [--passphrase PASS]
```
Tests credentials against a running NetCopy server by running a complete handshake and auth phase without executing any file writes.

```powershell
.\net_copy_admin.exe verify --host 127.0.0.1:1245 --name bob --key bob.pem
```

#### `ls` — Remote directory listing
```
net_copy_admin ls --host HOST --name NAME [--pass PASS] [--key KEY.pem] [--passphrase PASS] --remote PATH [--recursive]
```
Query and list directories on the remote server.

```powershell
.\net_copy_admin.exe ls --host 127.0.0.1:1245 --name bob --key bob.pem --remote "D:/Work/FILES"
```

---

### Password Authentication Setup

1. Add the user to the database:
   ```powershell
   .\net_copy_admin.exe adduser --users users.csv --name alice --pass "S3cr3t!" --paths "D:\Work\FILES"
   ```
2. Start the server (make sure `users_file = users.csv` is configured in `server.conf`).
3. Set the following in `client.conf`:
   ```ini
   [auth]
   username = alice
   auth_method = password
   password = S3cr3t!
   ```
4. Transfer files:
   ```powershell
   .\net_copy_client.exe test.zip 127.0.0.1:D:\Work\FILES\
   ```

---

### ML-KEM Key Authentication Setup

1. Generate key pair on client machine:
   ```powershell
   .\net_copy_admin.exe keygen --level 768 --encrypt --passphrase "KeyPass" --out alice
   ```
2. Give `alice.pub` to the server administrator.
3. Administrator registers the user:
   ```powershell
   .\net_copy_admin.exe adduser --users users.csv --name alice --pubkey alice.pub --paths "*" --methods mlkem
   ```
4. Configure `client.conf` with:
   ```ini
   [auth]
   username = alice
   auth_method = mlkem
   private_key_file = alice.pem
   private_key_passphrase = KeyPass
   ```
5. Transfer files:
   ```powershell
   .\net_copy_client.exe test.zip 127.0.0.1:D:\Work\FILES\
   ```

---

## Encryption Modes

Select the cipher engine using `-s` / `--security` on the client, or via configuration settings.

| Identifier | CLI Flag | Cipher Engine | Performance / Hardware Requirements | NIST Security Level |
|------------|----------|---------------|-------------------------------------|---------------------|
| `high` | `-s high` | ChaCha20-Poly1305 | Software implementation, very fast | ★★★★★ (Default) |
| `aes` | `-s aes` | AES-128-CTR | Hardware-accelerated (AES-NI instructions) | ★★★★ |
| `AES-256-GCM` | `-s AES-256-GCM` | AES-256-GCM | GPU accelerated (with CPU fallback) | ★★★★★ |
| `fast` | `-s fast` | XOR rolling key | Maximum speed, minimal CPU | ★ (Testing only) |

```powershell
# Use AES-128-CTR with AES-NI instructions
.\net_copy_client.exe -s aes large_archive.zip 127.0.0.1:D:\Work\

# Use GPU-accelerated AES-256-GCM
.\net_copy_client.exe -s AES-256-GCM large_archive.zip 127.0.0.1:D:\Work\
```

### Security Level Negotiation

The server and client coordinate encryption during handshake negotiation:
1. If the server has a strict security setting (e.g. `security_level = AES` or `HIGH` in `server.conf`), the server forces that level and responds with its selection.
2. If the server has `security_level = auto`, it accepts the client's requested level.
3. **Mismatch Prevention**: If the client explicitly requests a level (e.g. `--security HIGH`) but the server returns a different negotiated level, the client immediately aborts the connection with a `CryptoException` to prevent downgrade attacks.

---

## Examples & Use Cases

### Copy a single file to a remote server
```powershell
.\net_copy_client.exe C:\Reports\Q4_2026.xlsx 192.168.1.50:D:\Shared\Reports\
```

### Copy a file overriding the default port
```powershell
.\net_copy_client.exe -p 9000 C:\Reports\Q4_2026.xlsx 192.168.1.50:D:\Shared\Reports\
```

### Recursively back up a folder
```powershell
.\net_copy_client.exe -R C:\Users\Me\Documents\ 192.168.1.50:D:\Backup\Documents\
```

### Resume a large interrupted file copy
```powershell
# Initial upload attempt
.\net_copy_client.exe C:\ISOs\ubuntu-26.04.iso 192.168.1.50:D:\Downloads\

# If network drops, resume later (already received bytes are skipped)
.\net_copy_client.exe --resume C:\ISOs\ubuntu-26.04.iso 192.168.1.50:D:\Downloads\
```

### Pull/Download a file from the server
Use the `--get` / `--download` flag. The first argument specifies the remote path on the server, and the second is the local target:
```powershell
# Download database.sql from remote server to C:\LocalBackup\
.\net_copy_client.exe --get 192.168.1.50:D:\Backup\database.sql C:\LocalBackup\

# Download a directory recursively
.\net_copy_client.exe --get -R 192.168.1.50:D:\Shared\Assets C:\LocalAssets\
```

### Verbose Console Logging override
By default, console logging outputs at the level specified in the configuration files (`client.conf` and `server.conf`). You can override this behavior on the CLI using `-v` or `--verbose` independent of what the configuration files tell:
* **No level specified**: Force enables console logging at the `DEBUG` level.
* **Level specified (e.g. `-v INFO`)**: Force enables console logging at that specific level.
* *Note*: File logging settings in the configuration (path, file level) remain unaffected and are still respected.

```powershell
# Run client, force enabling console logging at DEBUG level
.\net_copy_client.exe -v large_file.zip 127.0.0.1:D:\Work\

# Run server, forcing console logging to only show INFO level and above
.\net_copy_server.exe -v INFO -l 0.0.0.0:1245 -a "D:\Shared"
```

### Throttled transfer
```ini
# Limit upload speed in client.conf
[performance]
max_bandwidth_percent = 40  # Use at most 40% of detected connection link speed
```
```powershell
.\net_copy_client.exe bigfile.bin 192.168.1.50:D:\Work\
```

### Compression Bypass for Incompressible Formats
NetCopy automatically bypasses compression logic for binary, high-entropy, or already-compressed file extensions to prevent CPU bottlenecks:
* Modelfiles: `.gguf`, `.safetensors`, `.bin`, `.pt`, `.onnx`
* Audio/Video: `.mp3`, `.mp4`, `.avi`, `.flac`, `.ogg`, `.mpg`, `.mpeg`
* Images: `.jpg`, `.jpeg`, `.png`, `.gif`
* Compressed Archives: `.zip`, `.gz`, `.bz2`, `.rar`, `.7z`, `.lz4`, `.tar`, `.tgz`
* Documents: `.pdf`

---

## users.csv Format

The user credentials database is stored in a semicolon-separated format. It is recommended to edit this file using `net_copy_admin` rather than modifying it manually.

```
# net_copy user database
# Fields: username;auth_methods;pbkdf2_hash_hex;salt_hex;iterations;mlkem_level;mlkem_pubkey_b64;allowed_paths
alice;password,mlkem;a3f9...;deadbeef...;200000;ML-KEM-768;AAAB...;D:\Work\FILES
readonly;password;b4e1...;cafebabe...;200000;;;D:\Reports
svc;mlkem;;;0;ML-KEM-512;CCCD...;*
```

| Field Name | Description |
|------------|-------------|
| `username` | Connecting user identifier |
| `auth_methods` | Permitted auth modes (`password`, `mlkem`, or `password,mlkem`) |
| `pbkdf2_hash_hex` | Hex-encoded PBKDF2-SHA3-256 password hash |
| `salt_hex` | 16-byte random salt in hex |
| `iterations` | PBKDF2 hashing loop count (default: 200,000) |
| `mlkem_level` | CRYSTALS-Kyber public key size (`ML-KEM-512`, `ML-KEM-768`, or `ML-KEM-1024`) |
| `mlkem_pubkey_b64` | Base64-encoded public key representation |
| `allowed_paths` | Comma-separated list of absolute directories this user is authorized to access (or `*` for unrestricted server access) |
