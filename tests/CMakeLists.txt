include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    URL_HASH SHA256=1f357c27ca988c3f7c6b4bf68a9395005ac6761f034046e9dde0896e3aba00e4
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(http_response_test http_response_test.cpp)
target_link_libraries(http_response_test PRIVATE http_server_core GTest::gtest_main)

add_executable(plaintext_integration_test plaintext_integration_test.cpp)
target_link_libraries(plaintext_integration_test PRIVATE GTest::gtest_main)
target_compile_definitions(plaintext_integration_test PRIVATE
    HTTP_SERVER_EXECUTABLE="$<TARGET_FILE:http_server>"
)

include(GoogleTest)
gtest_discover_tests(http_response_test)
gtest_discover_tests(plaintext_integration_test)
