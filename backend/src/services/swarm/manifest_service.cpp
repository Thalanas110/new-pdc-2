#include "services/swarm/manifest_service.hpp"

#include "core/transfer_core.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace {

std::uint64_t hash_piece(const std::vector<char>& bytes, std::size_t count) {
  std::uint64_t hash = 14695981039346656037ull;
  for (std::size_t index = 0; index < count; ++index) {
    hash ^= static_cast<unsigned char>(bytes[index]);
    hash *= 1099511628211ull;
  }
  return hash;
}

std::string now_stamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm local_time{};
#ifdef _WIN32
  localtime_s(&local_time, &time);
#else
  localtime_r(&time, &local_time);
#endif
  std::ostringstream out;
  out << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S");
  return out.str();
}

std::string hash_hex(std::uint64_t value) {
  std::ostringstream out;
  out << std::hex << std::setfill('0') << std::setw(16) << value;
  return out.str();
}

}  // namespace

std::optional<TorrentManifest> ManifestService::build_manifest(
    const std::filesystem::path& file_path,
    const std::string& display_name,
    const std::string& publisher_node_id) const {
  std::ifstream input(file_path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }

  std::error_code file_size_error;
  const auto file_size = std::filesystem::file_size(file_path, file_size_error);
  if (file_size_error) {
    return std::nullopt;
  }

  TorrentManifest manifest;
  manifest.display_name = display_name;
  manifest.publisher_node_id = publisher_node_id;
  manifest.created_at = now_stamp();
  manifest.file_size = static_cast<std::uint64_t>(file_size);

  std::vector<char> buffer(static_cast<std::size_t>(manifest.piece_size));
  std::uint64_t total_read = 0;
  while (true) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto read = static_cast<std::size_t>(input.gcount());
    total_read += static_cast<std::uint64_t>(read);

    if (read > 0) {
      manifest.piece_hashes.push_back(hash_piece(buffer, read));
    }

    if (input.bad()) {
      return std::nullopt;
    }

    if (input.fail() && !input.eof()) {
      return std::nullopt;
    }

    if (read == 0) {
      break;
    }
  }

  if (total_read != manifest.file_size) {
    return std::nullopt;
  }

  manifest.piece_count = static_cast<std::uint64_t>(manifest.piece_hashes.size());
  manifest.torrent_id = transfer::make_transfer_id();
  return manifest;
}

std::string ManifestService::manifest_json(const TorrentManifest& manifest) const {
  std::ostringstream out;
  out << '{'
      << "\"torrent_id\":\"" << transfer::json_escape(manifest.torrent_id) << "\","
      << "\"display_name\":\"" << transfer::json_escape(manifest.display_name) << "\","
      << "\"publisher_node_id\":\"" << transfer::json_escape(manifest.publisher_node_id) << "\","
      << "\"file_size\":" << manifest.file_size << ','
      << "\"piece_size\":" << manifest.piece_size << ','
      << "\"piece_count\":" << manifest.piece_count << ','
      << "\"piece_hashes\":[";

  for (std::size_t index = 0; index < manifest.piece_hashes.size(); ++index) {
    if (index > 0) {
      out << ',';
    }
    out << '"' << hash_hex(manifest.piece_hashes[index]) << '"';
  }

  out << "],"
      << "\"created_at\":\"" << transfer::json_escape(manifest.created_at) << "\""
      << '}';
  return out.str();
}
