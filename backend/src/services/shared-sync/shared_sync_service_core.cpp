#include "services/shared-sync/shared_sync_service.hpp"

#include "core/transfer_core.hpp"
#include "services/net-io/net_io.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>

SharedSyncService::SharedSyncService(AppState& state) : state_(state) {}

std::optional<int> SharedSyncService::parse_int(const std::string& value) {
  int parsed = 0;
  const auto* begin = value.data();
  const auto* end = value.data() + value.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end) {
    return std::nullopt;
  }
  return parsed;
}

std::optional<std::uint64_t> SharedSyncService::parse_u64(const std::string& value) {
  std::uint64_t parsed = 0;
  const auto* begin = value.data();
  const auto* end = value.data() + value.size();
  const auto result = std::from_chars(begin, end, parsed);
  if (result.ec != std::errc{} || result.ptr != end) {
    return std::nullopt;
  }
  return parsed;
}

std::uint64_t SharedSyncService::fnv1a_update(std::uint64_t hash, const char* data, std::size_t size) {
  constexpr std::uint64_t prime = 1099511628211ull;
  for (std::size_t index = 0; index < size; ++index) {
    hash ^= static_cast<unsigned char>(data[index]);
    hash *= prime;
  }
  return hash;
}

std::optional<std::uint64_t> SharedSyncService::hash_file(const std::filesystem::path& file_path) {
  std::ifstream input(file_path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }

  std::uint64_t hash = 14695981039346656037ull;
  std::vector<char> buffer(64 * 1024);
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read = input.gcount();
    if (read > 0) {
      hash = fnv1a_update(hash, buffer.data(), static_cast<std::size_t>(read));
    }
  }
  return hash;
}

std::optional<SharedFileSignature> SharedSyncService::shared_file_signature(const std::filesystem::path& file_path) {
  std::error_code error;
  const auto size = std::filesystem::file_size(file_path, error);
  if (error) {
    return std::nullopt;
  }

  const auto hash = hash_file(file_path);
  if (!hash) {
    return std::nullopt;
  }

  return SharedFileSignature{static_cast<std::uint64_t>(size), *hash};
}

std::string SharedSyncService::u64_hex(std::uint64_t value) {
  std::ostringstream out;
  out << std::hex << std::nouppercase << value;
  return out.str();
}

std::uint64_t SharedSyncService::shared_chunk_count(std::uint64_t size, std::uint64_t chunk_size) {
  if (size == 0 || chunk_size == 0) {
    return 0;
  }
  return (size + chunk_size - 1) / chunk_size;
}

std::uint64_t SharedSyncService::shared_chunk_bytes(std::uint64_t index,
                                                    std::uint64_t total_size,
                                                    std::uint64_t chunk_size,
                                                    std::uint64_t chunk_count) {
  if (chunk_size == 0 || index >= chunk_count) {
    return 0;
  }
  const std::uint64_t offset = index * chunk_size;
  if (offset >= total_size) {
    return 0;
  }
  return std::min<std::uint64_t>(chunk_size, total_size - offset);
}

std::string SharedSyncService::shared_chunk_key(const std::string& safe_name, const SharedFileSignature& signature) {
  return safe_name + "-" + std::to_string(signature.size) + "-" + u64_hex(signature.hash);
}

std::filesystem::path SharedSyncService::shared_chunk_root(const std::string& safe_name,
                                                           const SharedFileSignature& signature) const {
  return state_.shared_dir / ".loopline-chunks" / shared_chunk_key(safe_name, signature);
}

std::filesystem::path SharedSyncService::shared_chunk_file(const std::string& safe_name,
                                                           const SharedFileSignature& signature,
                                                           std::uint64_t index) const {
  std::ostringstream file_name;
  file_name << "chunk-" << std::setw(6) << std::setfill('0') << index << ".part";
  return shared_chunk_root(safe_name, signature) / file_name.str();
}

