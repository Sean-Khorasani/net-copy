#include "protocol/message.h"
#include "exceptions.h"
#include "common/fast_mem.h"
#include <cstring>
#include <sstream>

namespace netcopy {
namespace protocol {

// Helper functions for serialization
namespace {
    constexpr uint32_t FILE_DATA_MAGIC = 0x3144434E; // "NCD1" little-endian

    void write_uint32(std::vector<uint8_t>& buffer, uint32_t value) {
        buffer.push_back(value & 0xFF);
        buffer.push_back((value >> 8) & 0xFF);
        buffer.push_back((value >> 16) & 0xFF);
        buffer.push_back((value >> 24) & 0xFF);
    }
    
    void write_uint64(std::vector<uint8_t>& buffer, uint64_t value) {
        for (int i = 0; i < 8; ++i) {
            buffer.push_back((value >> (i * 8)) & 0xFF);
        }
    }
    
    void write_string(std::vector<uint8_t>& buffer, const std::string& str) {
        write_uint32(buffer, static_cast<uint32_t>(str.length()));
        buffer.insert(buffer.end(), str.begin(), str.end());
    }
    
    void write_bytes(std::vector<uint8_t>& buffer, const std::vector<uint8_t>& data) {
        write_uint32(buffer, static_cast<uint32_t>(data.size()));
        size_t old_size = buffer.size();
        buffer.resize(old_size + data.size());
        if (!data.empty()) {
            fast_mem::fast_memcpy(buffer.data() + old_size, data.data(), data.size());
        }
    }
    
    uint32_t read_uint32(const std::vector<uint8_t>& buffer, size_t& offset) {
        if (offset + 4 > buffer.size()) {
            throw ProtocolException("Buffer underflow reading uint32");
        }
        uint32_t value = buffer[offset] | (buffer[offset + 1] << 8) | 
                        (buffer[offset + 2] << 16) | (buffer[offset + 3] << 24);
        offset += 4;
        return value;
    }
    
    uint64_t read_uint64(const std::vector<uint8_t>& buffer, size_t& offset) {
        if (offset + 8 > buffer.size()) {
            throw ProtocolException("Buffer underflow reading uint64");
        }
        uint64_t value = 0;
        for (int i = 0; i < 8; ++i) {
            value |= static_cast<uint64_t>(buffer[offset + i]) << (i * 8);
        }
        offset += 8;
        return value;
    }
    
    std::string read_string(const std::vector<uint8_t>& buffer, size_t& offset) {
        uint32_t length = read_uint32(buffer, offset);
        if (offset + length > buffer.size()) {
            throw ProtocolException("Buffer underflow reading string");
        }
        std::string str(buffer.begin() + offset, buffer.begin() + offset + length);
        offset += length;
        return str;
    }
    
    std::vector<uint8_t> read_bytes(const std::vector<uint8_t>& buffer, size_t& offset) {
        uint32_t length = read_uint32(buffer, offset);
        if (offset + length > buffer.size()) {
            throw ProtocolException("Buffer underflow reading bytes");
        }
        std::vector<uint8_t> data(length);
        if (length > 0) {
            fast_mem::fast_memcpy(data.data(), buffer.data() + offset, length);
        }
        offset += length;
        return data;
    }
}

// MessageHeader implementation
std::vector<uint8_t> MessageHeader::serialize() const {
    std::vector<uint8_t> buffer;
    write_uint32(buffer, static_cast<uint32_t>(type));
    write_uint32(buffer, payload_length);
    write_uint32(buffer, sequence_number);
    write_uint32(buffer, reserved);
    return buffer;
}

MessageHeader MessageHeader::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < SIZE) {
        throw ProtocolException("Invalid header size");
    }
    
    size_t offset = 0;
    MessageHeader header;
    header.type = static_cast<MessageType>(read_uint32(data, offset));
    header.payload_length = read_uint32(data, offset);
    header.sequence_number = read_uint32(data, offset);
    header.reserved = read_uint32(data, offset);
    
    return header;
}

