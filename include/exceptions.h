#pragma once

#include <stdexcept>
#include <string>

namespace netcopy {

// Base exception class
class NetCopyException : public std::exception {
public:
    explicit NetCopyException(const std::string& message) : message_(message) {}
    const char* what() const noexcept override { return message_.c_str(); }

private:
    std::string message_;
};

// Network-related exceptions
class NetworkException : public NetCopyException {
public:
    explicit NetworkException(const std::string& message) 
        : NetCopyException("Network error: " + message) {}
};

// Cryptography-related exceptions
class CryptoException : public NetCopyException {
public:
    explicit CryptoException(const std::string& message) 
        : NetCopyException("Crypto error: " + message) {}
};

// File I/O related exceptions
class FileException : public NetCopyException {
public:
    explicit FileException(const std::string& message) 
        : NetCopyException("File error: " + message) {}
};

// Configuration-related exceptions
class ConfigException : public NetCopyException {
public:
    explicit ConfigException(const std::string& message) 
        : NetCopyException("Config error: " + message) {}
};

// Protocol-related exceptions
class ProtocolException : public NetCopyException {
public:
    explicit ProtocolException(const std::string& message) 
        : NetCopyException("Protocol error: " + message) {}
};

} // namespace netcopy

