# NetCopy

**NetCopy** is a fast, encrypted, resumable file transfer tool — similar in spirit to `scp` or `rsync`, but built around a persistent server daemon and post-quantum-ready cryptography.

---

## Table of Contents

- [Quick Start (copy-paste this)](#quick-start)
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

```ini
[network]
# IP address to listen on. 0.0.0.0 = all interfaces, 127.0.0.1 = localhost only.
listen_address = 0.0.0.0
# TCP port to listen on. Default: 1245.
listen_port = 1245
# Maximum simultaneous client connections.
max_connections = 10
# Seconds before an idle connection is dropped.
timeout = 30

[security]
# Pre-shared hex key for transport encryption (legacy / fallback mode).
# Generate with: net_copy_admin genkey
# If users.csv is present and require_auth=true, user auth replaces this
# for identity verification but the key is still used for transport encryption.
secret_key = 0xebf5fa7d...
# Require authentication from every client. Set false only for trusted LANs.
require_auth = true
# Maximum file size the server will accept, in bytes. 0 = unlimited.
# 1073741824 = 1 GB
max_file_size = 0

[performance]
# Bandwidth cap as % of detected link speed. 100 = unlimited.
# Values 1-99 throttle the upload; 0 or 100 = no cap.
max_bandwidth_percent = 100
# Maximum chunk size in bytes the server will negotiate.
# Clients use smaller chunks unless they grow via AIMD.
# 910485760 = 868 MB (effectively unlimited — client caps at 32 MB anyway)
max_chunk_size = 910485760

[logging]
# Log verbosity: DEBUG, INFO, WARNING, ERROR
log_level = INFO
# Path to log file. Leave empty to disable file logging.
log_file = server.log
# Print log lines to stdout (useful when not running as daemon).
console_output = true

[daemon]
# Start as a background daemon on launch (Linux only).
run_as_daemon = false
# PID file for daemon mode.
pid_file = /var/run/net_copy_server.pid

[transfer]
# Automatically create missing parent directories when a client writes a file.
auto_create_directories = true
# When transferring directories, also replicate empty subdirectories.
create_empty_directories = true

[paths]
# One or more directories the server allows clients to write to.
# Multiple entries are supported; use one line per path.
# Clients cannot write outside these directories.
allowed_paths = D:\Work\FILES
allowed_paths = D:\Work\Backup

[auth]
# Path to the user database CSV (relative to server executable, or absolute).
users_file = users.csv
# Allow connections with no username when users.csv is present.
# Set true for a mixed setup: some clients use auth, others don't.
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
  -R, --recursive            Transfer a directory and all its contents
  --resume                   Resume an interrupted transfer (skip already-received bytes)
  --auto-create              Create missing destination directories (default)
  --no-auto-create           Fail if destination directory doesn't exist
  --no-empty-dirs            Skip empty subdirectories during recursive transfer
  -s, --security LEVEL       Encryption: high (default) | fast | aes | AES-256-GCM
  -v, --verbose              Show debug info and live connection details
  -h, --help                 Show help
```

### Destination Format

```
<server>:<port>/<remote-path>   →   127.0.0.1:1245/D:/Work/
<server>:<remote-path>          →   127.0.0.1:D:\Work\          (port from config/default)
<server>:/remote-path           →   192.168.1.5:/mnt/data/      (absolute Unix path)
<server>                        →   192.168.1.5                  (default port, root path)
```

> On Windows, both `\` and `/` work in remote paths. The client normalises them automatically.

### client.conf Reference

```ini
[security]
# Must match the server's secret_key (transport encryption key).
# Generate a key pair with: net_copy_admin genkey
secret_key = 0xebf5fa7d...

[performance]
# Upload bandwidth cap as % of detected link speed.
# 100 = unlimited. Values 1-99 throttle. 0 = also unlimited.
max_bandwidth_percent = 100
# Retry on transient errors.
retry_attempts = 3
retry_delay = 5            # seconds between retries

# Chunk size tuning (AIMD — adapts automatically during transfer)
initial_chunk_size = 262144    # 256 KB starting chunk size
min_chunk_size = 8192          # 8 KB floor
max_chunk_size = 910485760     # 868 MB ceiling (capped at 32 MB in practice)
chunk_size_increase_factor = 1.1   # multiply chunk size after each successful ACK
chunk_size_decrease_factor = 0.5   # halve chunk size after an error

[logging]
log_level = DEBUG     # DEBUG | INFO | WARNING | ERROR
log_file = client.log
console_output = true

[connection]
# Seconds to wait for the initial connection and handshake.
# Removed after handshake — data transfers never time out.
timeout = 30
keep_alive = true

[transfer]
auto_create_directories = true
create_empty_directories = true

[auth]
# Uncomment and fill in to enable user authentication.
# username = alice
# auth_method = password          # password | mlkem | none
# password = MySecret123          # for auth_method = password
# private_key_file = id_mlkem768.pem   # for auth_method = mlkem
# private_key_passphrase =        # leave empty if key file is not encrypted
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
