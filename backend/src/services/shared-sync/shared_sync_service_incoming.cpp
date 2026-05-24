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

bool SharedSyncService::publish_shared_from_chunks(const std::string& safe_name,
                                                   const SharedFileSignature& signature,
                                                   std::uint64_t chunk_size,
                                                   std::uint64_t chunk_count,
                                                   std::string& error_message) {
  const std::filesystem::path chunk_root = shared_chunk_root(safe_name, signature);
  const std::filesystem::path destination = state_.shared_dir / safe_name;
  const std::filesystem::path temp_path =
      state_.shared_dir / (".loopline-tmp-" + transfer::make_transfer_id() + "-" + safe_name);

  std::error_code error;
  const auto current_signature = shared_file_signature(destination);
  if (current_signature && *current_signature == signature) {
    std::filesystem::remove_all(chunk_root, error);
    return true;
  }

  std::filesystem::create_directories(state_.shared_dir, error);
  if (error) {
    error_message = "Could not create shared folder";
    return false;
  }

  std::ofstream output(temp_path, std::ios::binary);
  if (!output) {
    error_message = "Could not open temporary shared output";
    return false;
  }

  std::uint64_t hash = 14695981039346656037ull;
  std::uint64_t assembled = 0;
  std::vector<char> buffer(64 * 1024);

  for (std::uint64_t index = 0; index < chunk_count; ++index) {
    const std::uint64_t expected = shared_chunk_bytes(index, signature.size, chunk_size, chunk_count);
    if (expected == 0) {
      error_message = "Invalid shared chunk size while assembling";
      output.close();
      std::filesystem::remove(temp_path, error);
      return false;
    }

    const std::filesystem::path chunk_path = shared_chunk_file(safe_name, signature, index);
    std::ifstream input(chunk_path, std::ios::binary);
    if (!input) {
      error_message = "A required shared chunk file is missing";
      output.close();
      std::filesystem::remove(temp_path, error);
      return false;
    }

    std::uint64_t chunk_read = 0;
    while (chunk_read < expected) {
      const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), expected - chunk_read));
      input.read(buffer.data(), static_cast<std::streamsize>(want));
      const auto got = static_cast<std::size_t>(input.gcount());
      if (got == 0) {
        error_message = "Shared chunk data ended early";
        output.close();
        std::filesystem::remove(temp_path, error);
        return false;
      }
      output.write(buffer.data(), static_cast<std::streamsize>(got));
      if (!output) {
        error_message = "Could not write assembled shared file";
        output.close();
        std::filesystem::remove(temp_path, error);
        return false;
      }
      hash = fnv1a_update(hash, buffer.data(), got);
      chunk_read += static_cast<std::uint64_t>(got);
      assembled += static_cast<std::uint64_t>(got);
    }
  }

  output.close();
  if (assembled != signature.size || hash != signature.hash) {
    error_message = "Shared chunks failed integrity verification";
    std::filesystem::remove(temp_path, error);
    return false;
  }

  const auto existing_signature = shared_file_signature(destination);
  if (!existing_signature || *existing_signature != signature) {
    std::filesystem::remove(destination, error);
    error.clear();
    std::filesystem::rename(temp_path, destination, error);
    if (error) {
      std::filesystem::remove(temp_path, error);
      error_message = "Could not publish shared file";
      return false;
    }
  } else {
    std::filesystem::remove(temp_path, error);
  }

  std::filesystem::remove_all(chunk_root, error);
  return true;
}

