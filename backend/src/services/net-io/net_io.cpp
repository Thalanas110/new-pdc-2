#include "services/net-io/net_io.hpp"

#include <algorithm>

namespace netio {

bool send_all(socket_t socket, const char* data, std::size_t size) {
  std::size_t sent = 0;
  while (sent < size) {
    const auto chunk = static_cast<int>(std::min<std::size_t>(size - sent, 64 * 1024));
    const int result = send(socket, data + sent, chunk, 0);
    if (result <= 0) {
      return false;
    }
    sent += static_cast<std::size_t>(result);
  }
  return true;
}

bool send_text(socket_t socket, const std::string& text) {
  return send_all(socket, text.data(), text.size());
}

bool recv_exact(socket_t socket, char* data, std::size_t size) {
  std::size_t received = 0;
  while (received < size) {
    const auto chunk = static_cast<int>(std::min<std::size_t>(size - received, 64 * 1024));
    const int result = recv(socket, data + received, chunk, 0);
    if (result <= 0) {
      return false;
    }
    received += static_cast<std::size_t>(result);
  }
  return true;
}

bool read_line(socket_t socket, std::string& line) {
  line.clear();
  char ch = '\0';
  while (line.size() < 4096) {
    const int received = recv(socket, &ch, 1, 0);
    if (received <= 0) {
      return false;
    }
    if (ch == '\n') {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      return true;
    }
    line.push_back(ch);
  }
  return false;
}

socket_t create_listener(const std::string& bind_host, int port) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;
  hints.ai_flags = AI_PASSIVE;

  addrinfo* results = nullptr;
  const std::string port_value = std::to_string(port);
  if (getaddrinfo(bind_host.c_str(), port_value.c_str(), &hints, &results) != 0) {
    return invalid_socket;
  }

  socket_t listener = invalid_socket;
  for (addrinfo* candidate = results; candidate != nullptr; candidate = candidate->ai_next) {
    listener = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
    if (listener == invalid_socket) {
      continue;
    }

    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    if (bind(listener, candidate->ai_addr, static_cast<int>(candidate->ai_addrlen)) == 0) {
      break;
    }

    close_socket(listener);
    listener = invalid_socket;
  }

  freeaddrinfo(results);

  if (listener == invalid_socket) {
    return invalid_socket;
  }

  if (listen(listener, SOMAXCONN) != 0) {
    close_socket(listener);
    return invalid_socket;
  }

  return listener;
}

socket_t connect_to_peer(const std::string& host, int port) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  addrinfo* results = nullptr;
  const std::string port_value = std::to_string(port);
  if (getaddrinfo(host.c_str(), port_value.c_str(), &hints, &results) != 0) {
    return invalid_socket;
  }

  socket_t connected = invalid_socket;
  for (addrinfo* candidate = results; candidate != nullptr; candidate = candidate->ai_next) {
    socket_t peer = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
    if (peer == invalid_socket) {
      continue;
    }
    if (connect(peer, candidate->ai_addr, static_cast<int>(candidate->ai_addrlen)) == 0) {
      connected = peer;
      break;
    }
    close_socket(peer);
  }

  freeaddrinfo(results);
  return connected;
}

}  // namespace netio

