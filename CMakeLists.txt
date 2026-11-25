cmake_minimum_required(VERSION 3.16)
project(web_server LANGUAGES C)

if(NOT UNIX)
    message(FATAL_ERROR "A POSIX environment is required. On Windows, use WSL2 or Docker Desktop in Linux container mode.")
endif()

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
add_compile_definitions(_POSIX_C_SOURCE=200809L)

include(CheckSymbolExists)
set(CMAKE_REQUIRED_DEFINITIONS -D_POSIX_C_SOURCE=200809L)
check_symbol_exists(socket "sys/socket.h" WEB_SERVER_HAS_POSIX_SOCKET)
unset(CMAKE_REQUIRED_DEFINITIONS)
if(NOT WEB_SERVER_HAS_POSIX_SOCKET)
    message(FATAL_ERROR "POSIX socket() is required.")
endif()

include(CTest)

find_package(Threads REQUIRED)

add_library(csapp STATIC src/csapp.c)
target_include_directories(csapp PUBLIC src)
target_link_libraries(csapp PUBLIC Threads::Threads)

add_executable(echoserveri src/echoserveri.c src/echo.c)
target_link_libraries(echoserveri PRIVATE csapp)

add_executable(echoclient src/echoclient.c)
target_link_libraries(echoclient PRIVATE csapp)

add_library(http STATIC src/http_request.c src/http_response.c src/static_file.c src/cgi.c src/conn_queue.c)
target_link_libraries(http PUBLIC csapp)

add_executable(httpd src/httpd.c)
target_link_libraries(httpd PRIVATE http)

foreach(name test_http_request test_http_response test_static_file test_cgi test_conn_queue)
    add_executable(${name} tests/${name}.c)
    target_link_libraries(${name} PRIVATE http)
    add_test(NAME ${name} COMMAND ${name})
endforeach()
