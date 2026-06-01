#include "server.h"

#include "http_response.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace http_server {
namespace {

constexpr int max_events = 16;
constexpr int listen_backlog = 128;
constexpr int epoll_timeout_milliseconds = 100;
constexpr std::size_t read_buffer_size = 4096;
constexpr std::size_t max_request_header_size = 8192;
constexpr std::size_t max_connections = 1024;
constexpr auto idle_read_timeout = std::chrono::seconds{5};

class file_descriptor {
public:
    explicit file_descriptor(int value = -1) noexcept : value_{value} {}
    ~file_descriptor() noexcept {
        if (value_ >= 0) {
            close(value_);
        }
    }

    file_descriptor(const file_descriptor&) = delete;
    file_descriptor& operator=(const file_descriptor&) = delete;

    file_descriptor(file_descriptor&& other) noexcept : value_{std::exchange(other.value_, -1)} {}

    file_descriptor& operator=(file_descriptor&& other) noexcept {
        if (this != &other) {
            if (value_ >= 0) {
                close(value_);
            }
            value_ = std::exchange(other.value_, -1);
        }
        return *this;
    }

    int get() const noexcept { return value_; }

private:
    int value_;
};

struct connection {
    file_descriptor fd;
    std::string input;
    std::string output;
    std::size_t bytes_written{0};
    std::chrono::steady_clock::time_point last_activity{std::chrono::steady_clock::now()};
};

void throw_system_error(const std::string& message) {
    throw std::runtime_error{message + ": " + std::strerror(errno)};
}

void set_non_blocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        throw_system_error("fcntl F_GETFL failed");
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        throw_system_error("fcntl F_SETFL failed");
    }
}

file_descriptor create_listener(const server_config& config) {
    file_descriptor listener{socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)};
    if (listener.get() < 0) {
        throw_system_error("socket failed");
    }

    const int reuse_address = 1;
    if (setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address)) < 0) {
        throw_system_error("setsockopt SO_REUSEADDR failed");
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config.port);
    if (inet_pton(AF_INET, config.host.c_str(), &address.sin_addr) != 1) {
        throw std::invalid_argument{"host must be an IPv4 address"};
    }

    if (bind(listener.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
        throw_system_error("bind failed");
    }
    if (listen(listener.get(), listen_backlog) < 0) {
        throw_system_error("listen failed");
    }

    return listener;
}

void add_epoll_interest(int epoll_fd, int fd, uint32_t events) {
    epoll_event event{};
    event.events = events;
    event.data.fd = fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event) < 0) {
        throw_system_error("epoll_ctl add failed");
    }
}

void modify_epoll_interest(int epoll_fd, int fd, uint32_t events) {
    epoll_event event{};
    event.events = events;
    event.data.fd = fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event) < 0) {
        throw_system_error("epoll_ctl modify failed");
    }
}

bool read_request(connection& client) {
    std::array<char, read_buffer_size> buffer{};
    while (true) {
        const ssize_t bytes_read = recv(client.fd.get(), buffer.data(), buffer.size(), 0);
        if (bytes_read > 0) {
            client.last_activity = std::chrono::steady_clock::now();
            client.input.append(buffer.data(), static_cast<std::size_t>(bytes_read));
            if (client.input.size() > max_request_header_size) {
                client.output = build_request_header_fields_too_large_response();
                return true;
            }
            if (client.input.find("\r\n\r\n") != std::string::npos) {
                client.output = build_plaintext_response();
                return true;
            }
            continue;
        }
        if (bytes_read == 0) {
            return false;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }
        if (errno == EINTR) {
            continue;
        }
        return false;
    }
}

bool write_response(connection& client) {
    while (client.bytes_written < client.output.size()) {
        const ssize_t bytes_sent = send(
            client.fd.get(),
            client.output.data() + client.bytes_written,
            client.output.size() - client.bytes_written,
            MSG_NOSIGNAL);
        if (bytes_sent > 0) {
            client.bytes_written += static_cast<std::size_t>(bytes_sent);
            continue;
        }
        if (bytes_sent < 0 && errno == EINTR) {
            continue;
        }
        if (bytes_sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return true;
        }
        return false;
    }
    return false;
}

void close_client(int epoll_fd, std::unordered_map<int, connection>& clients, int client_fd) {
    if (epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_fd, nullptr) < 0 && errno != EBADF && errno != ENOENT) {
        throw_system_error("epoll_ctl delete failed");
    }
    clients.erase(client_fd);
}

void accept_clients(int epoll_fd, int listener_fd, std::unordered_map<int, connection>& clients) {
    while (true) {
        sockaddr_in client_address{};
        socklen_t client_address_length = sizeof(client_address);
        file_descriptor client{accept4(
            listener_fd,
            reinterpret_cast<sockaddr*>(&client_address),
            &client_address_length,
            SOCK_NONBLOCK | SOCK_CLOEXEC)};
        if (client.get() < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            throw_system_error("accept4 failed");
        }
        if (clients.size() >= max_connections) {
            continue;
        }
        set_non_blocking(client.get());
        const int client_fd = client.get();
        add_epoll_interest(epoll_fd, client_fd, EPOLLIN | EPOLLONESHOT);
        clients.emplace(client_fd, connection{std::move(client), {}, {}, 0, std::chrono::steady_clock::now()});
    }
}

void close_idle_clients(int epoll_fd, std::unordered_map<int, connection>& clients) {
    const auto now = std::chrono::steady_clock::now();
    std::vector<int> idle_fds;
    for (const auto& [fd, client] : clients) {
        if (client.output.empty() && now - client.last_activity > idle_read_timeout) {
            idle_fds.push_back(fd);
        }
    }
    for (const int fd : idle_fds) {
        close_client(epoll_fd, clients, fd);
    }
}

}  // namespace

server::server(server_config config, std::function<bool()> should_stop)
    : config_{std::move(config)}, should_stop_{std::move(should_stop)} {}

void server::run() {
    const file_descriptor listener = create_listener(config_);
    const file_descriptor epoll_fd{epoll_create1(EPOLL_CLOEXEC)};
    if (epoll_fd.get() < 0) {
        throw_system_error("epoll_create1 failed");
    }

    add_epoll_interest(epoll_fd.get(), listener.get(), EPOLLIN);

    std::unordered_map<int, connection> clients;
    std::array<epoll_event, max_events> events{};
    while (!should_stop_()) {
        const int event_count = epoll_wait(epoll_fd.get(), events.data(), events.size(), epoll_timeout_milliseconds);
        if (event_count < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw_system_error("epoll_wait failed");
        }

        close_idle_clients(epoll_fd.get(), clients);

        for (int index = 0; index < event_count; ++index) {
            const int ready_fd = events[static_cast<std::size_t>(index)].data.fd;
            if (ready_fd == listener.get()) {
                accept_clients(epoll_fd.get(), listener.get(), clients);
                continue;
            }

            auto client = clients.find(ready_fd);
            if (client == clients.end()) {
                continue;
            }

            if (client->second.output.empty()) {
                if (!read_request(client->second)) {
                    close_client(epoll_fd.get(), clients, ready_fd);
                    continue;
                }
                if (client->second.output.empty()) {
                    modify_epoll_interest(epoll_fd.get(), ready_fd, EPOLLIN | EPOLLONESHOT);
                    continue;
                }
            }

            if (write_response(client->second)) {
                modify_epoll_interest(epoll_fd.get(), ready_fd, EPOLLOUT | EPOLLONESHOT);
            } else {
                close_client(epoll_fd.get(), clients, ready_fd);
            }
        }
    }
}

}  // namespace http_server
