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
    std::function<std::optional<std::string>(const std::string&)> file_list_json;
    std::function<void(socket_t, const std::string&, const std::string&)> send_file_inline;
    std::function<void(socket_t, const std::string&, int, const std::string&, const std::vector<char>&)> send_file_to_peer;
    std::function<std::string()> sync_shared_folder_json;
    std::function<void(socket_t, const std::string&, const std::vector<char>&)> save_shared_upload;
    std::function<int()> transfer_port;
    std::function<bool()> listener_active;
    std::function<void(const transfer::PeerEndpoint&)> add_sync_peer;
    std::function<void(const transfer::PeerEndpoint&)> remove_sync_peer;
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

