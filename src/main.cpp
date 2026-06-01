#include "server.h"

#include <signal.h>

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

volatile sig_atomic_t stop_requested = 0;

void handle_signal(int) {
    stop_requested = 1;
}

uint16_t parse_port(const std::string& text) {
    std::size_t parsed_length = 0;
    const int value = std::stoi(text, &parsed_length);
    if (parsed_length != text.size() || value <= 0 || value > 65535) {
        throw std::invalid_argument{"port must be between 1 and 65535"};
    }
    return static_cast<uint16_t>(value);
}

http_server::server_config parse_config(int argc, char* argv[]) {
    http_server::server_config config;
    for (int index = 1; index < argc; ++index) {
        const std::string option{argv[index]};
        if (option == "--host") {
            if (++index >= argc) {
                throw std::invalid_argument{"--host requires a value"};
            }
            config.host = argv[index];
        } else if (option == "--port") {
            if (++index >= argc) {
                throw std::invalid_argument{"--port requires a value"};
            }
            config.port = parse_port(argv[index]);
        } else {
            throw std::invalid_argument{"unknown option: " + option};
        }
    }
    return config;
}

}  // namespace

int main(int argc, char* argv[]) {
    try {
        signal(SIGPIPE, SIG_IGN);
        signal(SIGINT, handle_signal);
        signal(SIGTERM, handle_signal);
        const http_server::server_config config = parse_config(argc, argv);
        http_server::server app{config, [] { return stop_requested != 0; }};
        app.run();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
