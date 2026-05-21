#include "protocol/message.h"
#include "exceptions.h"
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
        buffer.insert(buffer.end(), data.begin(), data.end());
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
        std::vector<uint8_t> data(buffer.begin() + offset, buffer.begin() + offset + length);
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
    header_data.insert(header_data.end(), payload.begin(), payload.end());
    
    return header_data;
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
}

// FileResponse implementation
FileResponse::FileResponse() : Message(MessageType::FILE_RESPONSE) {}

std::vector<uint8_t> FileResponse::serialize_payload() const {
    std::vector<uint8_t> buffer;
    buffer.push_back(success ? 1 : 0);
    write_string(buffer, error_message);
    write_uint64(buffer, file_size);
    write_uint64(buffer, resume_offset);
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

    write_uint32(buffer, FILE_DATA_MAGIC);
    uint32_t chunk_count = chunks.empty() ? 1 : static_cast<uint32_t>(chunks.size());
    write_uint32(buffer, chunk_count);

    if (chunks.empty()) {
        Chunk chunk;
        chunk.offset = offset;
        chunk.uncompressed_size = uncompressed_size == 0 ? data.size() : uncompressed_size;
        chunk.data = data;
        chunk.is_last_chunk = is_last_chunk;
        chunk.compressed = compressed;
        auto chunk_payload = chunk.serialize_payload();
        buffer.insert(buffer.end(), chunk_payload.begin(), chunk_payload.end());
    } else {
        for (const auto& chunk : chunks) {
            auto chunk_payload = chunk.serialize_payload();
            buffer.insert(buffer.end(), chunk_payload.begin(), chunk_payload.end());
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

} // namespace protocol
} // namespace netcopy

