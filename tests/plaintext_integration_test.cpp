#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

class file_descriptor {
public:
    explicit file_descriptor(int value) : value_{value} {}
    ~file_descriptor() {
        if (value_ >= 0) {
            close(value_);
        }
    }

    file_descriptor(const file_descriptor&) = delete;
    file_descriptor& operator=(const file_descriptor&) = delete;

    file_descriptor(file_descriptor&& other) noexcept : value_{other.value_} {
        other.value_ = -1;
    }

    file_descriptor& operator=(file_descriptor&& other) noexcept {
        if (this != &other) {
            if (value_ >= 0) {
                close(value_);
            }
            value_ = other.value_;
            other.value_ = -1;
        }
        return *this;
    }

    int get() const { return value_; }

private:
    int value_;
};

int reserve_port() {
    const file_descriptor socket_fd{socket(AF_INET, SOCK_STREAM, 0)};
    if (socket_fd.get() < 0) {
        throw std::runtime_error{"failed to create reservation socket"};
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;

    if (bind(socket_fd.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        throw std::runtime_error{"failed to reserve port"};
    }

    socklen_t address_length = sizeof(address);
    if (getsockname(socket_fd.get(), reinterpret_cast<sockaddr*>(&address), &address_length) < 0) {
        throw std::runtime_error{"failed to read reserved port"};
    }

    return ntohs(address.sin_port);
}

file_descriptor connect_to_server(int port) {
    file_descriptor socket_fd{socket(AF_INET, SOCK_STREAM, 0)};
    if (socket_fd.get() < 0) {
        throw std::runtime_error{"failed to create client socket"};
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<uint16_t>(port));

    if (connect(socket_fd.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        throw std::runtime_error{"failed to connect to server"};
    }

    return socket_fd;
}

std::string read_response(int socket_fd) {
    std::string response;
    char buffer[1024]{};
    while (true) {
        const ssize_t bytes_read = recv(socket_fd, buffer, sizeof(buffer), 0);
        if (bytes_read < 0) {
            throw std::runtime_error{"failed to read response"};
        }
        if (bytes_read == 0) {
            break;
        }
        response.append(buffer, static_cast<std::size_t>(bytes_read));
        if (response.find("\r\n\r\nHello, World!") != std::string::npos) {
            break;
        }
    }
    return response;
}

void send_all(int socket_fd, const std::string& data) {
    if (send(socket_fd, data.data(), data.size(), 0) != static_cast<ssize_t>(data.size())) {
        throw std::runtime_error{"failed to send request"};
    }
}

std::string request_plaintext(int port) {
    file_descriptor socket_fd = connect_to_server(port);
    send_all(socket_fd.get(), "GET /plaintext HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");
    return read_response(socket_fd.get());
}

std::string request_plaintext_in_fragments(int port) {
    file_descriptor socket_fd = connect_to_server(port);
    send_all(socket_fd.get(), "GET /plaintext HTTP/1.1\r\n");
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    send_all(socket_fd.get(), "Host: 127.0.0.1\r\n\r\n");
    return read_response(socket_fd.get());
}

std::string request_with_oversized_header(int port) {
    file_descriptor socket_fd = connect_to_server(port);
    const std::string oversized_header(9000, 'a');
    send_all(socket_fd.get(), "GET /plaintext HTTP/1.1\r\nX-Large: " + oversized_header + "\r\n\r\n");
    return read_response(socket_fd.get());
}

class server_process {
public:
    server_process(std::string executable_path, int port) {
        pid_ = fork();
        if (pid_ < 0) {
            throw std::runtime_error{"failed to fork server"};
        }
        if (pid_ == 0) {
            const std::string port_text = std::to_string(port);
            execl(executable_path.c_str(), executable_path.c_str(), "--host", "127.0.0.1", "--port", port_text.c_str(), nullptr);
            _exit(127);
        }
    }

    ~server_process() {
        if (pid_ > 0) {
            kill(pid_, SIGTERM);
            waitpid(pid_, nullptr, 0);
        }
    }

private:
    pid_t pid_{-1};
};

std::string wait_for_plaintext_response(int port) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    while (std::chrono::steady_clock::now() < deadline) {
        try {
            return request_plaintext(port);
        } catch (const std::runtime_error&) {
            std::this_thread::sleep_for(std::chrono::milliseconds{50});
        }
    }
    throw std::runtime_error{"server did not return plaintext response before timeout"};
}

}  // namespace

TEST(PlaintextIntegration, ServerReturnsPlaintextOverTcp) {
    const int port = reserve_port();
    const server_process server{HTTP_SERVER_EXECUTABLE, port};

    const std::string response = wait_for_plaintext_response(port);

    EXPECT_NE(response.find("HTTP/1.1 200 OK\r\n"), std::string::npos);
    EXPECT_NE(response.find("Content-Length: 13\r\n"), std::string::npos);
    EXPECT_NE(response.find("\r\n\r\nHello, World!"), std::string::npos);
}

TEST(PlaintextIntegration, ServerHandlesFragmentedTcpRequest) {
    const int port = reserve_port();
    const server_process server{HTTP_SERVER_EXECUTABLE, port};
    wait_for_plaintext_response(port);

    const std::string response = request_plaintext_in_fragments(port);

    EXPECT_NE(response.find("HTTP/1.1 200 OK\r\n"), std::string::npos);
    EXPECT_NE(response.find("Content-Length: 13\r\n"), std::string::npos);
    EXPECT_NE(response.find("\r\n\r\nHello, World!"), std::string::npos);
}

TEST(PlaintextIntegration, ServerRejectsOversizedHeaders) {
    const int port = reserve_port();
    const server_process server{HTTP_SERVER_EXECUTABLE, port};
    wait_for_plaintext_response(port);

    const std::string response = request_with_oversized_header(port);

    EXPECT_NE(response.find("HTTP/1.1 431 Request Header Fields Too Large\r\n"), std::string::npos);
    EXPECT_NE(response.find("Content-Length: 0\r\n"), std::string::npos);
}