// Message base class implementation
Message::Message(MessageType type, uint32_t sequence_number) {
    header_.type = type;
    header_.sequence_number = sequence_number;
    header_.reserved = 0;
}

std::vector<uint8_t> Message::serialize() const {
    auto payload = serialize_payload();
    MessageHeader header = header_;
    header.payload_length = static_cast<uint32_t>(payload.size());
    
    auto header_data = header.serialize();
    std::vector<uint8_t> buffer(header_data.size() + payload.size());
    if (!header_data.empty()) {
        fast_mem::fast_memcpy(buffer.data(), header_data.data(), header_data.size());
    }
    if (!payload.empty()) {
        fast_mem::fast_memcpy(buffer.data() + header_data.size(), payload.data(), payload.size());
    }
    
    return buffer;
}

std::unique_ptr<Message> Message::deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < MessageHeader::SIZE) {
        throw ProtocolException("Message too short");
    }
    
    auto header = MessageHeader::deserialize(data);
    if (data.size() < MessageHeader::SIZE + header.payload_length) {
        throw ProtocolException("Incomplete message");
    }
    
    std::vector<uint8_t> payload(data.begin() + MessageHeader::SIZE, 
                                data.begin() + MessageHeader::SIZE + header.payload_length);
    
    std::unique_ptr<Message> message;
    
    switch (header.type) {
        case MessageType::HANDSHAKE_REQUEST:
            message = std::make_unique<HandshakeRequest>();
            break;
        case MessageType::HANDSHAKE_RESPONSE:
            message = std::make_unique<HandshakeResponse>();
            break;
        case MessageType::FILE_REQUEST:
            message = std::make_unique<FileRequest>();
            break;
        case MessageType::FILE_RESPONSE:
            message = std::make_unique<FileResponse>();
            break;
        case MessageType::FILE_DATA:
            message = std::make_unique<FileData>();
            break;
        case MessageType::FILE_ACK:
            message = std::make_unique<FileAck>();
            break;
        case MessageType::ERROR_MESSAGE:
            message = std::make_unique<ErrorMessage>();
            break;
        case MessageType::AUTH_CHALLENGE:
            message = std::make_unique<AuthChallenge>();
            break;
        case MessageType::AUTH_RESPONSE:
            message = std::make_unique<AuthResponse>();
            break;
        case MessageType::AUTH_RESULT:
            message = std::make_unique<AuthResult>();
            break;
        case MessageType::DOWNLOAD_REQUEST:
            message = std::make_unique<DownloadRequest>();
            break;
        case MessageType::DOWNLOAD_RESPONSE:
            message = std::make_unique<DownloadResponse>();
            break;
        case MessageType::LIST_REQUEST:
            message = std::make_unique<ListRequest>();
            break;
        case MessageType::LIST_RESPONSE:
            message = std::make_unique<ListResponse>();
            break;
        case MessageType::DISCONNECT:
            message = std::make_unique<Disconnect>();
            break;
        case MessageType::FILE_VERIFY_REQUEST:
            message = std::make_unique<FileVerifyRequest>();
            break;
        case MessageType::FILE_VERIFY_RESPONSE:
            message = std::make_unique<FileVerifyResponse>();
            break;
        case MessageType::BLOCK_HASHES_REQUEST:
            message = std::make_unique<BlockHashesRequest>();
            break;
        case MessageType::BLOCK_HASHES_RESPONSE:
            message = std::make_unique<BlockHashesResponse>();
            break;
        case MessageType::TRANSFER_STATUS_REQUEST:
            message = std::make_unique<TransferStatusRequest>();
            break;
        case MessageType::TRANSFER_STATUS_RESPONSE:
            message = std::make_unique<TransferStatusResponse>();
            break;
        default:
            throw ProtocolException("Unknown message type");
    }
    
    message->header_ = header;
    message->deserialize_payload(payload);
    
    return message;
}

// HandshakeRequest implementation
HandshakeRequest::HandshakeRequest()
    : Message(MessageType::HANDSHAKE_REQUEST),
      max_chunk_size(0),
      file_size(0),
      requested_parallel_streams(1),
      username(""),
      auth_method_id(0) {}

