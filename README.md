# NetCopy

NetCopy is a secure client-server application for transferring files or directories over a network. The project builds on Linux and Windows with standard compilers and CMake.

## Features
- **Secure communication** with ChaCha20-Poly1305, AES (CPU AES-NI), AES-256-GCM (GPU) AEAD encryption with pre-shared keys
- **Resume** capability for interrupted transfers
- **Cross-platform** (Linux and Windows)
- **Daemon/service mode** for background operation
- **INI-style configuration** files
- **Bandwidth control** and detailed logging

## Build
On Linux run:
```bash
./build.sh
```
This creates executables in the `build/` directory.

On Windows use:
```cmd (Visual Studio Build System)
build_vs_cli.bat
```
Alternatively, create a `build` directory and run CMake manually.
```cmd
On Windows with MSYS2 MinGW
rmdir /s /q build
mkdir build
cd build
cmake -G "MinGW Makefiles" ..
cmake --build . --config Release
```

## Configuration
Server and client settings are stored in `server.conf` and `client.conf`. See the examples in the `build/` folder and the documentation for full details.

## Key Generation
Generate a shared secret key:
```bash
net_copy_keygen -genkey
```
Enter a master password when prompted and copy the generated key to both configuration files.

## Server Usage
```bash
./net_copy_server
# specify listen address and allowed path
./net_copy_server -l 192.168.1.100:1245 -a "/home/shared"
# run as a daemon using a configuration file
./net_copy_server --daemon --config /etc/net_copy/server.conf
```

```Windows Service
net_copy_service.exe install

Use 'net start NetCopyServer' or 'net_copy_service.exe start' to start the service.
```

## Client Usage
```bash
# transfer a file
./net_copy file.txt 192.168.1.100:/remote/path/
# transfer a directory recursively
./net_copy ./folder/ 192.168.1.100:/remote/path/ -R
# resume an interrupted transfer
./net_copy file.txt 192.168.1.100:/remote/path/ --resume
```

