# NetCopy User Manual

## Overview

NetCopy is a secure file transfer application that provides encrypted communication between client and server using ChaCha20-Poly1305 encryption. It supports file and directory transfers with resume capability and cross-platform operation on Windows and Linux.

## Features

- **Secure Communication**: ChaCha20-Poly1305 AEAD encryption
- **Resume Capability**: Interrupted transfers can be resumed
- **Cross-Platform**: Works on Windows 10+ and Linux (CentOS 7+)
- **Daemon Mode**: Server can run as background service
- **Configurable**: Granular configuration options
- **Bandwidth Control**: Configurable bandwidth usage limits
- **Logging**: Comprehensive logging system

## Installation

### Prerequisites

- **Linux**: GCC 7+ or compatible compiler
- **Windows**: GCC (MSYS2) or Visual Studio 2019+
- **CMake**: Version 3.12 or higher

### Building from Source

1. Clone or extract the source code
2. Create build directory:
   ```bash
   mkdir build && cd build
   ```
3. Configure with CMake:
   ```bash
   cmake ..
   ```
4. Build:
   ```bash
   make -j$(nproc)  # Linux
   cmake --build . --config Release  # Windows
   ```

### Installation

```bash
sudo make install  # Linux
cmake --install . --config Release  # Windows
```

## Configuration

### Server Configuration

Create `/etc/net_copy/server.conf` (Linux) or `%APPDATA%\NetCopy\server.conf` (Windows):

```ini
[network]
listen_address = 0.0.0.0
listen_port = 1245
max_connections = 10
timeout = 30

[security]
secret_key = 0x1234567890abcdef...
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

[paths]
base_directory = /var/lib/net_copy
allowed_paths = /home,/tmp
```

### Client Configuration

Create `~/.config/netcopy/client.conf` (Linux) or `%APPDATA%\NetCopy\client.conf` (Windows):

```ini
[security]
secret_key = 0x1234567890abcdef...

[performance]
buffer_size = 65536
max_bandwidth_percent = 40
retry_attempts = 3
retry_delay = 5

[logging]
log_level = INFO
log_file = client.log
console_output = true

[connection]
timeout = 30
keep_alive = true
```

## Usage

### Key Generation

First, generate a shared encryption key:

```bash
net_copy_keygen -genkey
```

Enter a master password when prompted. Copy the generated key to both client and server configuration files.

### Server Usage

Start the server:

```bash
# Basic usage
net_copy_server

# Specify listen address and access path
net_copy_server -l 192.168.1.100:1245 -a "/home/shared"

# Run as daemon
net_copy_server --daemon --config /etc/net_copy/server.conf

# Verbose logging
net_copy_server --verbose
```

### Client Usage

Transfer files:

```bash
# Transfer single file
net_copy file.txt 192.168.1.100:/remote/path/

# Transfer directory recursively
net_copy ./folder/ 192.168.1.100:/remote/path/ -R

# Resume interrupted transfer
net_copy file.txt 192.168.1.100:/remote/path/ --resume

# Use custom configuration
net_copy file.txt 192.168.1.100:/remote/path/ --config client.conf
```

## Command Line Options

### Server Options

- `-l, --listen ADDRESS:PORT` - Listen address and port
- `-a, --access PATH` - Base directory for file access
- `-c, --config FILE` - Configuration file path
- `-d, --daemon` - Run as daemon
- `-v, --verbose` - Enable verbose logging
- `-h, --help` - Show help message

### Client Options

- `-R, --recursive` - Transfer directories recursively
- `--resume` - Resume interrupted transfer
- `-c, --config FILE` - Configuration file path
- `-v, --verbose` - Enable verbose logging
- `-h, --help` - Show help message

## Security Considerations

1. **Key Management**: Store encryption keys securely and use strong master passwords
2. **Network Security**: Use secure networks and consider additional VPN protection
3. **File Permissions**: Ensure proper file system permissions on server
4. **Access Control**: Configure allowed paths carefully to prevent unauthorized access

## Troubleshooting

### Common Issues

1. **Connection Refused**
   - Check if server is running
   - Verify firewall settings
   - Confirm correct IP address and port

2. **Authentication Failed**
   - Verify secret key matches between client and server
   - Check master password if prompted

3. **Permission Denied**
   - Ensure server has write permissions to destination
   - Check if path is in allowed_paths configuration

4. **Transfer Interrupted**
   - Use `--resume` flag to continue transfer
   - Check network connectivity
   - Verify available disk space

### Log Files

Check log files for detailed error information:
- Server: `server.log` (or configured log file)
- Client: `client.log` (or configured log file)

### Debug Mode

Enable verbose logging for troubleshooting:
```bash
net_copy_server --verbose
net_copy file.txt server:/path/ --verbose
```

## Performance Tuning

### Bandwidth Control

Limit bandwidth usage to prevent network congestion:
```ini
[performance]
max_bandwidth_percent = 40  # Use 40% of available bandwidth
```

### Buffer Size

Adjust buffer size for optimal performance:
```ini
[performance]
buffer_size = 65536  # 64KB buffer (default)
```

### Thread Pool

Configure server thread pool size:
```ini
[performance]
thread_pool_size = 4  # Number of worker threads
```

## Examples

### Basic File Transfer

```bash
# Start server
net_copy_server -l 192.168.1.100:1245 -a "/home/shared"

# Transfer file from client
net_copy document.pdf 192.168.1.100:/home/shared/
```

### Directory Synchronization

```bash
# Transfer entire directory structure
net_copy ./project/ 192.168.1.100:/backup/project/ -R
```

### Resume Large Transfer

```bash
# Start transfer
net_copy large_file.zip 192.168.1.100:/downloads/

# If interrupted, resume with:
net_copy large_file.zip 192.168.1.100:/downloads/ --resume
```

## Support

For technical support and bug reports, please refer to the project documentation or contact the development team.