std::vector<uint8_t> HandshakeRequest::serialize_payload() const {
    std::vector<uint8_t> buffer;
    write_string(buffer, client_version);
    write_bytes(buffer, client_nonce);
    buffer.push_back(static_cast<uint8_t>(security_level));
    write_uint64(buffer, max_chunk_size);
    write_uint64(buffer, file_size);
    write_uint32(buffer, requested_parallel_streams);
    // Auth fields (appended last for backward compatibility)
    write_string(buffer, username);
    buffer.push_back(auth_method_id);
    return buffer;
}

void HandshakeRequest::deserialize_payload(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    client_version = read_string(data, offset);
    client_nonce = read_bytes(data, offset);
    if (offset < data.size()) {
        security_level = static_cast<crypto::SecurityLevel>(data[offset++]);
    }
    // Read max_chunk_size (if present)
    if (offset + sizeof(uint64_t) <= data.size()) {
        max_chunk_size = read_uint64(data, offset);
    }
    // Read file_size (if present)
    if (offset + sizeof(uint64_t) <= data.size()) {
        file_size = read_uint64(data, offset);
    }
    if (offset + sizeof(uint32_t) <= data.size()) {
        requested_parallel_streams = read_uint32(data, offset);
    } else {
        requested_parallel_streams = 1;
    }
    // Auth fields (backward-compat guards)
    if (offset < data.size()) {
        // We need at least 4 bytes for the string length prefix
        if (offset + 4 <= data.size()) {
            username = read_string(data, offset);
        }
    }
    if (offset < data.size()) {
        auth_method_id = data[offset++];
    } else {
        auth_method_id = 0;
    }
}

// HandshakeResponse implementation
HandshakeResponse::HandshakeResponse()
    : Message(MessageType::HANDSHAKE_RESPONSE),
      authentication_required(false),
      max_chunk_size(0),
      accepted_parallel_streams(1),
      auto_create_directories_allowed(false) {}

std::vector<uint8_t> HandshakeResponse::serialize_payload() const {
    std::vector<uint8_t> buffer;
    write_string(buffer, server_version);
    write_bytes(buffer, server_nonce);
    buffer.push_back(authentication_required ? 1 : 0);
    buffer.push_back(static_cast<uint8_t>(accepted_security_level));
    write_uint64(buffer, max_chunk_size);
    write_uint32(buffer, accepted_parallel_streams);
    buffer.push_back(auto_create_directories_allowed ? 1 : 0);
    return buffer;
}

void HandshakeResponse::deserialize_payload(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    server_version = read_string(data, offset);
    server_nonce = read_bytes(data, offset);
    if (offset >= data.size()) {
        throw ProtocolException("Buffer underflow reading authentication flag");
    }
    authentication_required = data[offset] != 0;
    ++offset;
    if (offset < data.size()) {
        accepted_security_level = static_cast<crypto::SecurityLevel>(data[offset++]);
    }
    // Read max_chunk_size (if present)
    if (offset + sizeof(uint64_t) <= data.size()) {
        max_chunk_size = read_uint64(data, offset);
    }
    if (offset + sizeof(uint32_t) <= data.size()) {
        accepted_parallel_streams = read_uint32(data, offset);
    } else {
        accepted_parallel_streams = 1;
    }
    if (offset < data.size()) {
        auto_create_directories_allowed = data[offset++] != 0;
    } else {
        auto_create_directories_allowed = false;
    }
}

// FileRequest implementation
FileRequest::FileRequest() : Message(MessageType::FILE_REQUEST) {}

std::vector<uint8_t> FileRequest::serialize_payload() const {
    std::vector<uint8_t> buffer;
    write_string(buffer, source_path);
    write_string(buffer, destination_path);
    buffer.push_back(recursive ? 1 : 0);
    write_uint64(buffer, resume_offset);
    buffer.push_back(auto_create_directories ? 1 : 0);
    buffer.push_back(truncate_destination ? 1 : 0);
    
    // Phase 2 Metadata fields
    write_uint32(buffer, permissions);
    buffer.push_back(is_symlink ? 1 : 0);
    write_string(buffer, symlink_target);
    
    // Phase 3 Delta sync fields
    write_uint64(buffer, file_size);

    // Phase 4 Timestamp preservation
    write_uint64(buffer, last_modified);
    return buffer;
}

