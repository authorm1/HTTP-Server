#include "http_response.h"

#include <string>

namespace http_server {

std::string build_plaintext_response() {
    const std::string body{"Hello, World!"};
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: text/plain\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "Connection: close\r\n"
           "\r\n" + body;
}

std::string build_request_header_fields_too_large_response() {
    return "HTTP/1.1 431 Request Header Fields Too Large\r\n"
           "Content-Length: 0\r\n"
           "Connection: close\r\n"
           "\r\n";
}

}  // namespace http_server
