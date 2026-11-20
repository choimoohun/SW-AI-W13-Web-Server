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
