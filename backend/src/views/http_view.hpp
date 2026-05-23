#pragma once

#include "shared/net_socket.hpp"

#include <string>

class HttpView {
 public:
  void send_response(socket_t client,
                     int status,
                     const std::string& status_text,
                     const std::string& content_type,
                     const std::string& body) const;

  void send_json(socket_t client, int status, const std::string& status_text, const std::string& body) const;
};

