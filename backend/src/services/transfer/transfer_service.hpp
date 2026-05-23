#pragma once

#include "models/app_state.hpp"
#include "services/file-vault/file_vault_service.hpp"
#include "services/shared-sync/shared_sync_service.hpp"
#include "shared/net_socket.hpp"

#include <optional>
#include <string>
#include <vector>

class TransferService {
 public:
  TransferService(AppState& state, SharedSyncService& shared_sync_service);

  void transfer_listener();
  void send_file_to_peer(socket_t client,
                         const std::string& host,
                         int port,
                         const std::string& file_name,
                         const std::vector<char>& body);

 private:
  static std::optional<int> parse_int(const std::string& value);

  void handle_incoming_transfer(socket_t client, const std::string& peer);

  AppState& state_;
  SharedSyncService& shared_sync_service_;
};

