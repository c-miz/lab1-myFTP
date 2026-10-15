#pragma once

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace ftp {
namespace fs = std::filesystem;
constexpr uint32_t HEADER_SIZE = 12;
constexpr size_t BUFFER_SIZE = 16384;
constexpr size_t MAX_TEXT_SIZE = 1024 * 1024;
constexpr uint8_t MAGIC[] = {0xc1, 0xa1, 0x10, 'f', 't', 'p'};
enum Type : uint8_t {
    OPEN_REQUEST = 0xA1, OPEN_REPLY, LIST_REQUEST, LIST_REPLY,
    CD_REQUEST, CD_REPLY, GET_REQUEST, GET_REPLY, PUT_REQUEST, PUT_REPLY,
    SHA_REQUEST, SHA_REPLY, QUIT_REQUEST, QUIT_REPLY, FILE_DATA = 0xFF
};

struct Header {
    uint8_t protocol[6];
    uint8_t type;
    uint8_t status;
    uint32_t length;
} __attribute__((packed));
static_assert(sizeof(Header) == HEADER_SIZE, "myFTP header must be 12 bytes");

// Parsed headers use host byte order; only Header represents bytes on the wire.
struct Message {
    uint8_t type;
    uint8_t status;
    uint32_t payload_size;
};

class Socket {
public:
    explicit Socket(int fd = -1) : fd_(fd) {}
    ~Socket() { reset(); }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    int get() const { return fd_; }
    void reset(int fd = -1) {
        if (fd_ >= 0) ::close(fd_);
        fd_ = fd;
    }
private:
    int fd_;
};

using File = std::unique_ptr<FILE, decltype(&std::fclose)>;

inline void sendAll(int fd, const void* data, size_t size) {
    const char* bytes = static_cast<const char*>(data);
    while (size > 0) {
        ssize_t n = ::send(fd, bytes, size, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) throw std::runtime_error("socket send failed");
        bytes += n;
        size -= static_cast<size_t>(n);
    }
}

inline void receiveExact(int fd, void* data, size_t size) {
    char* bytes = static_cast<char*>(data);
    while (size > 0) {
        ssize_t n = ::recv(fd, bytes, size, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n == 0) throw std::runtime_error("peer closed the connection");
        if (n < 0) throw std::runtime_error("socket receive failed");
        bytes += n;
        size -= static_cast<size_t>(n);
    }
}

inline void sendHeader(int fd, uint8_t type, uint8_t status = 0,
                       uint32_t payload_size = 0) {
    if (payload_size > static_cast<uint32_t>(INT_MAX) - HEADER_SIZE)
        throw std::runtime_error("message is too large");
    Header header{};
    std::memcpy(header.protocol, MAGIC, sizeof(MAGIC));
    header.type = type;
    header.status = status;
    header.length = htonl(HEADER_SIZE + payload_size);
    sendAll(fd, &header, sizeof(header));
}

inline Message receiveHeader(int fd) {
    Header header{};
    receiveExact(fd, &header, sizeof(header));
    uint32_t wire_length;
    std::memcpy(&wire_length, &header.length, sizeof(wire_length));
    uint32_t length = ntohl(wire_length);
    if (std::memcmp(header.protocol, MAGIC, sizeof(MAGIC)) != 0 ||
        length < HEADER_SIZE || length > static_cast<uint32_t>(INT_MAX))
        throw std::runtime_error("invalid myFTP header");
    return {header.type, header.status, length - HEADER_SIZE};
}

inline Message expectHeader(int fd, uint8_t type, bool empty = true) {
    Message message = receiveHeader(fd);
    if (message.type != type || (empty && message.payload_size != 0))
        throw std::runtime_error("unexpected myFTP message");
    return message;
}

inline bool successStatus(const Message& message) {
    if (message.status > 1) throw std::runtime_error("invalid reply status");
    return message.status == 1;
}

inline void sendText(int fd, uint8_t type, const std::string& text) {
    if (text.size() >= MAX_TEXT_SIZE || text.find('\0') != std::string::npos)
        throw std::runtime_error("invalid text payload");
    sendHeader(fd, type, 0, static_cast<uint32_t>(text.size() + 1));
    sendAll(fd, text.c_str(), text.size() + 1);
}

inline std::string receiveText(int fd, const Message& message,
                               size_t limit = MAX_TEXT_SIZE) {
    if (message.payload_size == 0 || message.payload_size > limit)
        throw std::runtime_error("invalid text length");
    std::string text(message.payload_size, '\0');
    receiveExact(fd, text.data(), text.size());
    if (text.back() != '\0' || text.find('\0') != text.size() - 1)
        throw std::runtime_error("invalid string terminator");
    text.pop_back();
    return text;
}

inline bool validName(const std::string& name, bool directory = false) {
    if (name.empty() || name.size() > 255 || name == "." || name == "..") return false;
    for (unsigned char c : name) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || (!directory && c == '.')) continue;
        return false;
    }
    return true;
}

inline File openInput(const fs::path& path, uint32_t& size) {
    File file(std::fopen(path.c_str(), "rb"), &std::fclose);
    if (!file) return file;
    struct stat info{};
    if (::fstat(::fileno(file.get()), &info) != 0 || !S_ISREG(info.st_mode) ||
        info.st_size < 0 || static_cast<uint64_t>(info.st_size) > INT_MAX - HEADER_SIZE) {
        file.reset();
        return file;
    }
    size = static_cast<uint32_t>(info.st_size);
    return file;
}

inline void sendFile(int fd, FILE* file, uint32_t size) {
    sendHeader(fd, FILE_DATA, 0, size);
    std::array<char, BUFFER_SIZE> buffer{};
    while (size > 0) {
        size_t wanted = std::min<size_t>(buffer.size(), size);
        size_t n = std::fread(buffer.data(), 1, wanted, file);
        if (n == 0 || std::ferror(file)) throw std::runtime_error("file read failed");
        sendAll(fd, buffer.data(), n);
        size -= static_cast<uint32_t>(n);
    }
}

inline void receiveFile(int fd, const fs::path& path, uint32_t size) {
    File file(std::fopen(path.c_str(), "wb"), &std::fclose);
    if (!file) throw std::runtime_error("cannot open destination file");
    std::array<char, BUFFER_SIZE> buffer{};
    while (size > 0) {
        size_t n = std::min<size_t>(buffer.size(), size);
        receiveExact(fd, buffer.data(), n);
        if (std::fwrite(buffer.data(), 1, n, file.get()) != n)
            throw std::runtime_error("file write failed");
        size -= static_cast<uint32_t>(n);
    }
    if (std::fclose(file.release()) != 0) throw std::runtime_error("file close failed");
}

inline int parsePort(const std::string& text) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("port must be an integer from 1 to 65535");
    size_t used = 0;
    int port = std::stoi(text, &used);
    if (used != text.size() || port < 1 || port > 65535)
        throw std::runtime_error("port must be from 1 to 65535");
    return port;
}
} // namespace ftp
