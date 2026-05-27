#include "services/swarm/piece_store_service.hpp"

#include "core/transfer_core.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <sstream>
#include <string>
#include <utility>

namespace {

constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

std::uint64_t fnv1a_bytes(const char* data, std::size_t size) {
  std::uint64_t hash = kFnvOffsetBasis;
  for (std::size_t index = 0; index < size; ++index) {
    hash ^= static_cast<unsigned char>(data[index]);
    hash *= kFnvPrime;
  }
  return hash;
}

std::uint64_t fnv1a_bytes(const std::vector<char>& bytes) {
  return fnv1a_bytes(bytes.data(), bytes.size());
}

std::string hex64(std::uint64_t value) {
  std::ostringstream out;
  out << std::hex << std::nouppercase << std::setw(16) << std::setfill('0') << value;
  return out.str();
}

std::filesystem::path safe_manifest_component(const std::string& torrent_id) {
  return std::filesystem::path("torrent-" + hex64(fnv1a_bytes(torrent_id.data(), torrent_id.size())));
}

bool piece_index_in_bounds(const TorrentManifest& manifest, std::uint64_t piece_index) {
  return piece_index < manifest.piece_count;
}

std::uint64_t expected_piece_size(const TorrentManifest& manifest, std::uint64_t piece_index) {
  if (!piece_index_in_bounds(manifest, piece_index)) {
    return 0;
  }

  if (manifest.piece_count == 0) {
    return 0;
  }

  if (piece_index + 1 < manifest.piece_count) {
    return manifest.piece_size;
  }

  const std::uint64_t consumed = manifest.piece_size * (manifest.piece_count - 1);
  return manifest.file_size >= consumed ? (manifest.file_size - consumed) : 0;
}

bool read_file_bytes(const std::filesystem::path& path, std::vector<char>& bytes_out) {
  std::error_code size_error;
  const auto size = std::filesystem::file_size(path, size_error);
  if (size_error) {
    return false;
  }

  bytes_out.resize(static_cast<std::size_t>(size));
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return false;
  }

  if (bytes_out.empty()) {
    return true;
  }

  if (!bytes_out.empty()) {
    input.read(bytes_out.data(), static_cast<std::streamsize>(bytes_out.size()));
  }

  return static_cast<bool>(input) || (input.eof() && !input.fail());
}

bool piece_hash_matches(const TorrentManifest& manifest,
                        std::uint64_t piece_index,
                        const std::vector<char>& bytes) {
  if (piece_index >= manifest.piece_hashes.size()) {
    return false;
  }
  return manifest.piece_hashes[piece_index] == fnv1a_bytes(bytes);
}

}  // namespace

PieceStoreService::PieceStoreService(std::filesystem::path root) : root_(std::move(root)) {}

std::filesystem::path PieceStoreService::piece_dir(const TorrentManifest& manifest) const {
  return root_ / "pieces" / safe_manifest_component(manifest.torrent_id);
}

std::filesystem::path PieceStoreService::piece_path(const TorrentManifest& manifest, std::uint64_t piece_index) const {
  std::ostringstream name;
  name << "piece-" << std::setw(6) << std::setfill('0') << piece_index << ".bin";
  return piece_dir(manifest) / name.str();
}

std::filesystem::path PieceStoreService::assembled_file_path(const TorrentManifest& manifest) const {
  const std::string safe_name = transfer::safe_file_name(manifest.display_name);
  return root_ / "files" / (safe_manifest_component(manifest.torrent_id).string() + "-" + safe_name);
}

bool PieceStoreService::store_piece(const TorrentManifest& manifest,
                                    std::uint64_t piece_index,
                                    const std::vector<char>& bytes) {
  if (!piece_index_in_bounds(manifest, piece_index)) {
    return false;
  }

  if (bytes.size() != static_cast<std::size_t>(expected_piece_size(manifest, piece_index))) {
    return false;
  }

  if (!piece_hash_matches(manifest, piece_index, bytes)) {
    return false;
  }

  std::error_code directory_error;
  std::filesystem::create_directories(piece_dir(manifest), directory_error);
  if (directory_error) {
    return false;
  }

  const auto final_path = piece_path(manifest, piece_index);
  const auto temp_path = final_path.string() + ".tmp";

  {
    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output) {
      return false;
    }

    if (!bytes.empty()) {
      output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    if (!output) {
      std::error_code cleanup_error;
      std::filesystem::remove(temp_path, cleanup_error);
      return false;
    }
  }

  std::error_code remove_error;
  std::filesystem::remove(final_path, remove_error);
  if (remove_error) {
    std::error_code cleanup_error;
    std::filesystem::remove(temp_path, cleanup_error);
    return false;
  }

  std::error_code rename_error;
  std::filesystem::rename(temp_path, final_path, rename_error);
  if (rename_error) {
    std::error_code cleanup_error;
    std::filesystem::remove(temp_path, cleanup_error);
    return false;
  }

  return true;
}

