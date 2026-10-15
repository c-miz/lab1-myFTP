#include "ftp_common.h"
#include <iostream>
#include <thread>

using namespace ftp;

struct ClientSession {
    fs::path cwd;
    bool connected = false;
};

bool changeDirectory(ClientSession& session, const std::string& directory) {
    if (!validName(directory, true)) return false;
    std::error_code error;
    fs::path target = fs::canonical(session.cwd / directory, error);
    if (error || !fs::is_directory(target, error) || error) return false;
    session.cwd = target;
    return true;
}

std::string shellQuote(const std::string& text) {
    std::string quoted = "'";
    for (char c : text) {
        if (c == '\'') quoted += "'\\''";
        else quoted += c;
    }
    return quoted + "'";
}

std::string commandOutput(const fs::path& directory, const std::string& command) {
    // cd runs only in popen's child shell; the server's cwd never changes.
    std::string invocation = "cd " + shellQuote(directory.string()) + " && " + command;
    std::unique_ptr<FILE, decltype(&::pclose)> pipe(::popen(invocation.c_str(), "r"), &::pclose);
    if (!pipe) throw std::runtime_error("cannot execute command");
    std::string output;
    std::array<char, BUFFER_SIZE> buffer{};
    size_t n;
    while ((n = std::fread(buffer.data(), 1, buffer.size(), pipe.get())) > 0) {
        if (output.size() + n >= MAX_TEXT_SIZE)
            throw std::runtime_error("command output is too large");
        output.append(buffer.data(), n);
    }
    bool failed = std::ferror(pipe.get()) != 0;
    int status = ::pclose(pipe.release());
    if (failed || status != 0) throw std::runtime_error("command execution failed");
    return output;
}

void requireEmpty(const Message& message) {
    if (message.payload_size != 0) throw std::runtime_error("unexpected request payload");
}

void serveClient(int fd, const fs::path& initial_dir) {
    Socket connection(fd);
    try {
        ClientSession session{initial_dir};
        while (true) {
            Message request = receiveHeader(fd);
            if (!session.connected) {
                if (request.type != OPEN_REQUEST) throw std::runtime_error("OPEN required first");
                requireEmpty(request);
                sendHeader(fd, OPEN_REPLY, 1);
                session.connected = true;
                continue;
            }
            switch (request.type) {
            case LIST_REQUEST: {
                requireEmpty(request);
                sendText(fd, LIST_REPLY, commandOutput(session.cwd, "ls"));
                break;
            }
            case CD_REQUEST: {
                std::string directory = receiveText(fd, request, 256);
                sendHeader(fd, CD_REPLY, changeDirectory(session, directory) ? 1 : 0);
                break;
            }
            case GET_REQUEST: {
                std::string filename = receiveText(fd, request, 256);
                uint32_t size = 0;
                File file(nullptr, &std::fclose);
                if (validName(filename)) file = openInput(session.cwd / filename, size);
                sendHeader(fd, GET_REPLY, file ? 1 : 0);
                if (file) sendFile(fd, file.get(), size);
                break;
            }
            case PUT_REQUEST: {
                std::string filename = receiveText(fd, request, 256);
                if (!validName(filename)) throw std::runtime_error("invalid upload filename");
                sendHeader(fd, PUT_REPLY);
                Message data = expectHeader(fd, FILE_DATA, false);
                receiveFile(fd, session.cwd / filename, data.payload_size);
                break;
            }
            case SHA_REQUEST: {
                std::string filename = receiveText(fd, request, 256);
                uint32_t size = 0;
                File file(nullptr, &std::fclose);
                if (validName(filename)) file = openInput(session.cwd / filename, size);
                if (!file) {
                    sendHeader(fd, SHA_REPLY, 0);
                    break;
                }
                std::string hash;
                try { hash = commandOutput(session.cwd, "sha256sum -- " + shellQuote(filename)); }
                catch (const std::exception&) {
                    sendHeader(fd, SHA_REPLY, 0);
                    break;
                }
                sendHeader(fd, SHA_REPLY, 1);
                sendText(fd, FILE_DATA, hash);
                break;
            }
            case QUIT_REQUEST:
                requireEmpty(request);
                sendHeader(fd, QUIT_REPLY);
                return;
            default:
                throw std::runtime_error("unexpected request type");
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "Connection ended: " << error.what() << '\n';
    }
}

int main(int argc, char** argv) {
    try {
        if (argc != 1 && argc != 3) {
            std::cerr << "Usage: ftp_server [<ip> <port>]\n";
            return 1;
        }
        std::string ip = argc == 3 ? argv[1] : "127.0.0.1";
        int port = argc == 3 ? parsePort(argv[2]) : 12323;
        const fs::path initial_dir = fs::current_path();
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<uint16_t>(port));
        if (::inet_pton(AF_INET, ip.c_str(), &address.sin_addr) != 1)
            throw std::runtime_error("invalid IPv4 address");
        Socket listener(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
        if (listener.get() < 0) throw std::runtime_error("cannot create listening socket");
        int reuse = 1;
        if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0 ||
            ::bind(listener.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
            ::listen(listener.get(), SOMAXCONN) < 0)
            throw std::runtime_error("cannot listen on requested address and port");
        while (true) {
            int fd = ::accept4(listener.get(), nullptr, nullptr, SOCK_CLOEXEC);
            if (fd < 0) {
                if (errno == EINTR) continue;
                throw std::runtime_error("accept failed");
            }
            try {
                std::thread(serveClient, fd, initial_dir).detach();
            } catch (const std::exception& error) {
                ::close(fd);
                std::cerr << "Cannot start client worker: " << error.what() << '\n';
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
