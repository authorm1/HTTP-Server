#ifndef HTTP_SERVER_HTTP_RESPONSE_H
#define HTTP_SERVER_HTTP_RESPONSE_H

#include <string>

namespace http_server {

std::string build_plaintext_response();
std::string build_request_header_fields_too_large_response();

}  // namespace http_server

#endif  // HTTP_SERVER_HTTP_RESPONSE_H