bool PieceStoreService::has_piece(const TorrentManifest& manifest, std::uint64_t piece_index) const {
  if (!piece_index_in_bounds(manifest, piece_index)) {
    return false;
  }

  const auto path = piece_path(manifest, piece_index);
  std::error_code exists_error;
  if (!std::filesystem::exists(path, exists_error) || exists_error) {
    return false;
  }

  const auto expected_size = expected_piece_size(manifest, piece_index);
  std::error_code size_error;
  const auto actual_size = std::filesystem::file_size(path, size_error);
  if (size_error || actual_size != expected_size) {
    return false;
  }

  std::vector<char> bytes;
  if (!read_file_bytes(path, bytes)) {
    return false;
  }

  return piece_hash_matches(manifest, piece_index, bytes);
}

std::optional<std::vector<char>> PieceStoreService::load_piece(const TorrentManifest& manifest,
                                                               std::uint64_t piece_index) const {
  if (!has_piece(manifest, piece_index)) {
    return std::nullopt;
  }

  std::vector<char> bytes;
  if (!read_file_bytes(piece_path(manifest, piece_index), bytes)) {
    return std::nullopt;
  }
  return bytes;
}

std::vector<std::uint64_t> PieceStoreService::missing_pieces(const TorrentManifest& manifest) const {
  std::vector<std::uint64_t> missing;
  for (std::uint64_t index = 0; index < manifest.piece_count; ++index) {
    if (!has_piece(manifest, index)) {
      missing.push_back(index);
    }
  }
  return missing;
}

std::optional<std::filesystem::path> PieceStoreService::assembled_file_if_present(const TorrentManifest& manifest) const {
  const auto path = assembled_file_path(manifest);
  std::error_code exists_error;
  if (!std::filesystem::exists(path, exists_error) || exists_error) {
    return std::nullopt;
  }

  std::error_code size_error;
  const auto size = std::filesystem::file_size(path, size_error);
  if (size_error || size != manifest.file_size) {
    return std::nullopt;
  }

  return path;
}

std::optional<std::filesystem::path> PieceStoreService::assemble_file(const TorrentManifest& manifest) {
  if (!missing_pieces(manifest).empty()) {
    return std::nullopt;
  }

  std::error_code directory_error;
  const auto final_path = assembled_file_path(manifest);
  std::filesystem::create_directories(final_path.parent_path(), directory_error);
  if (directory_error) {
    return std::nullopt;
  }

  const auto temp_path = final_path.string() + ".tmp";
  {
    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output) {
      return std::nullopt;
    }

    for (std::uint64_t index = 0; index < manifest.piece_count; ++index) {
      const auto piece = load_piece(manifest, index);
      if (!piece) {
        std::error_code cleanup_error;
        std::filesystem::remove(temp_path, cleanup_error);
        return std::nullopt;
      }

      if (!piece->empty()) {
        output.write(piece->data(), static_cast<std::streamsize>(piece->size()));
      }

      if (!output) {
        std::error_code cleanup_error;
        std::filesystem::remove(temp_path, cleanup_error);
        return std::nullopt;
      }
    }
  }

  std::error_code size_error;
  const auto assembled_size = std::filesystem::file_size(temp_path, size_error);
  if (size_error || assembled_size != manifest.file_size) {
    std::error_code cleanup_error;
    std::filesystem::remove(temp_path, cleanup_error);
    return std::nullopt;
  }

  std::error_code remove_error;
  std::filesystem::remove(final_path, remove_error);
  if (remove_error) {
    std::error_code cleanup_error;
    std::filesystem::remove(temp_path, cleanup_error);
    return std::nullopt;
  }

  std::error_code rename_error;
  std::filesystem::rename(temp_path, final_path, rename_error);
  if (rename_error) {
    std::error_code cleanup_error;
    std::filesystem::remove(temp_path, cleanup_error);
    return std::nullopt;
  }

  return final_path;
}
