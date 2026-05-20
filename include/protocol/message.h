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
    SYNC_REQUEST = 9,
    SYNC_RESPONSE = 10,
    CONFLICT_RESOLVED = 11,
    ERROR_MESSAGE = 12
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
    uint64_t max_chunk_size;  // Maximum chunk size client can handle
    uint64_t file_size;       // File size for adaptive chunk sizing (0 if not transferring a file)
    uint32_t requested_parallel_streams;
    
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
    uint64_t max_chunk_size;  // Maximum chunk size server can handle
    uint32_t accepted_parallel_streams;
    bool auto_create_directories_allowed;
    
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
    bool auto_create_directories = true;
    bool truncate_destination = false;
    
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
    
    // For batched transfers - multiple chunks in a single message
    struct Chunk {
        uint64_t offset;
        uint64_t uncompressed_size;
        std::vector<uint8_t> data;
        bool is_last_chunk;
        bool compressed;
        
        // Serialization helper for individual chunk
        std::vector<uint8_t> serialize_payload() const;
        void deserialize_payload(const std::vector<uint8_t>& data);
    };
    
    // Batched transfer - multiple chunks in one message
    std::vector<Chunk> chunks;
    
    // Single chunk (for backward compatibility)
    uint64_t offset;
    uint64_t uncompressed_size;
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

class SyncRequest : public Message {
public:
    SyncRequest();
    
    std::string local_path;
    std::string remote_path;
    bool recursive;
    uint32_t sync_interval_seconds;
    
    std::vector<uint8_t> serialize_payload() const override;
    void deserialize_payload(const std::vector<uint8_t>& data) override;
};

class SyncResponse : public Message {
public:
    SyncResponse();
    
    bool success;
    std::string error_message;
    uint64_t sync_id;
    uint32_t file_count;
    
    std::vector<uint8_t> serialize_payload() const override;
    void deserialize_payload(const std::vector<uint8_t>& data) override;
};

class ConflictResolved : public Message {
public:
    ConflictResolved();
    
    std::string file_path;
    std::string resolution_strategy;
    bool resolved_successfully;
    
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

