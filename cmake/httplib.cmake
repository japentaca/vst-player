# Fetches cpp-httplib (header-only HTTP + WebSocket server) for the local API (stage 7).
# Plain HTTP on loopback only: TLS, compression and every optional dependency are disabled.
include(FetchContent)

set(VSTPLAYER_HTTPLIB_TAG "v0.64.0" CACHE STRING "cpp-httplib git tag to fetch")

set(HTTPLIB_USE_OPENSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_WOLFSSL_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_MBEDTLS_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_INSTALL OFF CACHE BOOL "" FORCE)
set(HTTPLIB_TEST OFF CACHE BOOL "" FORCE)

FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        ${VSTPLAYER_HTTPLIB_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   TRUE
    SYSTEM)

FetchContent_MakeAvailable(httplib)
