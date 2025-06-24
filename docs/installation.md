# NetCopy Installation Guide

## System Requirements

### Minimum Requirements

- **Operating System**: 
  - Linux: CentOS 7+ or Ubuntu 18.04+
  - Windows: Windows 10 or later
- **Compiler**: 
  - Linux: GCC 7+ or Clang 6+
  - Windows: GCC (MSYS2) or Visual Studio 2019+
- **CMake**: Version 3.12 or higher
- **Memory**: 512 MB RAM minimum, 1 GB recommended
- **Disk Space**: 100 MB for installation
- **Network**: TCP/IP networking capability

### Recommended Requirements

- **Memory**: 2 GB RAM or more
- **Disk Space**: 1 GB free space for transfers
- **Network**: Gigabit Ethernet for optimal performance

## Installation Methods

### Method 1: Binary Installation (Recommended)

#### Linux (Ubuntu/Debian)

```bash
# Download the package
wget https://releases.netcopy.org/netcopy_1.0.0_amd64.deb

# Install
sudo dpkg -i netcopy_1.0.0_amd64.deb
sudo apt-get install -f  # Fix dependencies if needed
```

#### Linux (CentOS/RHEL)

```bash
# Download the package
wget https://releases.netcopy.org/netcopy-1.0.0-1.x86_64.rpm

# Install
sudo rpm -ivh netcopy-1.0.0-1.x86_64.rpm
```

#### Windows

1. Download `NetCopy-1.0.0-Windows-x64.msi`
2. Run the installer as Administrator
3. Follow the installation wizard
4. Add installation directory to PATH if needed

### Method 2: Source Installation

#### Prerequisites Installation

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install -y build-essential cmake git
```

**CentOS/RHEL:**
```bash
sudo yum groupinstall "Development Tools"
sudo yum install cmake3 git
```

**Windows (MSYS2):**
```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake git
```

#### Build and Install

1. **Download Source Code**
   ```bash
   git clone https://github.com/netcopy/netcopy.git
   cd netcopy
   ```

2. **Create Build Directory**
   ```bash
   mkdir build
   cd build
   ```

3. **Configure Build**
   ```bash
   # Linux
   cmake ..
   
   # Windows (MSYS2)
   cmake -G "MinGW Makefiles" ..
   ```

4. **Compile**
   ```bash
   # Linux
   make -j$(nproc)
   
   # Windows
   cmake --build . --config Release
   ```

5. **Install**
   ```bash
   # Linux
   sudo make install
   
   # Windows (as Administrator)
   cmake --install . --config Release
   ```

## Post-Installation Setup

### 1. Create Configuration Directories

**Linux:**
```bash
sudo mkdir -p /etc/net_copy
sudo mkdir -p /var/lib/net_copy
sudo mkdir -p /var/log/net_copy
```

**Windows:**
```cmd
mkdir "%PROGRAMDATA%\NetCopy"
mkdir "%APPDATA%\NetCopy"
```

### 2. Copy Default Configuration Files

**Linux:**
```bash
sudo cp /usr/share/doc/net_copy/server.conf.default /etc/net_copy/server.conf
cp /usr/share/doc/net_copy/client.conf.default ~/.config/netcopy/client.conf
```

**Windows:**
```cmd
copy "%PROGRAMFILES%\NetCopy\config\server.conf.default" "%PROGRAMDATA%\NetCopy\server.conf"
copy "%PROGRAMFILES%\NetCopy\config\client.conf.default" "%APPDATA%\NetCopy\client.conf"
```

### 3. Generate Encryption Key

```bash
net_copy_keygen -genkey
```

Enter a strong master password when prompted. Copy the generated key to both server and client configuration files.

### 4. Configure Server

Edit the server configuration file:

**Linux:** `/etc/net_copy/server.conf`
**Windows:** `%PROGRAMDATA%\NetCopy\server.conf`

```ini
[security]
secret_key = 0x[generated_key_here]

