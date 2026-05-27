#pragma once

#include "shared/http_types.hpp"
#include "views/http_view.hpp"
#include "shared/net_socket.hpp"
#include "core/transfer_core.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

class HttpController {
 public:
  struct Dependencies {
    std::function<std::string()> status_json;
    std::function<std::string()> library_json;
    std::function<std::string()> downloads_json;
    std::function<void(socket_t, const std::string&, const std::vector<char>&)> publish_file;
    std::function<bool(const transfer::PeerEndpoint&)> bootstrap_peer;
    std::function<void(const std::string&)> start_download;
    std::function<int()> transfer_port;
    std::function<bool(const std::string&)> is_allowed_peer;
  };

  HttpController(HttpView view, Dependencies deps);

  void handle_client(socket_t client) const;

 private:
  bool read_http_request(socket_t client, HttpRequest& request) const;

  static std::string lower_copy(std::string value);
  static std::optional<int> parse_int(const std::string& value);
  static std::string header_value(const HttpRequest& request, const std::string& name, const std::string& fallback = "");
  static std::string url_decode(const std::string& input);
  static std::string query_value(const std::string& target, const std::string& key);
  static std::string path_only(const std::string& target);

  HttpView view_;
  Dependencies deps_;
};

