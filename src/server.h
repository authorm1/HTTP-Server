#ifndef HTTP_SERVER_SERVER_H
#define HTTP_SERVER_SERVER_H

#include <cstdint>
#include <functional>
#include <string>

namespace http_server {

struct server_config {
    std::string host{"127.0.0.1"};
    uint16_t port{8080};
};

class server {
public:
    server(server_config config, std::function<bool()> should_stop);

    void run();

private:
    server_config config_;
    std::function<bool()> should_stop_;
};

}  // namespace http_server

#endif  // HTTP_SERVER_SERVER_H
