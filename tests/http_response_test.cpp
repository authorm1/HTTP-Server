#include "http_response.h"

#include <gtest/gtest.h>

#include <string>

TEST(HttpResponse, BuildsPlaintextResponseWithStatusLengthAndBody) {
    const std::string response = http_server::build_plaintext_response();

    EXPECT_NE(response.find("HTTP/1.1 200 OK\r\n"), std::string::npos);
    EXPECT_NE(response.find("Content-Type: text/plain\r\n"), std::string::npos);
    EXPECT_NE(response.find("Content-Length: 13\r\n"), std::string::npos);
    EXPECT_NE(response.find("\r\n\r\nHello, World!"), std::string::npos);
}