void FileRequest::deserialize_payload(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    source_path = read_string(data, offset);
    destination_path = read_string(data, offset);
    if (offset >= data.size()) {
        throw ProtocolException("Buffer underflow reading recursive flag");
    }
    recursive = data[offset++] != 0;
    resume_offset = read_uint64(data, offset);
    if (offset < data.size()) {
        auto_create_directories = data[offset++] != 0;
    }
    if (offset < data.size()) {
        truncate_destination = data[offset++] != 0;
    } else {
        truncate_destination = false;
    }
    
    // Phase 2 Metadata fields
    if (offset < data.size()) {
        permissions = read_uint32(data, offset);
    } else {
        permissions = 0;
    }
    if (offset < data.size()) {
        is_symlink = data[offset++] != 0;
    } else {
        is_symlink = false;
    }
    if (offset < data.size()) {
        symlink_target = read_string(data, offset);
    } else {
        symlink_target = "";
    }
    
    // Phase 3 Delta sync fields
    if (offset < data.size()) {
        file_size = read_uint64(data, offset);
    } else {
        file_size = 0;
    }

    // Phase 4 Timestamp preservation
    if (offset < data.size()) {
        last_modified = read_uint64(data, offset);
    } else {
        last_modified = 0;
    }
}

// FileResponse implementation
FileResponse::FileResponse() : Message(MessageType::FILE_RESPONSE) {}

std::vector<uint8_t> FileResponse::serialize_payload() const {
    std::vector<uint8_t> buffer;
    buffer.push_back(success ? 1 : 0);
    write_string(buffer, error_message);
    write_uint64(buffer, file_size);
    write_uint64(buffer, resume_offset);
    write_string(buffer, session_id);
    return buffer;
}

void FileResponse::deserialize_payload(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    if (offset >= data.size()) {
        throw ProtocolException("Buffer underflow reading success flag");
    }
    success = data[offset++] != 0;
    error_message = read_string(data, offset);
    file_size = read_uint64(data, offset);
    resume_offset = read_uint64(data, offset);
    if (offset < data.size()) {
        session_id = read_string(data, offset);
    } else {
        session_id = "";
    }
}

// FileData implementation
FileData::FileData()
    : Message(MessageType::FILE_DATA),
      offset(0),
      uncompressed_size(0),
      is_last_chunk(false),
      compressed(false) {}

std::vector<uint8_t> FileData::Chunk::serialize_payload() const {
    std::vector<uint8_t> buffer;
    write_uint64(buffer, offset);
    write_uint64(buffer, uncompressed_size);
    write_bytes(buffer, data);
    buffer.push_back(is_last_chunk ? 1 : 0);
    buffer.push_back(compressed ? 1 : 0);
    return buffer;
}

void FileData::Chunk::deserialize_payload(const std::vector<uint8_t>& data_buffer) {
    size_t offset_pos = 0;
    offset = read_uint64(data_buffer, offset_pos);
    uncompressed_size = read_uint64(data_buffer, offset_pos);
    data = read_bytes(data_buffer, offset_pos);
    if (offset_pos >= data_buffer.size()) {
        throw ProtocolException("Buffer underflow reading last chunk flag");
    }
    is_last_chunk = data_buffer[offset_pos++] != 0;
    if (offset_pos >= data_buffer.size()) {
        throw ProtocolException("Buffer underflow reading compression flag");
    }
    compressed = data_buffer[offset_pos++] != 0;
}