void SharedSyncService::handle_incoming_shared_put_chunks(socket_t client,
                                                          const std::string& file_name,
                                                          const std::string& size_line,
                                                          const std::string& hash_line,
                                                          const std::string& chunk_size_line,
                                                          const std::string& chunk_count_line,
                                                          const std::string& peer,
                                                          const std::optional<transfer::PeerEndpoint>& source_peer) {
  const auto parsed_size = parse_u64(size_line);
  const auto parsed_hash = parse_u64(hash_line);
  const auto parsed_chunk_size = parse_u64(chunk_size_line);
  const auto parsed_chunk_count = parse_u64(chunk_count_line);
  if (!parsed_size || !parsed_hash || !parsed_chunk_size || !parsed_chunk_count) {
    close_socket(client);
    return;
  }

  if (*parsed_chunk_size == 0 || *parsed_chunk_size > (8 * 1024 * 1024)) {
    close_socket(client);
    return;
  }

  const SharedFileSignature signature{*parsed_size, *parsed_hash};
  const std::uint64_t expected_chunk_count = shared_chunk_count(signature.size, *parsed_chunk_size);
  if (expected_chunk_count != *parsed_chunk_count) {
    close_socket(client);
    return;
  }

  const std::string safe_name = transfer::safe_file_name(file_name);
  const std::uint64_t chunk_size = *parsed_chunk_size;
  const std::uint64_t chunk_count = *parsed_chunk_count;

  TransferRecord record;
  record.direction = "incoming";
  record.file_name = safe_name;
  record.status = "transferring";
  record.peer = peer;
  record.size = signature.size;
  record.message = "Receiving shared chunks";
  const std::string id = state_.add_transfer(record);

  std::error_code error;
  const std::filesystem::path chunk_root = shared_chunk_root(safe_name, signature);
  std::filesystem::create_directories(chunk_root, error);
  if (error) {
    state_.update_transfer(id, 0, "failed", "Could not create shared chunk folder");
    close_socket(client);
    return;
  }

  const auto initial_missing = shared_missing_chunks(safe_name, signature, chunk_size, chunk_count);
  if (!send_missing_chunks_response(client, initial_missing)) {
    state_.update_transfer(id, 0, "failed", "Could not send missing chunk list");
    close_socket(client);
    return;
  }

  std::uint64_t received_total = 0;
  std::vector<char> buffer(64 * 1024);
  while (true) {
    std::string command;
    if (!netio::read_line(client, command)) {
      state_.update_transfer(id, received_total, "failed", "Shared chunk stream closed early");
      close_socket(client);
      return;
    }

    if (command == "DONE") {
      std::string blank;
      if (!netio::read_line(client, blank)) {
        state_.update_transfer(id, received_total, "failed", "Shared chunk stream closed early");
      }
      break;
    }

    if (command != "CHUNK") {
      state_.update_transfer(id, received_total, "failed", "Unexpected shared chunk command");
      close_socket(client);
      return;
    }

    std::string chunk_index_line;
    std::string chunk_bytes_line;
    if (!netio::read_line(client, chunk_index_line) || !netio::read_line(client, chunk_bytes_line)) {
      state_.update_transfer(id, received_total, "failed", "Malformed shared chunk header");
      close_socket(client);
      return;
    }

    const auto chunk_index = parse_u64(chunk_index_line);
    const auto chunk_bytes = parse_u64(chunk_bytes_line);
    if (!chunk_index || !chunk_bytes || *chunk_index >= chunk_count) {
      state_.update_transfer(id, received_total, "failed", "Invalid shared chunk index");
      close_socket(client);
      return;
    }

    const std::uint64_t expected_bytes = shared_chunk_bytes(*chunk_index, signature.size, chunk_size, chunk_count);
    if (expected_bytes == 0 || expected_bytes != *chunk_bytes) {
      state_.update_transfer(id, received_total, "failed", "Invalid shared chunk size");
      close_socket(client);
      return;
    }

    const std::filesystem::path temp_chunk = shared_chunk_temp_file(safe_name, signature, *chunk_index);
    const std::filesystem::path final_chunk = shared_chunk_file(safe_name, signature, *chunk_index);
    std::ofstream chunk_output(temp_chunk, std::ios::binary);
    if (!chunk_output) {
      state_.update_transfer(id, received_total, "failed", "Could not open shared chunk file");
      close_socket(client);
      return;
    }

    std::uint64_t chunk_received = 0;
    while (chunk_received < *chunk_bytes) {
      const std::size_t want = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), *chunk_bytes - chunk_received));
      if (!netio::recv_exact(client, buffer.data(), want)) {
        chunk_output.close();
        std::filesystem::remove(temp_chunk, error);
        state_.update_transfer(id, received_total, "failed", "Shared chunk stream closed early");
        close_socket(client);
        return;
      }
      chunk_output.write(buffer.data(), static_cast<std::streamsize>(want));
      if (!chunk_output) {
        chunk_output.close();
        std::filesystem::remove(temp_chunk, error);
        state_.update_transfer(id, received_total, "failed", "Could not write shared chunk");
        close_socket(client);
        return;
      }
      chunk_received += static_cast<std::uint64_t>(want);
      received_total += static_cast<std::uint64_t>(want);
      state_.update_transfer(id,
                             received_total,
                             "transferring",
                             "Shared chunks " + transfer::format_bytes(received_total));
    }

    chunk_output.close();
    std::filesystem::remove(final_chunk, error);
    error.clear();
    std::filesystem::rename(temp_chunk, final_chunk, error);
    if (error) {
      std::filesystem::remove(temp_chunk, error);
      state_.update_transfer(id, received_total, "failed", "Could not finalize shared chunk");
      close_socket(client);
      return;
    }
  }

  std::string publish_error;
  if (!shared_has_all_chunks(safe_name, signature, chunk_size, chunk_count)) {
    state_.update_transfer(id, received_total, "queued", "Partial shared chunks saved; waiting for another peer");
    close_socket(client);
    return;
  }

  if (!publish_shared_from_chunks(safe_name, signature, chunk_size, chunk_count, publish_error)) {
    state_.update_transfer(id, received_total, "failed", publish_error);
    close_socket(client);
    return;
  }

  if (source_peer) {
    state_.mark_synced_shared_version(*source_peer, safe_name, signature);
  }
  state_.update_transfer(id, signature.size, "complete", "Saved to shared folder");
  close_socket(client);
}

