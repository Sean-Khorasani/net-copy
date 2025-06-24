# NetCopy

NetCopy is a secure client-server application for transferring files or directories over a network. It uses ChaCha20-Poly1305 authenticated encryption and supports resuming interrupted transfers. The project builds on Linux and Windows with standard compilers and CMake.

## Features
- **Secure communication** with ChaCha20-Poly1305
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
```cmd
build_vs_cli.bat
```
Alternatively, create a `build` directory and run CMake manually.

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
net_copy_server
# specify listen address and allowed path
net_copy_server -l 192.168.1.100:1245 -a "/home/shared"
# run as a daemon using a configuration file
net_copy_server --daemon --config /etc/net_copy/server.conf
```

## Client Usage
```bash
# transfer a file
net_copy file.txt 192.168.1.100:/remote/path/
# transfer a directory recursively
net_copy ./folder/ 192.168.1.100:/remote/path/ -R
# resume an interrupted transfer
net_copy file.txt 192.168.1.100:/remote/path/ --resume
```

## Documentation
Further details are available in the [docs/](docs) directory including installation instructions and an extensive user manual.