std::vector<uint8_t> FileData::serialize_payload() const {
    std::vector<uint8_t> buffer;

    size_t reserve_size = 8;
    if (chunks.empty()) {
        reserve_size += 8 + 8 + 4 + data.size() + 2;
    } else {
        for (const auto& chunk : chunks) {
            reserve_size += 8 + 8 + 4 + chunk.data.size() + 2;
        }
    }
    buffer.reserve(reserve_size);

    write_uint32(buffer, FILE_DATA_MAGIC);
    uint32_t chunk_count = chunks.empty() ? 1 : static_cast<uint32_t>(chunks.size());
    write_uint32(buffer, chunk_count);

    auto write_chunk_payload = [&](uint64_t chunk_offset,
                                   uint64_t chunk_uncompressed_size,
                                   const std::vector<uint8_t>& chunk_data,
                                   bool chunk_is_last,
                                   bool chunk_compressed) {
        write_uint64(buffer, chunk_offset);
        write_uint64(buffer, chunk_uncompressed_size == 0 ? chunk_data.size() : chunk_uncompressed_size);
        write_bytes(buffer, chunk_data);
        buffer.push_back(chunk_is_last ? 1 : 0);
        buffer.push_back(chunk_compressed ? 1 : 0);
    };

    if (chunks.empty()) {
        write_chunk_payload(offset, uncompressed_size, data, is_last_chunk, compressed);
    } else {
        for (const auto& chunk : chunks) {
            write_chunk_payload(chunk.offset, chunk.uncompressed_size, chunk.data, chunk.is_last_chunk, chunk.compressed);
        }
    }
    
    return buffer;
}

void FileData::deserialize_payload(const std::vector<uint8_t>& data_buffer) {
    size_t offset_pos = 0;
    uint32_t magic = read_uint32(data_buffer, offset_pos);
    if (magic != FILE_DATA_MAGIC) {
        throw ProtocolException("Unsupported file data payload format");
    }

    uint32_t chunk_count = read_uint32(data_buffer, offset_pos);
    if (chunk_count == 0) {
        throw ProtocolException("File data payload has no chunks");
    }

    chunks.clear();
    chunks.reserve(chunk_count);

    for (uint32_t i = 0; i < chunk_count; ++i) {
        Chunk chunk;
        chunk.offset = read_uint64(data_buffer, offset_pos);
        chunk.uncompressed_size = read_uint64(data_buffer, offset_pos);
        chunk.data = read_bytes(data_buffer, offset_pos);
        if (offset_pos >= data_buffer.size()) {
            throw ProtocolException("Buffer underflow reading last chunk flag");
        }
        chunk.is_last_chunk = data_buffer[offset_pos++] != 0;
        if (offset_pos >= data_buffer.size()) {
            throw ProtocolException("Buffer underflow reading compression flag");
        }
        chunk.compressed = data_buffer[offset_pos++] != 0;
        chunks.push_back(chunk);
    }

    if (offset_pos != data_buffer.size()) {
        throw ProtocolException("Trailing bytes in file data payload");
    }

    const auto& first = chunks.front();
    offset = first.offset;
    uncompressed_size = first.uncompressed_size;
    data = first.data;
    is_last_chunk = first.is_last_chunk;
    compressed = first.compressed;
}

// FileAck implementation
FileAck::FileAck() : Message(MessageType::FILE_ACK) {}

std::vector<uint8_t> FileAck::serialize_payload() const {
    std::vector<uint8_t> buffer;
    write_uint64(buffer, bytes_received);
    buffer.push_back(success ? 1 : 0);
    write_string(buffer, error_message);
    return buffer;
}

void FileAck::deserialize_payload(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    bytes_received = read_uint64(data, offset);
    if (offset >= data.size()) {
        throw ProtocolException("Buffer underflow reading success flag");
    }
    success = data[offset++] != 0;
    error_message = read_string(data, offset);
}

// ErrorMessage implementation
ErrorMessage::ErrorMessage() : Message(MessageType::ERROR_MESSAGE) {}

std::vector<uint8_t> ErrorMessage::serialize_payload() const {
    std::vector<uint8_t> buffer;
    write_uint32(buffer, error_code);
    write_string(buffer, error_description);
    return buffer;
}

void ErrorMessage::deserialize_payload(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    error_code = read_uint32(data, offset);
    error_description = read_string(data, offset);
}

// AuthChallenge implementation
AuthChallenge::AuthChallenge()
    : Message(MessageType::AUTH_CHALLENGE),
      method(0),
      pbkdf2_iterations(0) {}