void SharedSyncService::handle_incoming_shared_delete(socket_t client,
                                                      const std::string& file_name,
                                                      const std::string& peer,
                                                      const std::optional<transfer::PeerEndpoint>& source_peer) {
  const std::string safe_name = transfer::safe_file_name(file_name);
  TransferRecord record;
  record.direction = "incoming";
  record.file_name = safe_name;
  record.status = "transferring";
  record.peer = peer;
  record.size = 0;
  record.message = "Removing shared file";
  const std::string id = state_.add_transfer(record);

  std::error_code error;
  std::filesystem::create_directories(state_.shared_dir, error);
  const std::filesystem::path destination = state_.shared_dir / safe_name;
  if (std::filesystem::exists(destination, error)) {
    std::filesystem::remove(destination, error);
  }

  if (error) {
    state_.update_transfer(id, 0, "failed", "Could not remove shared file");
  } else {
    if (source_peer) {
      state_.mark_synced_shared_version(*source_peer, safe_name, SharedFileSignature{0, 0});
    }
    state_.update_transfer(id, 0, "complete", "Removed from shared folder");
  }
  close_socket(client);
}

void SharedSyncService::handle_incoming_shared_put(socket_t client,
                                                   const std::string& file_name,
                                                   const std::string& size_line,
                                                   const std::string& peer,
                                                   const std::optional<transfer::PeerEndpoint>& source_peer) {
  const auto parsed_size = parse_int(size_line);
  if (!parsed_size || *parsed_size < 0) {
    close_socket(client);
    return;
  }

  const std::string safe_name = transfer::safe_file_name(file_name);
  const std::uint64_t total_size = static_cast<std::uint64_t>(*parsed_size);
  TransferRecord record;
  record.direction = "incoming";
  record.file_name = safe_name;
  record.status = "transferring";
  record.peer = peer;
  record.size = total_size;
  record.message = "Receiving shared file";
  const std::string id = state_.add_transfer(record);

  std::error_code error;
  std::filesystem::create_directories(state_.shared_dir, error);
  if (error) {
    state_.update_transfer(id, 0, "failed", "Could not create shared folder");
    close_socket(client);
    return;
  }

  const std::filesystem::path destination = state_.shared_dir / safe_name;
  const std::filesystem::path temp_path =
      state_.shared_dir / (".loopline-tmp-" + transfer::make_transfer_id() + "-" + safe_name);
  std::filesystem::remove(temp_path, error);

  std::ofstream output(temp_path, std::ios::binary);
  if (!output) {
    state_.update_transfer(id, 0, "failed", "Could not open shared file");
    close_socket(client);
    return;
  }

  std::uint64_t hash = 14695981039346656037ull;
  std::uint64_t received_total = 0;
  std::vector<char> buffer(64 * 1024);
  while (received_total < total_size) {
    const auto remaining = static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), total_size - received_total));
    const int received = recv(client, buffer.data(), static_cast<int>(remaining), 0);
    if (received <= 0) {
      output.close();
      std::filesystem::remove(temp_path, error);
      state_.update_transfer(id, received_total, "failed", "Connection closed early");
      close_socket(client);
      return;
    }
    output.write(buffer.data(), received);
    hash = fnv1a_update(hash, buffer.data(), static_cast<std::size_t>(received));
    received_total += static_cast<std::uint64_t>(received);
    state_.update_transfer(id,
                           received_total,
                           "transferring",
                           "Shared " + transfer::format_bytes(received_total));
  }
  output.close();

  const SharedFileSignature received_signature{received_total, hash};
  const auto existing_signature = shared_file_signature(destination);
  if (existing_signature && *existing_signature == received_signature) {
    std::filesystem::remove(temp_path, error);
    if (source_peer) {
      state_.mark_synced_shared_version(*source_peer, safe_name, received_signature);
    }
    state_.update_transfer(id, received_total, "complete", "Shared file already current");
    close_socket(client);
    return;
  }

  std::filesystem::remove(destination, error);
  error.clear();
  std::filesystem::rename(temp_path, destination, error);
  if (error) {
    std::filesystem::remove(temp_path, error);
    state_.update_transfer(id, received_total, "failed", "Could not publish shared file");
    close_socket(client);
    return;
  }

  if (source_peer) {
    state_.mark_synced_shared_version(*source_peer, safe_name, received_signature);
  }
  state_.update_transfer(id, received_total, "complete", "Saved to shared folder");
  close_socket(client);
}

