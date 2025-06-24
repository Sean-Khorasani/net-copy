#include "protocol/message.h"
#include "exceptions.h"
#include <cstring>
#include <sstream>

namespace netcopy {
namespace protocol {

// Helper functions for serialization
namespace {
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
        default:
            throw ProtocolException("Unknown message type");
    }
    
    message->header_ = header;
    message->deserialize_payload(payload);
    
    return message;
}

// HandshakeRequest implementation
HandshakeRequest::HandshakeRequest() : Message(MessageType::HANDSHAKE_REQUEST) {}

std::vector<uint8_t> HandshakeRequest::serialize_payload() const {
    std::vector<uint8_t> buffer;
    write_string(buffer, client_version);
    write_bytes(buffer, client_nonce);
    buffer.push_back(static_cast<uint8_t>(security_level));
    return buffer;
}

void HandshakeRequest::deserialize_payload(const std::vector<uint8_t>& data) {
    size_t offset = 0;
    client_version = read_string(data, offset);
    client_nonce = read_bytes(data, offset);
    if (offset < data.size()) {
        security_level = static_cast<crypto::SecurityLevel>(data[offset]);
    }
}

// HandshakeResponse implementation
HandshakeResponse::HandshakeResponse() : Message(MessageType::HANDSHAKE_RESPONSE) {}

std::vector<uint8_t> HandshakeResponse::serialize_payload() const {
    std::vector<uint8_t> buffer;
    write_string(buffer, server_version);
    write_bytes(buffer, server_nonce);
    buffer.push_back(authentication_required ? 1 : 0);
    buffer.push_back(static_cast<uint8_t>(accepted_security_level));
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
        accepted_security_level = static_cast<crypto::SecurityLevel>(data[offset]);
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
FileData::FileData() : Message(MessageType::FILE_DATA) {}

std::vector<uint8_t> FileData::serialize_payload() const {
    std::vector<uint8_t> buffer;
    write_uint64(buffer, offset);
    write_bytes(buffer, data);
    buffer.push_back(is_last_chunk ? 1 : 0);
    return buffer;
}

void FileData::deserialize_payload(const std::vector<uint8_t>& data_buffer) {
    size_t offset_pos = 0;
    offset = read_uint64(data_buffer, offset_pos);
    data = read_bytes(data_buffer, offset_pos);
    if (offset_pos >= data_buffer.size()) {
        throw ProtocolException("Buffer underflow reading last chunk flag");
    }
    is_last_chunk = data_buffer[offset_pos] != 0;
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

} // namespace protocol
} // namespace netcopy