[paths]
base_directory = /var/lib/net_copy  # Linux
# base_directory = C:\NetCopy\Data  # Windows
```

### 5. Configure Client

Edit the client configuration file:

**Linux:** `~/.config/netcopy/client.conf`
**Windows:** `%APPDATA%\NetCopy\client.conf`

```ini
[security]
secret_key = 0x[same_key_as_server]
```

## Service Installation (Linux)

### SystemD Service

1. **Create Service File**
   ```bash
   sudo tee /etc/systemd/system/net_copy_server.service > /dev/null <<EOF
   [Unit]
   Description=NetCopy Secure File Transfer Server
   After=network.target
   
   [Service]
   Type=forking
   User=netcopy
   Group=netcopy
   ExecStart=/usr/bin/net_copy_server --daemon --config /etc/net_copy/server.conf
   PIDFile=/var/run/net_copy_server.pid
   Restart=always
   RestartSec=5
   
   [Install]
   WantedBy=multi-user.target
   EOF
   ```

2. **Create Service User**
   ```bash
   sudo useradd -r -s /bin/false netcopy
   sudo chown -R netcopy:netcopy /var/lib/net_copy
   sudo chown -R netcopy:netcopy /var/log/net_copy
   ```

3. **Enable and Start Service**
   ```bash
   sudo systemctl daemon-reload
   sudo systemctl enable net_copy_server
   sudo systemctl start net_copy_server
   ```

4. **Check Service Status**
   ```bash
   sudo systemctl status net_copy_server
   ```

## Firewall Configuration

### Linux (iptables)

```bash
# Allow incoming connections on default port
sudo iptables -A INPUT -p tcp --dport 1245 -j ACCEPT

# Save rules (Ubuntu/Debian)
sudo iptables-save > /etc/iptables/rules.v4

# Save rules (CentOS/RHEL)
sudo service iptables save
```

### Linux (firewalld)

```bash
sudo firewall-cmd --permanent --add-port=1245/tcp
sudo firewall-cmd --reload
```

### Windows Firewall

```cmd
# Run as Administrator
netsh advfirewall firewall add rule name="NetCopy Server" dir=in action=allow protocol=TCP localport=1245
```

## Verification

### 1. Test Server Installation

```bash
# Start server in foreground for testing
net_copy_server --verbose

# Should output:
# [INFO] Starting NetCopy server...
# [INFO] Securely listening on TCP port 1245
```

### 2. Test Client Installation

```bash
# Create test file
echo "Hello NetCopy" > test.txt

# Test transfer (replace with actual server IP)
net_copy test.txt 127.0.0.1:/tmp/

# Should output:
# Successfully connected to Network File Transfer Agent on 127.0.0.1
# test.txt successfully transferred to 127.0.0.1:/tmp/
```

### 3. Verify Service (Linux)

```bash
sudo systemctl status net_copy_server
sudo journalctl -u net_copy_server -f
```

## Troubleshooting Installation

### Common Issues

1. **CMake Not Found**
   ```bash
   # Ubuntu/Debian
   sudo apt install cmake
   
   # CentOS 7
   sudo yum install cmake3
   ln -s /usr/bin/cmake3 /usr/local/bin/cmake
   ```

2. **Compiler Errors**
   ```bash
   # Ensure C++17 support
   g++ --version  # Should be 7.0 or higher
   
   # Update if necessary
   sudo apt install gcc-9 g++-9  # Ubuntu
   sudo yum install devtoolset-9  # CentOS
   ```

3. **Permission Denied**
   ```bash
   # Fix ownership
   sudo chown -R netcopy:netcopy /var/lib/net_copy
   
   # Fix permissions
   sudo chmod 755 /var/lib/net_copy
   sudo chmod 644 /etc/net_copy/server.conf
   ```

4. **Port Already in Use**
   ```bash
   # Check what's using the port
   sudo netstat -tlnp | grep 1245
   
   # Change port in configuration
   sudo nano /etc/net_copy/server.conf
   ```

### Log Files

Check log files for detailed error information:
- **Server logs**: `/var/log/net_copy/server.log` (Linux) or `%PROGRAMDATA%\NetCopy\Logs\server.log` (Windows)
- **Client logs**: `~/.config/netcopy/client.log` (Linux) or `%APPDATA%\NetCopy\client.log` (Windows)
- **System logs**: `journalctl -u net_copy_server` (Linux) or Event Viewer (Windows)

## Uninstallation

### Linux (Package)

```bash
# Ubuntu/Debian
sudo apt remove netcopy

# CentOS/RHEL
sudo rpm -e netcopy
```

### Linux (Source)

```bash
# From build directory
sudo make uninstall

# Manual cleanup
sudo rm -rf /etc/net_copy
sudo rm -rf /var/lib/net_copy
sudo userdel netcopy
```

### Windows

1. Use "Add or Remove Programs" in Windows Settings
2. Or run the uninstaller from the installation directory
3. Manually remove configuration files if needed

## Support

For installation issues:
1. Check the troubleshooting section above
2. Review log files for error messages
3. Consult the user manual for configuration details
4. Contact technical support with system details and error logs