std::vector<uint8_t> AuthChallenge::serialize_payload() const {
    std::vector<uint8_t> buffer;
    buffer.push_back(method);
    write_bytes(buffer, challenge_nonce);
    write_string(buffer, salt_hex);
    write_uint32(buffer, static_cast<uint32_t>(pbkdf2_iterations));
    write_bytes(buffer, kem_ciphertext);
    write_string(buffer, mlkem_level_str);
    write_bytes(buffer, kem_nonce);
    return buffer;
}

void AuthChallenge::deserialize_payload(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    if (offset >= data.size()) throw ProtocolException("AuthChallenge: missing method byte");
    method = data[offset++];
    challenge_nonce   = read_bytes(data, offset);
    salt_hex          = read_string(data, offset);
    pbkdf2_iterations = static_cast<int>(read_uint32(data, offset));
    kem_ciphertext    = read_bytes(data, offset);
    mlkem_level_str   = read_string(data, offset);
    kem_nonce         = read_bytes(data, offset);
}

// AuthResponse implementation
AuthResponse::AuthResponse()
    : Message(MessageType::AUTH_RESPONSE) {}

std::vector<uint8_t> AuthResponse::serialize_payload() const {
    std::vector<uint8_t> buffer;
    write_bytes(buffer, proof);
    return buffer;
}

void AuthResponse::deserialize_payload(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    proof = read_bytes(data, offset);
}

// AuthResult implementation
AuthResult::AuthResult()
    : Message(MessageType::AUTH_RESULT),
      success(false) {}

std::vector<uint8_t> AuthResult::serialize_payload() const {
    std::vector<uint8_t> buffer;
    buffer.push_back(success ? 1 : 0);
    write_string(buffer, error_message);
    return buffer;
}

void AuthResult::deserialize_payload(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    if (offset >= data.size()) throw ProtocolException("AuthResult: missing success byte");
    success = data[offset++] != 0;
    if (offset < data.size()) {
        error_message = read_string(data, offset);
    }
}

// DownloadRequest implementation
DownloadRequest::DownloadRequest()
    : Message(MessageType::DOWNLOAD_REQUEST),
      resume_offset(0) {}

std::vector<uint8_t> DownloadRequest::serialize_payload() const {
    std::vector<uint8_t> buffer;
    write_string(buffer, remote_path);
    write_uint64(buffer, resume_offset);
    return buffer;
}

void DownloadRequest::deserialize_payload(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    remote_path = read_string(data, offset);
    if (offset + 8 <= data.size()) {
        resume_offset = read_uint64(data, offset);
    } else {
        resume_offset = 0;
    }
}

// DownloadResponse implementation
DownloadResponse::DownloadResponse()
    : Message(MessageType::DOWNLOAD_RESPONSE),
      success(false),
      file_size(0),
      is_directory(false) {}

std::vector<uint8_t> DownloadResponse::serialize_payload() const {
    std::vector<uint8_t> buffer;
    buffer.push_back(success ? 1 : 0);
    write_string(buffer, error_message);
    write_uint64(buffer, file_size);
    buffer.push_back(is_directory ? 1 : 0);
    
    // Phase 2 Metadata fields
    write_uint32(buffer, permissions);
    buffer.push_back(is_symlink ? 1 : 0);
    write_string(buffer, symlink_target);
    write_string(buffer, session_id);

    // Phase 4 Timestamp preservation
    write_uint64(buffer, last_modified);
    return buffer;
}

void DownloadResponse::deserialize_payload(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    if (offset >= data.size()) throw ProtocolException("DownloadResponse: missing success byte");
    success = data[offset++] != 0;
    error_message = read_string(data, offset);
    file_size = read_uint64(data, offset);
    if (offset >= data.size()) throw ProtocolException("DownloadResponse: missing is_directory byte");
    is_directory = data[offset++] != 0;
    
    // Phase 2 Metadata fields
    if (offset < data.size()) {
        permissions = read_uint32(data, offset);
    } else {
        permissions = 0;
    }
    if (offset < data.size()) {
        is_symlink = data[offset++] != 0;
    } else {
        is_symlink = false;
    }
    if (offset < data.size()) {
        symlink_target = read_string(data, offset);
    } else {
        symlink_target = "";
    }
    if (offset < data.size()) {
        session_id = read_string(data, offset);
    } else {
        session_id = "";
    }

    // Phase 4 Timestamp preservation
    if (offset < data.size()) {
        last_modified = read_uint64(data, offset);
    } else {
        last_modified = 0;
    }
}

