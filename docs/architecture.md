# NetCopy - Secure File Transfer Application

## Architecture Overview

NetCopy is a client-server application designed for secure file transfer with resume capability. The application uses ChaCha20-Poly1305 encryption for secure communication and supports cross-platform operation on Windows and Linux.

## Module Structure

### Core Modules

1. **Network Module** (`network/`)
   - TCP socket handling
   - Connection management
   - Cross-platform socket abstraction

2. **Crypto Module** (`crypto/`)
   - ChaCha20-Poly1305 implementation
   - Key derivation and management
   - Secure random number generation

3. **File Module** (`file/`)
   - File I/O operations
   - Directory traversal
   - Resume capability implementation

4. **Config Module** (`config/`)
   - Configuration file parsing
   - Default value management
   - Runtime configuration

5. **Protocol Module** (`protocol/`)
   - Message serialization/deserialization
   - Protocol state management
   - Error handling

6. **Daemon Module** (`daemon/`) - Server only
   - Background process management
   - Signal handling
   - Process detachment

7. **Logging Module** (`logging/`)
   - Configurable logging system
   - Thread-safe logging
   - Multiple output targets

## Communication Protocol

### Message Format
```
[Header: 16 bytes] [Encrypted Payload: Variable] [Auth Tag: 16 bytes]
```

### Header Structure
- Message Type (4 bytes)
- Payload Length (4 bytes)
- Sequence Number (4 bytes)
- Reserved (4 bytes)

### Message Types
- HANDSHAKE_REQUEST
- HANDSHAKE_RESPONSE
- FILE_REQUEST
- FILE_RESPONSE
- FILE_DATA
- FILE_ACK
- RESUME_REQUEST
- RESUME_RESPONSE
- ERROR_MESSAGE

## Security Design

### Encryption
- Algorithm: ChaCha20-Poly1305 (AEAD)
- Key Size: 256 bits
- Nonce: 96 bits (incremental)
- Authentication: Poly1305 MAC

### Key Management
- Pre-shared key stored in configuration
- Key derivation using PBKDF2
- Master password for key generation

## Configuration Format

### Server Configuration (server.conf)
```ini
[network]
listen_address = 0.0.0.0
listen_port = 1245
max_connections = 10
timeout = 30

[security]
secret_key = 
require_auth = true
max_file_size = 1073741824

[performance]
buffer_size = 65536
max_bandwidth_percent = 40
thread_pool_size = 4

[logging]
log_level = INFO
log_file = server.log
console_output = true

[daemon]
run_as_daemon = false
pid_file = /var/run/net_copy_server.pid
```

### Client Configuration (client.conf)
```ini
[security]
secret_key = 

[performance]
buffer_size = 65536
max_bandwidth_percent = 40
retry_attempts = 3
retry_delay = 5

[logging]
log_level = INFO
log_file = client.log
console_output = true
```

## Dependencies

### Header-Only Libraries
- **nlohmann/json** - JSON parsing for configuration
- **spdlog** - Logging framework
- **CLI11** - Command line parsing

### System Libraries
- **OpenSSL** (optional) - For PBKDF2 key derivation
- **pthread** (Linux) - Threading support
- **ws2_32** (Windows) - Winsock2 for networking

## Build System

CMake configuration supports:
- Cross-platform compilation
- Dependency management
- Testing framework integration
- Installation targets

## Error Handling

### Exception Hierarchy
```cpp
class NetCopyException : public std::exception
├── NetworkException
├── CryptoException
├── FileException
├── ConfigException
└── ProtocolException
```

## Thread Safety

- All shared data structures protected by mutexes
- Lock-free data structures where appropriate
- Thread-safe logging implementation
- Connection-specific thread pools