std::filesystem::path SharedSyncService::shared_chunk_temp_file(const std::string& safe_name,
                                                                const SharedFileSignature& signature,
                                                                std::uint64_t index) const {
  std::ostringstream file_name;
  file_name << "chunk-" << std::setw(6) << std::setfill('0') << index << ".tmp";
  return shared_chunk_root(safe_name, signature) / file_name.str();
}

bool SharedSyncService::shared_chunk_available(const std::string& safe_name,
                                               const SharedFileSignature& signature,
                                               std::uint64_t chunk_size,
                                               std::uint64_t chunk_count,
                                               std::uint64_t index) const {
  const std::filesystem::path chunk_path = shared_chunk_file(safe_name, signature, index);
  std::error_code error;
  if (!std::filesystem::exists(chunk_path, error) || error) {
    return false;
  }

  const auto size = std::filesystem::file_size(chunk_path, error);
  if (error) {
    return false;
  }
  return size == shared_chunk_bytes(index, signature.size, chunk_size, chunk_count);
}

std::vector<std::uint64_t> SharedSyncService::shared_missing_chunks(const std::string& safe_name,
                                                                    const SharedFileSignature& signature,
                                                                    std::uint64_t chunk_size,
                                                                    std::uint64_t chunk_count) const {
  const auto existing_signature = shared_file_signature(state_.shared_dir / safe_name);
  if (existing_signature && *existing_signature == signature) {
    return {};
  }

  std::vector<std::uint64_t> missing;
  for (std::uint64_t index = 0; index < chunk_count; ++index) {
    if (!shared_chunk_available(safe_name, signature, chunk_size, chunk_count, index)) {
      missing.push_back(index);
    }
  }
  return missing;
}

bool SharedSyncService::shared_has_all_chunks(const std::string& safe_name,
                                              const SharedFileSignature& signature,
                                              std::uint64_t chunk_size,
                                              std::uint64_t chunk_count) const {
  const auto existing_signature = shared_file_signature(state_.shared_dir / safe_name);
  if (existing_signature && *existing_signature == signature) {
    return true;
  }

  for (std::uint64_t index = 0; index < chunk_count; ++index) {
    if (!shared_chunk_available(safe_name, signature, chunk_size, chunk_count, index)) {
      return false;
    }
  }
  return true;
}

bool SharedSyncService::send_missing_chunks_response(socket_t socket, const std::vector<std::uint64_t>& missing) {
  std::ostringstream out;
  out << "MISSING " << missing.size() << "\n";
  for (const std::uint64_t index : missing) {
    out << index << '\n';
  }
  out << "\n";
  return netio::send_text(socket, out.str());
}

std::optional<std::vector<std::uint64_t>> SharedSyncService::read_missing_chunks_response(socket_t socket) {
  std::string header;
  if (!netio::read_line(socket, header)) {
    return std::nullopt;
  }

  const std::string prefix = "MISSING ";
  if (header.rfind(prefix, 0) != 0) {
    return std::nullopt;
  }

  const auto count = parse_u64(header.substr(prefix.size()));
  if (!count) {
    return std::nullopt;
  }

  std::vector<std::uint64_t> requested;
  requested.reserve(static_cast<std::size_t>(*count));
  for (std::uint64_t index = 0; index < *count; ++index) {
    std::string line;
    if (!netio::read_line(socket, line)) {
      return std::nullopt;
    }
    const auto parsed_index = parse_u64(line);
    if (!parsed_index) {
      return std::nullopt;
    }
    requested.push_back(*parsed_index);
  }

  std::string blank;
  if (!netio::read_line(socket, blank)) {
    return std::nullopt;
  }

  return requested;
}

std::optional<transfer::PeerEndpoint> SharedSyncService::source_peer_from_header(const std::string& host,
                                                                                  const std::string& port,
                                                                                  int fallback_port) {
  if (host.empty()) {
    return std::nullopt;
  }
  const std::string endpoint = host + ":" + (port.empty() ? std::to_string(fallback_port) : port);
  return transfer::parse_peer_endpoint(endpoint, fallback_port);
}

