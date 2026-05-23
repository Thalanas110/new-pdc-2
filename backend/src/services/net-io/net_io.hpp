#pragma once

#include "shared/net_socket.hpp"

#include <string>

namespace netio {

bool send_all(socket_t socket, const char* data, std::size_t size);
bool send_text(socket_t socket, const std::string& text);
bool recv_exact(socket_t socket, char* data, std::size_t size);
bool read_line(socket_t socket, std::string& line);

socket_t create_listener(const std::string& bind_host, int port);
socket_t connect_to_peer(const std::string& host, int port);

}  // namespace netio
