#pragma once

#include "models/app_state.hpp"
#include "shared/net_socket.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

class SharedSyncService {
 public:
  explicit SharedSyncService(AppState& state);

  SharedSyncSummary sync_shared_folder_once();
  std::string sync_shared_folder_json();
  void watch_shared_folder();

  // Returns true when the socket request was a shared protocol and was fully handled.
  bool handle_shared_protocol(socket_t client, const std::string& magic, const std::string& peer);

 private:
  static constexpr std::uint64_t shared_chunk_size_ = 256 * 1024;

  static std::optional<int> parse_int(const std::string& value);
  static std::optional<std::uint64_t> parse_u64(const std::string& value);

  static std::uint64_t fnv1a_update(std::uint64_t hash, const char* data, std::size_t size);
  static std::optional<std::uint64_t> hash_file(const std::filesystem::path& file_path);
  static std::optional<SharedFileSignature> shared_file_signature(const std::filesystem::path& file_path);

  static std::string u64_hex(std::uint64_t value);
  static std::uint64_t shared_chunk_count(std::uint64_t size, std::uint64_t chunk_size);
  static std::uint64_t shared_chunk_bytes(std::uint64_t index,
                                          std::uint64_t total_size,
                                          std::uint64_t chunk_size,
                                          std::uint64_t chunk_count);

  static std::string shared_chunk_key(const std::string& safe_name, const SharedFileSignature& signature);
  std::filesystem::path shared_chunk_root(const std::string& safe_name, const SharedFileSignature& signature) const;
  std::filesystem::path shared_chunk_file(const std::string& safe_name,
                                          const SharedFileSignature& signature,
                                          std::uint64_t index) const;
  std::filesystem::path shared_chunk_temp_file(const std::string& safe_name,
                                               const SharedFileSignature& signature,
                                               std::uint64_t index) const;

  bool shared_chunk_available(const std::string& safe_name,
                              const SharedFileSignature& signature,
                              std::uint64_t chunk_size,
                              std::uint64_t chunk_count,
                              std::uint64_t index) const;
  std::vector<std::uint64_t> shared_missing_chunks(const std::string& safe_name,
                                                   const SharedFileSignature& signature,
                                                   std::uint64_t chunk_size,
                                                   std::uint64_t chunk_count) const;
  bool shared_has_all_chunks(const std::string& safe_name,
                             const SharedFileSignature& signature,
                             std::uint64_t chunk_size,
                             std::uint64_t chunk_count) const;

  static bool send_missing_chunks_response(socket_t socket, const std::vector<std::uint64_t>& missing);
  static std::optional<std::vector<std::uint64_t>> read_missing_chunks_response(socket_t socket);

  static std::optional<transfer::PeerEndpoint> source_peer_from_header(const std::string& host,
                                                                        const std::string& port,
                                                                        int fallback_port);

  bool send_shared_delete_to_peer(const transfer::PeerEndpoint& peer, const std::string& file_name);
  bool send_shared_file_to_peer(const transfer::PeerEndpoint& peer,
                                const std::string& file_name,
                                const std::filesystem::path& file_path,
                                const SharedFileSignature& signature,
                                bool force_probe = false);

  std::map<std::string, SharedFileProbe> scan_shared_folder(const std::filesystem::path& directory) const;
  static std::string shared_sync_summary_json(const SharedSyncSummary& summary);

  bool publish_shared_from_chunks(const std::string& safe_name,
                                  const SharedFileSignature& signature,
                                  std::uint64_t chunk_size,
                                  std::uint64_t chunk_count,
                                  std::string& error_message);

  void handle_incoming_shared_put_chunks(socket_t client,
                                         const std::string& file_name,
                                         const std::string& size_line,
                                         const std::string& hash_line,
                                         const std::string& chunk_size_line,
                                         const std::string& chunk_count_line,
                                         const std::string& peer,
                                         const std::optional<transfer::PeerEndpoint>& source_peer);
  void handle_incoming_shared_delete(socket_t client,
                                     const std::string& file_name,
                                     const std::string& peer,
                                     const std::optional<transfer::PeerEndpoint>& source_peer);
  void handle_incoming_shared_put(socket_t client,
                                  const std::string& file_name,
                                  const std::string& size_line,
                                  const std::string& peer,
                                  const std::optional<transfer::PeerEndpoint>& source_peer);

  AppState& state_;
};