bool SharedSyncService::handle_shared_protocol(socket_t client, const std::string& magic, const std::string& peer) {
  if (magic == "LOOPLINE-SHARED/2") {
    std::string operation;
    std::string file_name;
    std::string size_line;
    std::string hash_line;
    std::string chunk_size_line;
    std::string chunk_count_line;
    std::string source_node;
    std::string source_host;
    std::string source_port;
    std::string blank;
    if (!netio::read_line(client, operation) || !netio::read_line(client, file_name) || !netio::read_line(client, size_line) ||
        !netio::read_line(client, hash_line) || !netio::read_line(client, chunk_size_line) || !netio::read_line(client, chunk_count_line) ||
        !netio::read_line(client, source_node) || !netio::read_line(client, source_host) || !netio::read_line(client, source_port) ||
        !netio::read_line(client, blank)) {
      close_socket(client);
      return true;
    }

    (void)source_node;
    const auto source_peer = source_peer_from_header(source_host, source_port, state_.transfer_port);
    if (operation == "PUT-CHUNKS") {
      handle_incoming_shared_put_chunks(
          client, file_name, size_line, hash_line, chunk_size_line, chunk_count_line, peer, source_peer);
      return true;
    }

    close_socket(client);
    return true;
  }

  if (magic == "LOOPLINE-SHARED/1") {
    std::string operation;
    std::string file_name;
    std::string size_line;
    std::string source_node;
    std::string source_host;
    std::string source_port;
    std::string blank;
    if (!netio::read_line(client, operation) || !netio::read_line(client, file_name) || !netio::read_line(client, size_line) ||
        !netio::read_line(client, source_node) || !netio::read_line(client, source_host) || !netio::read_line(client, source_port) ||
        !netio::read_line(client, blank)) {
      close_socket(client);
      return true;
    }

    (void)source_node;
    const auto source_peer = source_peer_from_header(source_host, source_port, state_.transfer_port);
    if (operation == "DELETE") {
      handle_incoming_shared_delete(client, file_name, peer, source_peer);
      return true;
    }
    if (operation == "PUT") {
      handle_incoming_shared_put(client, file_name, size_line, peer, source_peer);
      return true;
    }

    close_socket(client);
    return true;
  }

  return false;
}

