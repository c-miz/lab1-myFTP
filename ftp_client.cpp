#include "ftp_common.h"
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

using namespace ftp;

class Client {
public:
    void open(const std::string& ip, int port) {
        if (connected_) {
            std::cerr << "Already connected; use quit before opening another connection\n";
            return;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<uint16_t>(port));
        if (::inet_pton(AF_INET, ip.c_str(), &address.sin_addr) != 1)
            throw std::runtime_error("invalid IPv4 address");
        socket_.reset(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
        if (socket_.get() < 0) throw std::runtime_error("cannot create socket");
        if (::connect(socket_.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
            throw std::runtime_error("cannot connect to server");
        sendHeader(socket_.get(), OPEN_REQUEST);
        if (!successStatus(expectHeader(socket_.get(), OPEN_REPLY)))
            throw std::runtime_error("server rejected connection");
        connected_ = true;
        std::cout << "Connected to server " << ip << ':' << port << '\n';
    }

    void ls() {
        requireConnection();
        sendHeader(socket_.get(), LIST_REQUEST);
        Message reply = expectHeader(socket_.get(), LIST_REPLY, false);
        std::cout << receiveText(socket_.get(), reply) << std::flush;
    }

    void cd(const std::string& directory) {
        requireConnection();
        if (!checkName(directory, true)) return;
        sendText(socket_.get(), CD_REQUEST, directory);
        if (successStatus(expectHeader(socket_.get(), CD_REPLY)))
            std::cout << "Changed directory to " << directory << '\n';
        else
            std::cerr << "Directory does not exist: " << directory << '\n';
    }

    void get(const std::string& filename) {
        requireConnection();
        if (!checkName(filename)) return;
        sendText(socket_.get(), GET_REQUEST, filename);
        if (!successStatus(expectHeader(socket_.get(), GET_REPLY))) {
            std::cerr << "File is unavailable on server: " << filename << '\n';
            return;
        }
        Message data = expectHeader(socket_.get(), FILE_DATA, false);
        receiveFile(socket_.get(), filename, data.payload_size);
        std::cout << "Downloaded " << filename << '\n';
    }

    void put(const std::string& filename) {
        requireConnection();
        if (!checkName(filename)) return;
        uint32_t size = 0;
        File file = openInput(filename, size);
        if (!file) {
            std::cerr << "Local file is unavailable: " << filename << '\n';
            return;
        }
        // Open the file before asking the server to wait for FILE_DATA.
        sendText(socket_.get(), PUT_REQUEST, filename);
        expectHeader(socket_.get(), PUT_REPLY);
        sendFile(socket_.get(), file.get(), size);
        std::cout << "Uploaded " << filename << '\n';
    }

    void sha256(const std::string& filename) {
        requireConnection();
        if (!checkName(filename)) return;
        sendText(socket_.get(), SHA_REQUEST, filename);
        if (!successStatus(expectHeader(socket_.get(), SHA_REPLY))) {
            std::cerr << "File is unavailable on server: " << filename << '\n';
            return;
        }
        Message data = expectHeader(socket_.get(), FILE_DATA, false);
        std::string hash = receiveText(socket_.get(), data);
        std::cout << hash;
        if (hash.empty() || hash.back() != '\n') std::cout << '\n';
    }

    bool quit() {
        if (!connected_) return true;
        sendHeader(socket_.get(), QUIT_REQUEST);
        expectHeader(socket_.get(), QUIT_REPLY);
        disconnect();
        std::cout << "Disconnected from server\n";
        return false;
    }

    void disconnect() {
        socket_.reset();
        connected_ = false;
    }

private:
    Socket socket_;
    bool connected_ = false;

    void requireConnection() const {
        if (!connected_) throw std::runtime_error("not connected to server");
    }
    static bool checkName(const std::string& name, bool directory = false) {
        if (validName(name, directory)) return true;
        std::cerr << "Invalid " << (directory ? "directory" : "file") << " name\n";
        return false;
    }
};

std::vector<std::string> parseCommand(const std::string& line) {
    std::vector<std::string> arguments;
    std::istringstream input(line);
    std::string argument;
    while (input >> std::quoted(argument)) arguments.push_back(argument);
    return arguments;
}

int main() {
    Client client;
    std::string line;
    while (std::cout << "ftp> " << std::flush, std::getline(std::cin, line)) {
        auto args = parseCommand(line);
        if (args.empty()) continue;
        const std::string& command = args[0];
        // Invalid user input must not interrupt a healthy existing connection.
        if (command == "open") {
            if (args.size() != 3) {
                std::cerr << "Usage: open <ip> <port>\n";
                continue;
            }
            int port;
            try { port = parsePort(args[2]); }
            catch (const std::exception& error) {
                std::cerr << "Error: " << error.what() << '\n';
                continue;
            }
            try { client.open(args[1], port); }
            catch (const std::exception& error) {
                client.disconnect();
                std::cerr << "Error: " << error.what() << '\n';
            }
            continue;
        }
        bool no_argument = command == "ls" || command == "quit";
        bool one_argument = command == "cd" || command == "get" ||
                            command == "put" || command == "sha256";
        if (!no_argument && !one_argument) {
            std::cerr << "Unknown command: " << command << '\n';
            continue;
        }
        if (args.size() != (no_argument ? 1U : 2U)) {
            std::cerr << "Usage: " << command << (no_argument ? "" : " <name>") << '\n';
            continue;
        }
        try {
            if (command == "ls") client.ls();
            else if (command == "cd") client.cd(args[1]);
            else if (command == "get") client.get(args[1]);
            else if (command == "put") client.put(args[1]);
            else if (command == "sha256") client.sha256(args[1]);
            else if (client.quit()) break;
        } catch (const std::exception& error) {
            // A partial transfer cannot be reused as a fresh protocol stream.
            client.disconnect();
            std::cerr << "Error: " << error.what() << '\n';
        }
    }
    return 0;
}