// ListRequest implementation
ListRequest::ListRequest()
    : Message(MessageType::LIST_REQUEST),
      recursive(false) {}

std::vector<uint8_t> ListRequest::serialize_payload() const {
    std::vector<uint8_t> buffer;
    write_string(buffer, remote_path);
    buffer.push_back(recursive ? 1 : 0);
    return buffer;
}

void ListRequest::deserialize_payload(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    remote_path = read_string(data, offset);
    if (offset >= data.size()) throw ProtocolException("ListRequest: missing recursive byte");
    recursive = data[offset++] != 0;
}

// ListResponse implementation
ListResponse::ListResponse()
    : Message(MessageType::LIST_RESPONSE),
      success(false) {}

std::vector<uint8_t> ListResponse::serialize_payload() const {
    std::vector<uint8_t> buffer;
    buffer.push_back(success ? 1 : 0);
    write_string(buffer, error_message);
    write_uint32(buffer, static_cast<uint32_t>(entries.size()));
    for (const auto& e : entries) {
        write_string(buffer, e.path);
        write_uint64(buffer, e.size);
        buffer.push_back(e.is_directory ? 1 : 0);
        write_uint64(buffer, e.last_modified);
        
        // Phase 2 Metadata fields
        write_uint32(buffer, e.permissions);
        buffer.push_back(e.is_symlink ? 1 : 0);
        write_string(buffer, e.symlink_target);
    }
    return buffer;
}

void ListResponse::deserialize_payload(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    if (offset >= data.size()) throw ProtocolException("ListResponse: missing success byte");
    success = data[offset++] != 0;
    error_message = read_string(data, offset);
    uint32_t size = read_uint32(data, offset);
    entries.clear();
    for (uint32_t i = 0; i < size; ++i) {
        RemoteFileInfo info;
        info.path = read_string(data, offset);
        info.size = read_uint64(data, offset);
        if (offset >= data.size()) throw ProtocolException("ListResponse: missing entry is_directory byte");
        info.is_directory = data[offset++] != 0;
        info.last_modified = read_uint64(data, offset);
        
        // Phase 2 Metadata fields
        if (offset < data.size()) {
            info.permissions = read_uint32(data, offset);
        } else {
            info.permissions = 0;
        }
        if (offset < data.size()) {
            info.is_symlink = data[offset++] != 0;
        } else {
            info.is_symlink = false;
        }
        if (offset < data.size()) {
            info.symlink_target = read_string(data, offset);
        } else {
            info.symlink_target = "";
        }
        entries.push_back(info);
    }
}

// Disconnect implementation
Disconnect::Disconnect()
    : Message(MessageType::DISCONNECT) {}

std::vector<uint8_t> Disconnect::serialize_payload() const {
    return std::vector<uint8_t>();
}

void Disconnect::deserialize_payload(const std::vector<uint8_t>& data) {
    // No payload
}

// FileVerifyRequest implementation
FileVerifyRequest::FileVerifyRequest()
    : Message(MessageType::FILE_VERIFY_REQUEST) {}

std::vector<uint8_t> FileVerifyRequest::serialize_payload() const {
    std::vector<uint8_t> buffer;
    write_string(buffer, file_path);
    write_bytes(buffer, expected_hash);
    return buffer;
}

void FileVerifyRequest::deserialize_payload(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    file_path = read_string(data, offset);
    expected_hash = read_bytes(data, offset);
}

// FileVerifyResponse implementation
FileVerifyResponse::FileVerifyResponse()
    : Message(MessageType::FILE_VERIFY_RESPONSE),
      success(false) {}

