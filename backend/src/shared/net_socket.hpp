#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
using socket_length_t = int;
constexpr socket_t invalid_socket = INVALID_SOCKET;
inline void close_socket(socket_t socket_handle) { closesocket(socket_handle); }
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
using socket_length_t = socklen_t;
constexpr socket_t invalid_socket = -1;
inline void close_socket(socket_t socket_handle) { close(socket_handle); }
#endif
