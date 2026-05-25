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
    ERROR_MESSAGE = 12,
    AUTH_CHALLENGE = 13,
    AUTH_RESPONSE = 14,
    AUTH_RESULT = 15,
    DOWNLOAD_REQUEST = 16,
    DOWNLOAD_RESPONSE = 17,
    LIST_REQUEST = 18,
    LIST_RESPONSE = 19,
    FILE_VERIFY_REQUEST = 20,
    FILE_VERIFY_RESPONSE = 21,
    DISCONNECT = 22,
    BLOCK_HASHES_REQUEST = 23,
    BLOCK_HASHES_RESPONSE = 24,
    TRANSFER_STATUS_REQUEST = 25,
    TRANSFER_STATUS_RESPONSE = 26
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
    // Auth fields (appended last for backward compatibility)
    std::string username;       // empty = anonymous
    uint8_t auth_method_id;     // 0=none, 1=password, 2=mlkem
    
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
    
    // Metadata fields
    uint32_t permissions = 0;
    bool is_symlink = false;
    std::string symlink_target;
    uint64_t file_size = 0;
    
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
    std::string session_id;
    
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

class AuthChallenge : public Message {
public:
    AuthChallenge();
    uint8_t method;                         // 1=password, 2=mlkem
    std::vector<uint8_t> challenge_nonce;   // 32 bytes
    std::string salt_hex;                   // for password auth
    int pbkdf2_iterations;                  // for password auth
    std::vector<uint8_t> kem_ciphertext;    // for ML-KEM auth
    std::string mlkem_level_str;            // "ML-KEM-768" etc, for ML-KEM auth
    std::vector<uint8_t> kem_nonce;         // 32 bytes, for ML-KEM auth
private:
    std::vector<uint8_t> serialize_payload() const override;
    void deserialize_payload(const std::vector<uint8_t>& data) override;
};

class AuthResponse : public Message {
public:
    AuthResponse();
    std::vector<uint8_t> proof;  // 32-byte SHA3-256 proof
private:
    std::vector<uint8_t> serialize_payload() const override;
    void deserialize_payload(const std::vector<uint8_t>& data) override;
};

class AuthResult : public Message {
public:
    AuthResult();
    bool success;
    std::string error_message;
private:
    std::vector<uint8_t> serialize_payload() const override;
    void deserialize_payload(const std::vector<uint8_t>& data) override;
};

struct RemoteFileInfo {
    std::string path;
    uint64_t size;
    bool is_directory;
    uint64_t last_modified;
    
    // Metadata fields
    uint32_t permissions = 0;
    bool is_symlink = false;
    std::string symlink_target;
};

class DownloadRequest : public Message {
public:
    DownloadRequest();
    
    std::string remote_path;
    uint64_t resume_offset = 0;
    
    std::vector<uint8_t> serialize_payload() const override;
    void deserialize_payload(const std::vector<uint8_t>& data) override;
};

class DownloadResponse : public Message {
public:
    DownloadResponse();
    
    bool success;
    std::string error_message;
    uint64_t file_size;
    bool is_directory;
    
    // Metadata fields
    uint32_t permissions = 0;
    bool is_symlink = false;
    std::string symlink_target;
    std::string session_id;
    
    std::vector<uint8_t> serialize_payload() const override;
    void deserialize_payload(const std::vector<uint8_t>& data) override;
};

class ListRequest : public Message {
public:
    ListRequest();
    
    std::string remote_path;
    bool recursive;
    
    std::vector<uint8_t> serialize_payload() const override;
    void deserialize_payload(const std::vector<uint8_t>& data) override;
};

class ListResponse : public Message {
public:
    ListResponse();
    
    bool success;
    std::string error_message;
    std::vector<RemoteFileInfo> entries;
    
    std::vector<uint8_t> serialize_payload() const override;
    void deserialize_payload(const std::vector<uint8_t>& data) override;
};

class Disconnect : public Message {
public:
    Disconnect();
    
    std::vector<uint8_t> serialize_payload() const override;
    void deserialize_payload(const std::vector<uint8_t>& data) override;
};

class FileVerifyRequest : public Message {
public:
    FileVerifyRequest();
    
    std::string file_path;
    std::vector<uint8_t> expected_hash;
    
    std::vector<uint8_t> serialize_payload() const override;
    void deserialize_payload(const std::vector<uint8_t>& data) override;
};

class FileVerifyResponse : public Message {
public:
    FileVerifyResponse();
    
    bool success;
    std::string error_message;
    std::vector<uint8_t> actual_hash;
    
    std::vector<uint8_t> serialize_payload() const override;
    void deserialize_payload(const std::vector<uint8_t>& data) override;
};

class BlockHashesRequest : public Message {
public:
    BlockHashesRequest();
    
    std::string file_path;
    uint64_t block_size = 65536; // Default 64KB
    
    std::vector<uint8_t> serialize_payload() const override;
    void deserialize_payload(const std::vector<uint8_t>& data) override;
};

struct BlockHashInfo {
    uint64_t offset;
    std::vector<uint8_t> hash;
};

class BlockHashesResponse : public Message {
public:
    BlockHashesResponse();
    
    bool success;
    std::string error_message;
    uint64_t block_size;
    std::vector<BlockHashInfo> blocks;
    
    std::vector<uint8_t> serialize_payload() const override;
    void deserialize_payload(const std::vector<uint8_t>& data) override;
};

class TransferStatusRequest : public Message {
public:
    TransferStatusRequest();
    
    std::string session_id;
    
    std::vector<uint8_t> serialize_payload() const override;
    void deserialize_payload(const std::vector<uint8_t>& data) override;
};

class TransferStatusResponse : public Message {
public:
    TransferStatusResponse();
    
    bool success;
    std::string error_message;
    bool active;
    uint64_t bytes_transferred;
    uint64_t total_bytes;
    std::string status_string;
    std::string logs;
    
    std::vector<uint8_t> serialize_payload() const override;
    void deserialize_payload(const std::vector<uint8_t>& data) override;
};

} // namespace protocol
} // namespace netcopy