std::vector<uint8_t> FileVerifyResponse::serialize_payload() const {
    std::vector<uint8_t> buffer;
    buffer.push_back(success ? 1 : 0);
    write_string(buffer, error_message);
    write_bytes(buffer, actual_hash);
    return buffer;
}

void FileVerifyResponse::deserialize_payload(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    if (offset >= data.size()) throw ProtocolException("FileVerifyResponse: missing success byte");
    success = data[offset++] != 0;
    error_message = read_string(data, offset);
    actual_hash = read_bytes(data, offset);
}

// BlockHashesRequest implementation
BlockHashesRequest::BlockHashesRequest()
    : Message(MessageType::BLOCK_HASHES_REQUEST),
      block_size(65536) {}

std::vector<uint8_t> BlockHashesRequest::serialize_payload() const {
    std::vector<uint8_t> buffer;
    write_string(buffer, file_path);
    write_uint64(buffer, block_size);
    return buffer;
}

void BlockHashesRequest::deserialize_payload(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    file_path = read_string(data, offset);
    if (offset < data.size()) {
        block_size = read_uint64(data, offset);
    } else {
        block_size = 65536;
    }
}

// BlockHashesResponse implementation
BlockHashesResponse::BlockHashesResponse()
    : Message(MessageType::BLOCK_HASHES_RESPONSE),
      success(false),
      block_size(65536) {}

std::vector<uint8_t> BlockHashesResponse::serialize_payload() const {
    std::vector<uint8_t> buffer;
    buffer.push_back(success ? 1 : 0);
    write_string(buffer, error_message);
    write_uint64(buffer, block_size);
    write_uint32(buffer, static_cast<uint32_t>(blocks.size()));
    for (const auto& b : blocks) {
        write_uint64(buffer, b.offset);
        write_bytes(buffer, b.hash);
    }
    return buffer;
}

void BlockHashesResponse::deserialize_payload(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    if (offset >= data.size()) throw ProtocolException("BlockHashesResponse: missing success byte");
    success = data[offset++] != 0;
    error_message = read_string(data, offset);
    if (offset < data.size()) {
        block_size = read_uint64(data, offset);
        uint32_t size = read_uint32(data, offset);
        blocks.clear();
        for (uint32_t i = 0; i < size; ++i) {
            BlockHashInfo info;
            info.offset = read_uint64(data, offset);
            info.hash = read_bytes(data, offset);
            blocks.push_back(info);
        }
    } else {
        block_size = 65536;
        blocks.clear();
    }
}

// TransferStatusRequest implementation
TransferStatusRequest::TransferStatusRequest() : Message(MessageType::TRANSFER_STATUS_REQUEST) {}

std::vector<uint8_t> TransferStatusRequest::serialize_payload() const {
    std::vector<uint8_t> buffer;
    write_string(buffer, session_id);
    return buffer;
}

void TransferStatusRequest::deserialize_payload(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    session_id = read_string(data, offset);
}

// TransferStatusResponse implementation
TransferStatusResponse::TransferStatusResponse() : Message(MessageType::TRANSFER_STATUS_RESPONSE) {}

std::vector<uint8_t> TransferStatusResponse::serialize_payload() const {
    std::vector<uint8_t> buffer;
    buffer.push_back(success ? 1 : 0);
    write_string(buffer, error_message);
    buffer.push_back(active ? 1 : 0);
    write_uint64(buffer, bytes_transferred);
    write_uint64(buffer, total_bytes);
    write_string(buffer, status_string);
    write_string(buffer, logs);
    return buffer;
}

void TransferStatusResponse::deserialize_payload(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    if (offset >= data.size()) throw ProtocolException("TransferStatusResponse: missing success byte");
    success = data[offset++] != 0;
    error_message = read_string(data, offset);
    if (offset >= data.size()) throw ProtocolException("TransferStatusResponse: missing active byte");
    active = data[offset++] != 0;
    bytes_transferred = read_uint64(data, offset);
    total_bytes = read_uint64(data, offset);
    status_string = read_string(data, offset);
    logs = read_string(data, offset);
}

} // namespace protocol
} // namespace netcopy

