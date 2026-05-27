#pragma once

#include "models/app_state.hpp"
#include "shared/net_socket.hpp"
#include "views/http_view.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class FileVaultService {
 public:
  explicit FileVaultService(AppState& state);

  std::optional<std::string> file_list_json(const std::string& kind) const;
  void send_file_inline(socket_t client, const std::string& kind, const std::string& raw_name) const;
  void send_file_attachment(socket_t client, const std::string& kind, const std::string& raw_name) const;
  void save_shared_upload(socket_t client, const std::string& file_name, const std::vector<char>& body);

  static std::filesystem::path unique_received_path(const std::filesystem::path& directory,
                                                    const std::string& file_name);

 private:
  static std::string url_encode(const std::string& input);
  void send_file_with_disposition(socket_t client,
                                  const std::string& kind,
                                  const std::string& raw_name,
                                  const std::string& disposition) const;

  AppState& state_;
  HttpView view_;
};
