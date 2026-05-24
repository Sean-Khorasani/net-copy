# NetCopy

**NetCopy** is a fast, encrypted, resumable file transfer tool — similar in spirit to `scp` or `rsync`, but built around a persistent server daemon and post-quantum-ready cryptography.

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
- [User Management & Authentication](#user-management--authentication)
  - [How Authentication Works](#how-authentication-works)
  - [net_copy_admin Tool](#net_copy_admin-tool)
  - [Password Authentication](#password-authentication)
  - [ML-KEM Key Authentication](#ml-kem-key-authentication)
- [Encryption Modes](#encryption-modes)
- [Examples & Use Cases](#examples--use-cases)

---

## Quick Start

> Copy and paste these commands. They work on a single Windows machine (server and client both on `127.0.0.1`).

**Step 1 — Start the server** (in one terminal):
```powershell
cd build_vs
.\net_copy_server.exe -l 127.0.0.1:1245 -a "D:\Work\files"
```

**Step 2 — Transfer a file** (in another terminal):
```powershell
cd build_vs
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

That's it. No setup required for a quick test — the tool uses a default pre-shared key if none is configured.

---

## Building

### Windows (Recommended — Visual Studio Build Tools)
```powershell
.\build_vs_cli.bat
```
Outputs all executables to `build_vs\`.

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
Outputs to `build/`.

### Dependencies (managed automatically by vcpkg)
| Package | Purpose |
|---------|---------|
| `lz4` | Fast compression for transfer data |
| `liboqs` | ML-KEM post-quantum key encapsulation |

---

## Architecture Overview

```
┌─────────────────────┐       TCP/IP        ┌─────────────────────┐
│   net_copy_client   │ ──────────────────► │   net_copy_server   │
│                     │                     │                     │
│  reads local file   │   encrypted chunks  │  writes remote file │
│  flow-controlled    │ ◄────────────────── │  sends ACKs         │
│  pipeline sender    │                     │  per-user ACL       │
└─────────────────────┘                     └─────────────────────┘
         │                                           │
   client.conf                                 server.conf
   (auth, crypto,                              (paths, users,
    bandwidth)                                  crypto, logging)
```

**Handshake sequence:**
1. Client → Server: version, security level, username, auth method
2. Server → Client: accepted parameters, auth challenge (if auth enabled)
3. Client → Server: auth proof (password hash or ML-KEM decapsulation proof)
4. Server → Client: auth result — connection proceeds or is rejected
5. Client streams `FILE_DATA` chunks; server sends `FILE_ACK` per chunk
6. Flow control: up to 64 MB of unacknowledged data in flight at once

---

## Server

### Running the Server

```
net_copy_server [options]

Options:
  -l, --listen ADDRESS:PORT   Listen address and port  (default: 0.0.0.0:1245)
  -a, --access PATH           Directory allowed for file writes
  -c, --config FILE           Path to configuration file
  -d, --daemon                Run as background daemon (Linux)
  --auto-create               Create missing destination directories (default)
  --no-auto-create            Reject writes to non-existent directories
  -v, --verbose               Enable DEBUG-level logging
  -h, --help                  Show help
```

#### Basic examples

```powershell
# Listen on all interfaces, port 1245, allow writes to D:\Shared
.\net_copy_server.exe -l 0.0.0.0:1245 -a "D:\Shared"

# Listen on localhost only with a custom config file
.\net_copy_server.exe -l 127.0.0.1:1245 -c "C:\config\server.conf"

# Run with verbose logging (shows every chunk and ACK)
.\net_copy_server.exe -v

# Multiple allowed paths — use the config file (see below)
.\net_copy_server.exe -c server.conf
```

> **Note:** Command-line flags override the config file. The server looks for `server.conf` in the current directory automatically if `-c` is not given.

---

### server.conf Reference

The server configuration file (`server.conf`) is organized into sections. Below is the detailed reference of all available parameters, their meanings, default values, and possible values.

#### `[network]`
* **`listen_address`** (Default: `"0.0.0.0"`)
  * **Meaning**: The local IP address on which the server listens for incoming connections.
  * **Possible Values**: `0.0.0.0` (binds to all IPv4 interfaces), `127.0.0.1` (localhost only), any specific local interface IP, or `::` / `::1` for IPv6 dual-stack.
* **`listen_port`** (Default: `1245`)
  * **Meaning**: The TCP port on which the server listens.
  * **Possible Values**: Any valid unused port number between `1` and `65535`.
* **`max_connections`** (Default: `10`)
  * **Meaning**: Maximum number of simultaneous active client connections.
  * **Possible Values**: Any positive integer (e.g. `1` to `1000`).
* **`timeout`** (Default: `30`)
  * **Meaning**: Connection inactivity timeout in seconds. If a connection is idle with no packets for this duration, it is dropped.
  * **Possible Values**: Any positive integer.

#### `[security]`
* **`secret_key`** (Default: `""`)
  * **Meaning**: Hex-encoded key used for transport-layer encryption. If a user database is present and `require_auth` is enabled, this is still used for session key derivation.
  * **Possible Values**: A 64-character hexadecimal string representing a 256-bit key, prefixed with `0x` (e.g. `0xebf5fa7d...`).
* **`require_auth`** (Default: `true`)
  * **Meaning**: Enforce user authentication against the user database (`users.csv`) for all connections.
  * **Possible Values**: `true`, `false`.
* **`max_file_size`** (Default: `0`)
  * **Meaning**: The maximum file size (in bytes) the server will accept for transfer.
  * **Possible Values**: `0` (unlimited size) or any positive 64-bit integer. For example, `1073741824` is 1 GB.

#### `[performance]`
* **`max_bandwidth_percent`** (Default: `0`)
  * **Meaning**: Global server-side bandwidth throttle percentage based on detected link speed.
  * **Possible Values**: `0` or `100` (unlimited, no throttle), or `1` through `99` (e.g. `50` restricts speeds to 50% of the adapter link speed).
* **`max_chunk_size`** (Default: `10485760` / 10 MB)
  * **Meaning**: Maximum size (in bytes) of data payload chunks the server will negotiate.
  * **Possible Values**: Positive integers up to the protocol-enforced cap of `33554432` (32 MB).
* **`socket_buffer_size`** (Default: `0`)
  * **Meaning**: Explicit TCP socket send and receive buffer size in bytes.
  * **Possible Values**: `0` (uses operating system default auto-tuning) or positive integers (e.g. `65536` for 64 KB, `1048576` for 1 MB).

#### `[logging]`
* **`log_level`** (Default: `"INFO"`)
  * **Meaning**: Logger verbosity level.
  * **Possible Values**: `"DEBUG"`, `"INFO"`, `"WARNING"`, `"ERROR"`.
* **`log_file`** (Default: `"server.log"`)
  * **Meaning**: Path to the log file.
  * **Possible Values**: Any valid writeable filepath, or `""` (empty) to disable logging to a file.
* **`log_format`** (Default: `"text"`)
  * **Meaning**: Formatting structure for log messages.
  * **Possible Values**: `"text"` (standard readable logs) or `"json"` (structured JSON lines for aggregators).
* **`audit_log`** (Default: `""`)
  * **Meaning**: Path to the transfer audit log file which logs successes/failures.
  * **Possible Values**: Any valid writeable filepath, or `""` (empty) to disable audit logging.
* **`console_output`** (Default: `true`)
  * **Meaning**: Replicate log outputs to the server terminal.
  * **Possible Values**: `true`, `false`.

#### `[daemon]`
* **`run_as_daemon`** (Default: `false`)
  * **Meaning**: Runs the server process in the background (Linux/Unix only).
  * **Possible Values**: `true`, `false`.
* **`pid_file`** (Default: `"/var/run/net_copy_server.pid"`)
  * **Meaning**: Path to the daemon process ID file.
  * **Possible Values**: Any valid writeable filepath.

#### `[paths]`
* **`allowed_paths`** (Default: `{"/var/lib/net_copy"}`)
  * **Meaning**: List of directories allowed for client reads and writes. Can be specified multiple times.
  * **Possible Values**: Absolute paths on the server.
* **`auto_create_directories`** (Default: `true`)
  * **Meaning**: Auto-create parent folders for incoming files if they do not exist.
  * **Possible Values**: `true`, `false`.

#### `[auth]`
* **`users_file`** (Default: `"users.csv"`)
  * **Meaning**: Path to the CSV file representing the user database.
  * **Possible Values**: Any valid readable/writeable filepath.
* **`allow_anonymous`** (Default: `false`)
  * **Meaning**: Allow connections with no username even when the user database is loaded.
  * **Possible Values**: `true`, `false`.

#### Sample `server.conf` Template:

```ini
[network]
listen_address = 0.0.0.0
listen_port = 1245
max_connections = 10
timeout = 30

[security]
secret_key = 0xebf5fa7d...
require_auth = true
max_file_size = 0

[performance]
max_bandwidth_percent = 0
max_chunk_size = 10485760
socket_buffer_size = 0

[logging]
log_level = INFO
log_file = server.log
log_format = text
audit_log = audit.log
console_output = true

[daemon]
run_as_daemon = false
pid_file = /var/run/net_copy_server.pid

[paths]
allowed_paths = D:\Work\FILES
allowed_paths = D:\Work\Backup
auto_create_directories = true

[auth]
users_file = users.csv
allow_anonymous = false
```

---

### Windows Service

Install and manage NetCopy as a Windows service:

```powershell
# Install the service (run as Administrator)
.\net_copy_service.exe install

# Start / stop
net start NetCopyServer
net stop NetCopyServer

# Or use the service executable directly
.\net_copy_service.exe start
.\net_copy_service.exe stop

# Remove the service
.\net_copy_service.exe uninstall
```

The service reads `server.conf` from the same directory as the executable.

---

## Client

### Running the Client

```
net_copy_client [options] <source> <destination>

Options:
  -c, --config FILE          Path to configuration file
  -p, --port PORT            Override server port
  -R, --recursive            Transfer a directory and all its contents recursively
  --resume                   Resume an interrupted transfer (skip already-received bytes)
  --auto-create              Create missing destination directories (default)
  --no-auto-create           Fail if destination directory doesn't exist
  --no-empty-dirs            Skip empty subdirectories during recursive transfer
  -s, --security LEVEL       Encryption: high (default) | fast | aes | AES-256-GCM
  -g, --get, --download      Download/pull file/directory from the server (source becomes remote, destination becomes local)
  -v, --verbose              Show debug info and live connection details
  -h, --help                 Show help
```

### Destination/Source Format

For uploading, the `<destination>` is remote. For downloading (using `--get`), the `<source>` is remote. The remote path format is:

```
<server>:<port>/<remote-path>   →   127.0.0.1:1245/D:/Work/
<server>:<remote-path>          →   127.0.0.1:D:\Work\          (port from config/default)
<server>:/remote-path           →   192.168.1.5:/mnt/data/      (absolute Unix path)
<server>                        →   192.168.1.5                  (default port, root path)
```

> On Windows, both `\` and `/` work in remote paths. The client normalises them automatically.

### client.conf Reference

The client configuration file (`client.conf`) configures security, performance, connection, logging, and authentication parameters for client transfers. Below is the detailed reference of all available parameters.

#### `[security]`
* **`secret_key`** (Default: `""`)
  * **Meaning**: Hex-encoded key used for transport-layer encryption. Must match the server's `secret_key`.
  * **Possible Values**: A 64-character hexadecimal string representing a 256-bit key, prefixed with `0x`.

#### `[performance]`
* **`max_bandwidth_percent`** (Default: `0`)
  * **Meaning**: Client-side upload bandwidth throttle percentage based on detected link speed.
  * **Possible Values**: `0` or `100` (unlimited), or `1` through `99` (e.g. `25` restricts upload to 25% of the link speed).
* **`retry_attempts`** (Default: `3`)
  * **Meaning**: Number of times the client will attempt to reconnect and resume after transient network failures.
  * **Possible Values**: `0` (disabled) or any positive integer.
* **`retry_delay`** (Default: `5`)
  * **Meaning**: Number of seconds to wait between retry attempts.
  * **Possible Values**: Any positive integer.
* **`socket_buffer_size`** (Default: `0`)
  * **Meaning**: Explicit TCP socket send and receive buffer size in bytes.
  * **Possible Values**: `0` (uses OS auto-tuning) or positive integers.
* **`initial_chunk_size`** (Default: `262144` / 256 KB)
  * **Meaning**: Initial size of data chunks in bytes when a transfer starts.
  * **Possible Values**: Positive integers (e.g. `4096` to `33554432`).
* **`min_chunk_size`** (Default: `8192` / 8 KB)
  * **Meaning**: Minimum chunk size that the adaptive congestion control (AIMD) can scale down to.
  * **Possible Values**: Positive integers.
* **`max_chunk_size`** (Default: `10485760` / 10 MB)
  * **Meaning**: Maximum chunk size that the AIMD algorithm can scale up to.
  * **Possible Values**: Positive integers up to `33554432` (32 MB).
* **`chunk_size_increase_factor`** (Default: `1.1`)
  * **Meaning**: Multiplier used to grow chunk size after each successful chunk acknowledgment (Additive Increase).
  * **Possible Values**: Floats greater than `1.0`.
* **`chunk_size_decrease_factor`** (Default: `0.5`)
  * **Meaning**: Multiplier used to shrink chunk size upon experiencing packets dropping or delays (Multiplicative Decrease).
  * **Possible Values**: Floats between `0.0` and `1.0`.

#### `[logging]`
* **`log_level`** (Default: `"INFO"`)
  * **Meaning**: Client log verbosity level.
  * **Possible Values**: `"DEBUG"`, `"INFO"`, `"WARNING"`, `"ERROR"`.
* **`log_file`** (Default: `"client.log"`)
  * **Meaning**: Path to the client log file.
  * **Possible Values**: Any valid writeable filepath, or `""` (empty) to disable logging to file.
* **`log_format`** (Default: `"text"`)
  * **Meaning**: Format style for logs.
  * **Possible Values**: `"text"`, `"json"`.
* **`console_output`** (Default: `true`)
  * **Meaning**: Replicate log outputs to stdout/stderr in the terminal window.
  * **Possible Values**: `true`, `false`.

#### `[connection]`
* **`timeout`** (Default: `30`)
  * **Meaning**: Timeout in seconds for connection establishment and handshake. Once data transfer starts, this timeout is deactivated.
  * **Possible Values**: Any positive integer.
* **`keep_alive`** (Default: `true`)
  * **Meaning**: Enable TCP keep-alive probes on the socket.
  * **Possible Values**: `true`, `false`.

#### `[transfer]`
* **`auto_create_directories`** (Default: `true`)
  * **Meaning**: Request the server to auto-create parent destination folders if missing.
  * **Possible Values**: `true`, `false`.
* **`create_empty_directories`** (Default: `true`)
  * **Meaning**: Replicate empty subdirectories at the destination when doing recursive directory transfers.
  * **Possible Values**: `true`, `false`.

#### `[auth]`
* **`username`** (Default: `""`)
  * **Meaning**: Username for client authentication on the server.
  * **Possible Values**: Registered usernames in the server's user database.
* **`auth_method`** (Default: `"none"`)
  * **Meaning**: Client authentication mechanism.
  * **Possible Values**: `"none"`, `"password"`, `"mlkem"`.
* **`password`** (Default: `""`)
  * **Meaning**: Plaintext password for password authentication.
  * **Possible Values**: Any string.
* **`password_encrypted`** (Default: `""`)
  * **Meaning**: Master passphrase encrypted password blob.
  * **Possible Values**: Valid base64-encoded encrypted password blob produced by `net_copy_admin encrypt-password`.
* **`private_key_file`** (Default: `""`)
  * **Meaning**: Path to the client's ML-KEM private key file (PEM format).
  * **Possible Values**: Valid filepath to a PEM key file.
* **`private_key_passphrase`** (Default: `""`)
  * **Meaning**: Passphrase to decrypt the private key file or `password_encrypted` blob. If omitted, the client will prompt you on startup.
  * **Possible Values**: Any string.

#### Sample `client.conf` Template:

```ini
[security]
secret_key = 0xebf5fa7d...

[performance]
max_bandwidth_percent = 0
retry_attempts = 3
retry_delay = 5
socket_buffer_size = 0
initial_chunk_size = 262144
min_chunk_size = 8192
max_chunk_size = 10485760
chunk_size_increase_factor = 1.1
chunk_size_decrease_factor = 0.5

[logging]
log_level = INFO
log_file = client.log
log_format = text
console_output = true

[connection]
timeout = 30
keep_alive = true

[transfer]
auto_create_directories = true
create_empty_directories = true

[auth]
# username = alice
# auth_method = password
# password = MySecret123
# password_encrypted = <encrypted-blob>
# private_key_file = alice.pem
# private_key_passphrase = KeyPass
```

---

## User Management & Authentication

NetCopy supports two authentication methods, similar to OpenSSH:
- **Password** — PBKDF2-SHA3-256 challenge-response (password never sent in plain text)
- **ML-KEM key** — post-quantum challenge-response using CRYSTALS-Kyber (FIPS 203)

### How Authentication Works

```
Password auth:
  Server sends:  challenge_nonce (32 random bytes) + user's stored salt
  Client sends:  SHA3-256( PBKDF2(password, salt, 200000) ‖ nonce )
  Server checks: SHA3-256( stored_hash ‖ nonce )   ← constant-time compare

ML-KEM key auth:
  Server calls:  Encaps(user_public_key) → ciphertext + shared_secret
  Server sends:  ciphertext + random kem_nonce
  Client calls:  Decaps(private_key, ciphertext) → shared_secret
  Client sends:  SHA3-256( shared_secret ‖ kem_nonce )
  Server checks: SHA3-256( its_shared_secret ‖ kem_nonce )
```

The password is **never transmitted**. The ML-KEM private key **never leaves the client**.

---

### net_copy_admin Tool

`net_copy_admin` is the all-in-one administration utility. It handles ML-KEM key generation, key file management, and the server-side user database. It replaces the older `net_copy_keygen`.

```
net_copy_admin <command> [options]
net_copy_admin --help
```

---

#### `keygen` — Generate an ML-KEM key pair

```
net_copy_admin keygen --level 512|768|1024 --out STEM [--encrypt] [--passphrase PASS]
```

Creates two files:
- `STEM.pub` — public key (share with server admin)
- `STEM.pem` — private key (keep secret on client machine)

| Flag | Required | Description |
|------|----------|-------------|
| `--level` | No (default: 768) | Key size: `512`, `768`, or `1024` |
| `--out` | **Yes** | Output filename stem (no extension) |
| `--encrypt` | No | Encrypt the private key with a passphrase |
| `--passphrase` | No | Passphrase for `--encrypt` (prompts interactively if omitted) |

**Key sizes (FIPS 203):**

| Level | Public key | Private key | Ciphertext | Security |
|-------|-----------|------------|-----------|---------|
| ML-KEM-512 | 800 B | 1,632 B | 768 B | NIST Level 1 |
| ML-KEM-768 | 1,184 B | 2,400 B | 1,088 B | NIST Level 3 ★ recommended |
| ML-KEM-1024 | 1,568 B | 3,168 B | 1,568 B | NIST Level 5 |

```powershell
# Recommended: ML-KEM-768 plain private key
.\net_copy_admin.exe keygen --level 768 --out alice

# ML-KEM-768 with encrypted private key (passphrase prompted)
.\net_copy_admin.exe keygen --level 768 --encrypt --out alice

# ML-KEM-768 with encrypted private key (passphrase inline — scripting)
.\net_copy_admin.exe keygen --level 768 --encrypt --passphrase "MyKeyPass" --out alice

# ML-KEM-512 (faster operations, smaller files)
.\net_copy_admin.exe keygen --level 512 --out fastkey

# ML-KEM-1024 (maximum security margin)
.\net_copy_admin.exe keygen --level 1024 --out strongkey
```

---

#### `showpubkey` — Display public key info from a private key file

```
net_copy_admin showpubkey KEY.pem [--passphrase PASS]
```

Reads the private key file and prints the algorithm, key sizes, and the base64-encoded public key. Useful to verify a key file or to extract the public key without regenerating the pair.

| Argument | Required | Description |
|----------|----------|-------------|
| `KEY.pem` | **Yes** | Path to private key file (positional, not a flag) |
| `--passphrase` | No | Passphrase if the key file is encrypted |

```powershell
# Plain private key
.\net_copy_admin.exe showpubkey alice.pem

# Encrypted private key
.\net_copy_admin.exe showpubkey alice.pem --passphrase "MyKeyPass"
```

Example output:
```
Private key file: alice.pem
Algorithm: ML-KEM-768
Private key size: 2400 bytes
Public key size: 1184 bytes
Public key (base64):
AAAB...base64...
```

---

#### `encrypt-key` — Add passphrase protection to a private key

```
net_copy_admin encrypt-key KEY.pem [--passphrase PASS] [--out OUT.pem]
```

Reads a plain private key and writes it back AES-256-CTR encrypted, protected by a PBKDF2-SHA3-256 derived key. If `--out` is omitted the file is overwritten in place.

| Argument | Required | Description |
|----------|----------|-------------|
| `KEY.pem` | **Yes** | Plain private key file (positional) |
| `--passphrase` | No | Passphrase (prompted interactively if omitted) |
| `--out` | No | Output file path (default: overwrite `KEY.pem`) |

```powershell
# Encrypt in place (prompts for passphrase)
.\net_copy_admin.exe encrypt-key alice.pem

# Encrypt to a new file
.\net_copy_admin.exe encrypt-key alice.pem --passphrase "MyKeyPass" --out alice_enc.pem
```

---

#### `decrypt-key` — Remove passphrase protection from a private key

```
net_copy_admin decrypt-key KEY.pem [--passphrase PASS] [--out OUT.pem]
```

Decrypts an encrypted private key back to plain format. If `--out` is omitted the file is overwritten in place.

| Argument | Required | Description |
|----------|----------|-------------|
| `KEY.pem` | **Yes** | Encrypted private key file (positional) |
| `--passphrase` | No | Passphrase (prompted interactively if omitted) |
| `--out` | No | Output file path (default: overwrite `KEY.pem`) |

```powershell
# Decrypt in place (prompts for passphrase)
.\net_copy_admin.exe decrypt-key alice_enc.pem

# Decrypt to a new file
.\net_copy_admin.exe decrypt-key alice_enc.pem --passphrase "MyKeyPass" --out alice_plain.pem
```

---

#### `encrypt-password` — Encrypt a password for client.conf

```
net_copy_admin encrypt-password [--passphrase PASS] [--pass PASSWORD]
```

Encrypts a password using AES-CTR + HMAC-SHA3-256 derived from a master passphrase. The output is a base64-encoded blob that can be saved in `client.conf` as `password_encrypted`.

| Flag | Required | Description |
|------|----------|-------------|
| `--passphrase` | No | Master passphrase to derive the key (prompted interactively if omitted) |
| `--pass` | No | Cleartext password to encrypt (prompted interactively if omitted) |

```powershell
# Interactive encryption (recommended — keeps password out of command history)
.\net_copy_admin.exe encrypt-password

# Scripted encryption
.\net_copy_admin.exe encrypt-password --passphrase "MyMasterKey" --pass "AliceSecret"
```

Save the generated line under `[auth]` in `client.conf`:
```ini
[auth]
username = alice
auth_method = password
password_encrypted = <generated-base64-blob>
```

When running the client, it will decrypt the password using the passphrase in `private_key_passphrase`. If `private_key_passphrase` is empty, the client will prompt you for the master passphrase interactively on startup.

---

#### `adduser` — Add a user to the database

```
net_copy_admin adduser --users USERS.CSV --name NAME [--pass PASS]
                       [--pubkey KEY.pub] [--paths PATH1,PATH2,...]
                       [--methods password,mlkem]
```

| Flag | Required | Description |
|------|----------|-------------|
| `--users` | **Yes** | Path to `users.csv` file (created if absent) |
| `--name` | **Yes** | Username |
| `--pass` | No | Password (prompted if `--methods` includes `password` and flag omitted; use `-` to force prompt) |
| `--pubkey` | No | Path to `.pub` public key file for ML-KEM auth |
| `--paths` | No | Comma-separated allowed paths, or `*` for all (default: `*`) |
| `--methods` | No | Comma-separated auth methods: `password`, `mlkem` (default: `password`) |

```powershell
# Add user with password only, restricted to one path
.\net_copy_admin.exe adduser --users users.csv --name alice --pass "S3cr3t!" --paths "D:\Work\FILES"

# Add user with ML-KEM key only, full access
.\net_copy_admin.exe adduser --users users.csv --name bob --pubkey bob.pub --paths "*" --methods mlkem

# Add user with both methods, multiple paths
.\net_copy_admin.exe adduser --users users.csv --name admin --pass "AdminPass!" --pubkey admin.pub ^
    --paths "D:\Work\FILES,D:\Backup" --methods password,mlkem

# Add user, prompt for password interactively (no --pass flag)
.\net_copy_admin.exe adduser --users users.csv --name carol --paths "D:\Reports"
```

---

#### `passwd` — Change a user's password

```
net_copy_admin passwd --users USERS.CSV --name NAME [--pass NEWPASS]
```

| Flag | Required | Description |
|------|----------|-------------|
| `--users` | **Yes** | Path to `users.csv` |
| `--name` | **Yes** | Username to update |
| `--pass` | No | New password (prompted interactively if omitted) |

A new random salt is generated automatically. The old password hash is never reused.

```powershell
# Interactive (recommended — password not visible in shell history)
.\net_copy_admin.exe passwd --users users.csv --name alice

# Scripted
.\net_copy_admin.exe passwd --users users.csv --name alice --pass "N3wP@ss!"
```

---

#### `setkey` — Update a user's ML-KEM public key

```
net_copy_admin setkey --users USERS.CSV --name NAME --pubkey KEY.pub
```

| Flag | Required | Description |
|------|----------|-------------|
| `--users` | **Yes** | Path to `users.csv` |
| `--name` | **Yes** | Username to update |
| `--pubkey` | **Yes** | Path to new `.pub` public key file |

Use this when a user regenerates their key pair (e.g. key rotation).

```powershell
.\net_copy_admin.exe setkey --users users.csv --name alice --pubkey alice_new.pub
```

---

#### `deluser` — Remove a user

```
net_copy_admin deluser --users USERS.CSV --name NAME
```

| Flag | Required | Description |
|------|----------|-------------|
| `--users` | **Yes** | Path to `users.csv` |
| `--name` | **Yes** | Username to remove |

```powershell
.\net_copy_admin.exe deluser --users users.csv --name alice
```

---

#### `listusers` — List all users

```
net_copy_admin listusers --users USERS.CSV
```

| Flag | Required | Description |
|------|----------|-------------|
| `--users` | **Yes** | Path to `users.csv` |

```powershell
.\net_copy_admin.exe listusers --users users.csv
```

Example output:
```
Users in users.csv (3 total):
----------------------------------------------------------------------
  User:    alice
  Methods: password
  Paths:   D:\Work\FILES
----------------------------------------------------------------------
  User:    bob
  Methods: mlkem
  Paths:   *
  ML-KEM:  ML-KEM-768 (1184 bytes)
----------------------------------------------------------------------
  User:    admin
  Methods: password,mlkem
  Paths:   D:\Work\FILES,D:\Backup
  ML-KEM:  ML-KEM-1024 (1568 bytes)
----------------------------------------------------------------------
```

---

#### `verify` — Verify user credentials against a remote server

```
net_copy_admin verify --host HOST --name NAME [--pass PASS] [--key KEY.pem] [--passphrase PASS]
```

Tests a user's credentials by performing a handshake and authenticating against the remote server. No files are transferred.

| Flag | Required | Description |
|------|----------|-------------|
| `--host` | **Yes** | Server address to connect to (e.g., `127.0.0.1:1245` or `192.168.1.100`) |
| `--name` | **Yes** | Username to verify |
| `--pass` | No | Password for password auth (prompts interactively if using password auth and flag is omitted) |
| `--key` | No | Path to ML-KEM private key file for key auth |
| `--passphrase` | No | Passphrase if ML-KEM key is encrypted |

```powershell
# Verify password auth interactively
.\net_copy_admin.exe verify --host 127.0.0.1:1245 --name alice

# Verify password auth inline
.\net_copy_admin.exe verify --host 127.0.0.1:1245 --name alice --pass "S3cr3t!"

# Verify ML-KEM key auth
.\net_copy_admin.exe verify --host 127.0.0.1:1245 --name bob --key bob.pem --passphrase "KeyPass"
```

---

#### `ls` — List remote directory contents on the server

```
net_copy_admin ls --host HOST --name NAME [--pass PASS] [--key KEY.pem] [--passphrase PASS] --remote PATH [--recursive]
```

Lists directories or files on the remote server. Relies on the same authentication options as `verify`.

| Flag | Required | Description |
|------|----------|-------------|
| `--host` | **Yes** | Server address (e.g., `127.0.0.1:1245`) |
| `--name` | **Yes** | Username |
| `--remote` | **Yes** | Remote path to list on the server (e.g., `/` or `D:/Work/FILES`) |
| `--recursive` | No | Recurse into subdirectories when listing |
| Other flags | No | `--pass`, `--key`, and `--passphrase` work exactly as in `verify` |

```powershell
# List remote allowed root directory
.\net_copy_admin.exe ls --host 127.0.0.1:1245 --name alice --remote "D:/Work/FILES"

# List recursively using ML-KEM key
.\net_copy_admin.exe ls --host 127.0.0.1:1245 --name bob --key bob.pem --remote "D:/Work" --recursive
```

---

### Password Authentication

**Server setup:**

```powershell
# 1. Add a user to users.csv
.\net_copy_admin.exe adduser --users users.csv --name alice --pass "S3cr3t!" --paths "D:\Work\FILES"

# 2. Verify the user was added
.\net_copy_admin.exe listusers --users users.csv

# 3. Start the server (server.conf must have users_file = users.csv)
.\net_copy_server.exe
```

**client.conf for password auth:**

```ini
[auth]
username = alice
auth_method = password
password = S3cr3t!
```

**Transfer:**

```powershell
.\net_copy_client.exe bigfile.bin 127.0.0.1:D:\Work\FILES\
```

---

### ML-KEM Key Authentication

**One-time setup:**

```powershell
# 1. Generate key pair on the client machine
.\net_copy_admin.exe keygen --level 768 --encrypt --passphrase "KeyPass" --out alice

# 2. Send alice.pub to the server admin

# 3. Server admin adds alice with her public key
.\net_copy_admin.exe adduser --users users.csv --name alice --pubkey alice.pub --paths "*" --methods mlkem

# 4. Start the server
.\net_copy_server.exe
```

**client.conf for ML-KEM auth:**

```ini
[auth]
username = alice
auth_method = mlkem
private_key_file = alice.pem
private_key_passphrase = KeyPass
```

**Transfer:**

```powershell
.\net_copy_client.exe bigfile.bin 192.168.1.100:D:\Work\FILES\
```

---

## Encryption Modes

Select with `-s` / `--security` on the client, or negotiated during handshake.

| Level | Flag | Algorithm | Speed | Security |
|-------|------|-----------|-------|---------|
| `high` | `-s high` | ChaCha20-Poly1305 | Fast | ★★★★★ (default) |
| `aes` | `-s aes` | AES-128-CTR + AES-NI | Fastest on AES-NI CPUs | ★★★★ |
| `AES-256-GCM` | `-s AES-256-GCM` | AES-256-GCM (GPU accel) | Fast with NVIDIA GPU | ★★★★★ |
| `fast` | `-s fast` | XOR cipher | Maximum speed | ★ (not for production) |

```powershell
# Use AES hardware acceleration (ideal for local network transfers)
.\net_copy_client.exe -s aes largefile.bin 127.0.0.1:D:\Work\

# Use ChaCha20-Poly1305 (default — safe choice for all environments)
.\net_copy_client.exe -s high largefile.bin 192.168.1.100:D:\Work\

# Use AES-256-GCM with GPU acceleration
.\net_copy_client.exe -s AES-256-GCM largefile.bin 192.168.1.100:D:\Work\
```

---

## Examples & Use Cases

### Copy a single file to a remote server

```powershell
.\net_copy_client.exe C:\Reports\Q4_2025.xlsx 192.168.1.50:D:\Shared\Reports\
```

### Copy a file using a specific port

```powershell
.\net_copy_client.exe -p 9000 C:\Reports\Q4_2025.xlsx 192.168.1.50:D:\Shared\Reports\
```

### Recursively back up a directory

```powershell
.\net_copy_client.exe -R C:\Users\Me\Documents\ 192.168.1.50:D:\Backup\Documents\
```

### Resume a large interrupted transfer

```powershell
# Start transfer
.\net_copy_client.exe C:\ISO\ubuntu-24.04.iso 192.168.1.50:D:\Downloads\

# If interrupted, resume — already-received bytes are skipped
.\net_copy_client.exe --resume C:\ISO\ubuntu-24.04.iso 192.168.1.50:D:\Downloads\
```

### Transfer to a Linux server

```powershell
# Linux server is running net_copy_server at 10.0.0.5:1245
# Linux server config allows /data/uploads
.\net_copy_client.exe dataset.tar.gz 10.0.0.5:/data/uploads/
```

### Use a custom config file

```powershell
# Useful when connecting to multiple servers with different keys / users
.\net_copy_client.exe -c C:\config\prod_server.conf bigfile.bin 10.0.0.5:/data/
```

### Transfer without a config file (prompts for password)

```powershell
# If no secret_key is in client.conf, the client prompts for a master password
.\net_copy_client.exe file.txt 127.0.0.1:D:\Shared\
# Enter master password: ████████
```

### Run server in verbose mode to diagnose issues

```powershell
.\net_copy_server.exe -v -l 127.0.0.1:1245 -a "D:\Work"
# Shows every handshake, auth step, chunk received, ACK sent
```

### Transfer with bandwidth cap

Set in `client.conf`:
```ini
[performance]
max_bandwidth_percent = 50   # use at most 50% of link speed
```
Then transfer normally:
```powershell
.\net_copy_client.exe bigfile.bin 192.168.1.50:D:\Work\
```

### Download / Pull mode

To pull files or directories from a remote server, pass the `-g`/`--get`/`--download` flag. The first positional argument is the remote source, and the second is the local destination.

```powershell
# Download a single file from the server
.\net_copy_client.exe --get 192.168.1.50:D:\Backup\database.sql C:\LocalBackup\

# Download a directory recursively from the server
.\net_copy_client.exe --get -R 192.168.1.50:D:\Shared\images C:\LocalImages\
```

### List files remotely on the server

To view directories and files on the remote server without performing a transfer:

```powershell
.\net_copy_admin.exe ls --host 192.168.1.50:1245 --name alice --remote "D:/Shared"
```

### Verify client authentication credentials

To test if connection parameters and user credentials (password or ML-KEM keys) are valid:

```powershell
.\net_copy_admin.exe verify --host 192.168.1.50:1245 --name alice
```

### Minimal server — no auth, localhost only (dev/testing)

`server.conf`:
```ini
[network]
listen_address = 127.0.0.1
listen_port = 1245

[security]
secret_key = 0xebf5fa7d3e9fcf67e874baddee773b1d5badd6eb75baddee974b2592c9643299
require_auth = false

[paths]
allowed_paths = C:\Temp

[auth]
allow_anonymous = true
```

```powershell
.\net_copy_server.exe
.\net_copy_client.exe testfile.txt 127.0.0.1:C:\Temp\
```

### Production server — users.csv + ML-KEM auth

`server.conf`:
```ini
[network]
listen_address = 0.0.0.0
listen_port = 1245

[security]
secret_key = 0x<your-key-here>
require_auth = true

[logging]
log_level = INFO
log_file = server.log
console_output = false

[paths]
allowed_paths = D:\Data\uploads

[auth]
users_file = users.csv
allow_anonymous = false
```

```powershell
# One-time: add a user
.\net_copy_admin.exe adduser --users users.csv --name deploy_bot --pubkey deploy_bot.pub --paths "D:\Data\uploads" --methods mlkem

# Start server as a Windows service
net start NetCopyServer

# Client transfers using ML-KEM key (no password ever typed)
.\net_copy_client.exe -c deploy.conf build_artifact.zip 10.0.0.5:D:\Data\uploads\
```

`deploy.conf` (on the CI machine):
```ini
[security]
secret_key = 0x<shared-key>

[auth]
username = deploy_bot
auth_method = mlkem
private_key_file = C:\CI\keys\deploy_bot.pem
private_key_passphrase = 
```

---

## users.csv Format

Managed by `net_copy_admin` — you normally don't edit this by hand.

```
# net_copy user database
# Fields: username;auth_methods;pbkdf2_hash_hex;salt_hex;iterations;mlkem_level;mlkem_pubkey_b64;allowed_paths
alice;password,mlkem;a3f9...;deadbeef...;200000;ML-KEM-768;AAAB...;D:\Work\FILES
readonly;password;b4e1...;cafebabe...;200000;;;D:\Reports
svc;mlkem;;;0;ML-KEM-512;CCCD...;*
```

| Field | Description |
|-------|-------------|
| `username` | Login name |
| `auth_methods` | Comma-separated: `password`, `mlkem` |
| `pbkdf2_hash_hex` | PBKDF2-SHA3-256(password, salt, iterations, 32) as hex |
| `salt_hex` | 16-byte random salt as hex |
| `iterations` | PBKDF2 iteration count (default 200,000) |
| `mlkem_level` | `ML-KEM-512`, `ML-KEM-768`, or `ML-KEM-1024` |
| `mlkem_pubkey_b64` | Base64-encoded ML-KEM public key |
| `allowed_paths` | Comma-separated paths; `*` allows everything |
