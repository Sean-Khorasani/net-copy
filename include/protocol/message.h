#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include "crypto/chacha20_poly1305.h"

namespace netcopy {
namespace protocol {

enum class MessageType : uint32_t {
    HANDSHAKE_REQUEST = 1,
    HANDSHAKE_RESPONSE = 2,
    FILE_REQUEST = 3,
    FILE_RESPONSE = 4,
    FILE_DATA = 5,
    FILE_ACK = 6,
    RESUME_REQUEST = 7,
    RESUME_RESPONSE = 8,
    ERROR_MESSAGE = 9
};

struct MessageHeader {
    MessageType type;
    uint32_t payload_length;
    uint32_t sequence_number;
    uint32_t reserved;
    
    static constexpr size_t SIZE = 16;
    
    std::vector<uint8_t> serialize() const;
    static MessageHeader deserialize(const std::vector<uint8_t>& data);
};

class Message {
public:
    Message(MessageType type, uint32_t sequence_number = 0);
    virtual ~Message() = default;

    MessageType get_type() const { return header_.type; }
    uint32_t get_sequence_number() const { return header_.sequence_number; }
    
    virtual std::vector<uint8_t> serialize_payload() const = 0;
    virtual void deserialize_payload(const std::vector<uint8_t>& data) = 0;
    
    std::vector<uint8_t> serialize() const;
    static std::unique_ptr<Message> deserialize(const std::vector<uint8_t>& data);

protected:
    MessageHeader header_;
};

class HandshakeRequest : public Message {
public:
    HandshakeRequest();
    
    std::string client_version;
    std::vector<uint8_t> client_nonce;
    crypto::SecurityLevel security_level = crypto::SecurityLevel::HIGH;
    
    std::vector<uint8_t> serialize_payload() const override;
    void deserialize_payload(const std::vector<uint8_t>& data) override;
};

class HandshakeResponse : public Message {
public:
    HandshakeResponse();
    
    std::string server_version;
    std::vector<uint8_t> server_nonce;
    bool authentication_required;
    crypto::SecurityLevel accepted_security_level = crypto::SecurityLevel::HIGH;
    
    std::vector<uint8_t> serialize_payload() const override;
    void deserialize_payload(const std::vector<uint8_t>& data) override;
};

class FileRequest : public Message {
public:
    FileRequest();
    
    std::string source_path;
    std::string destination_path;
    bool recursive;
    uint64_t resume_offset;
    
    std::vector<uint8_t> serialize_payload() const override;
    void deserialize_payload(const std::vector<uint8_t>& data) override;
};

class FileResponse : public Message {
public:
    FileResponse();
    
    bool success;
    std::string error_message;
    uint64_t file_size;
    uint64_t resume_offset;
    
    std::vector<uint8_t> serialize_payload() const override;
    void deserialize_payload(const std::vector<uint8_t>& data) override;
};

class FileData : public Message {
public:
    FileData();

    uint64_t offset;
    std::vector<uint8_t> data;
    bool is_last_chunk;
    bool compressed;
    
    std::vector<uint8_t> serialize_payload() const override;
    void deserialize_payload(const std::vector<uint8_t>& data) override;
};

class FileAck : public Message {
public:
    FileAck();
    
    uint64_t bytes_received;
    bool success;
    std::string error_message;
    
    std::vector<uint8_t> serialize_payload() const override;
    void deserialize_payload(const std::vector<uint8_t>& data) override;
};

class ErrorMessage : public Message {
public:
    ErrorMessage();
    
    uint32_t error_code;
    std::string error_description;
    
    std::vector<uint8_t> serialize_payload() const override;
    void deserialize_payload(const std::vector<uint8_t>& data) override;
};

} // namespace protocol
} // namespace netcopy

